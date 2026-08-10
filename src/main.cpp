#include <Arduino.h>
#include <MIDI.h>
#include <USBHost_t36.h>

// Physical MIDI DIN/DOUT on Teensy Serial1.
// Pins: TX = 1, RX = 0.
MIDI_CREATE_INSTANCE(HardwareSerial, Serial1, midiPort);

// Connect the Novation Launchpad X to Teensy's USB host port.
USBHost myusb;
USBHub hub1(myusb);
MIDIDevice_BigBuffer launchpad(myusb);

namespace {
constexpr uint8_t kStepCount = 16;
constexpr uint8_t kMidiChannelCount = 16;
constexpr uint8_t kLaunchpadGridNoteMin = 0;
constexpr uint8_t kLaunchpadGridNoteMax = 63;
constexpr uint8_t kLaunchpadTopRowNoteMin = 60;
constexpr uint8_t kLaunchpadTopRowNoteMax = 111;
constexpr uint8_t kLaunchpadRightColumnNoteMin = 112;
constexpr uint8_t kLaunchpadRightColumnNoteMax = 119;
constexpr uint16_t kMinTempoBpm = 60;
constexpr uint16_t kMaxTempoBpm = 200;
constexpr uint8_t kRatchetMax = 4;
constexpr uint8_t kMicrostepMax = 8;
constexpr uint8_t kSwingMax = 50;
constexpr uint8_t kLaunchpadColorOff = 0;
constexpr uint8_t kLaunchpadColorGreenLow = 1;
constexpr uint8_t kLaunchpadColorGreenHigh = 3;
constexpr uint8_t kLaunchpadColorAmberLow = 7;
constexpr uint8_t kLaunchpadColorAmberHigh = 9;
constexpr uint8_t kLaunchpadColorRedLow = 4;
constexpr uint8_t kLaunchpadColorRedHigh = 6;
constexpr uint8_t kLaunchpadColorBlueLow = 10;

struct StepLaneState {
  bool active = false;
  byte note = 60;
  byte velocity = 100;
};

StepLaneState g_sequence[kStepCount][kMidiChannelCount];

uint8_t g_stepIndex = 0;
uint8_t g_lastPlayedStep = 0;
bool g_hasPlayedStep = false;
uint16_t g_tempoBpm = 120;
uint8_t g_microstepDivisions = 1;
uint8_t g_ratchetCount = 1;
uint8_t g_swingPct = 0;
bool g_running = true;
bool g_recording = false;
bool g_overdub = false;
elapsedMillis g_stepTimer;
elapsedMillis g_clockPulseTimer;
elapsedMillis g_ledTimer;
elapsedMillis g_statusTimer;
elapsedMillis g_launchpadInitTimer;
bool g_seenLaunchpadInput = false;
bool g_launchpadProgramModeSent = false;
bool g_launchpadTestPatternSent = false;
bool g_launchpadControlLedsInitialized = false;

void refreshLaunchpadGridLedState();

bool isLaunchpadGridPad(byte note) {
  return note >= kLaunchpadGridNoteMin && note <= kLaunchpadGridNoteMax;
}

bool isLaunchpadTopRowNote(byte note) {
  return note >= kLaunchpadTopRowNoteMin && note <= kLaunchpadTopRowNoteMax;
}

bool isLaunchpadRightColumnNote(byte note) {
  return note >= kLaunchpadRightColumnNoteMin && note <= kLaunchpadRightColumnNoteMax;
}

bool isLaunchpadControlNote(byte note) {
  return isLaunchpadTopRowNote(note) || isLaunchpadRightColumnNote(note);
}

uint16_t calculateStepDurationMs() {
  return static_cast<uint16_t>(60000.0f / static_cast<float>(g_tempoBpm) / 4.0f / static_cast<float>(g_microstepDivisions));
}

uint16_t calculateClockPulseMs() {
  return static_cast<uint16_t>(60000.0f / static_cast<float>(g_tempoBpm) / 24.0f);
}

void sendMidiMessage(byte channel, byte note, byte velocity, bool noteOn) {
  if (channel >= kMidiChannelCount) {
    channel = 0;
  }
  midiPort.send(noteOn ? midi::NoteOn : midi::NoteOff, note, velocity, channel);
}

void sendStepClockPulse() {
  if (!g_running) {
    return;
  }
  midiPort.sendClock();
}

void stageLaunchpadPad(byte channel, byte note, byte velocity, bool active) {
  if (!isLaunchpadGridPad(note)) {
    return;
  }

  const uint8_t row = note / 8;
  const uint8_t col = note % 8;
  const uint8_t step = col;
  const uint8_t laneChannel = row;
  StepLaneState& lane = g_sequence[step][laneChannel];
  lane.active = active;
  lane.note = note + 48;
  lane.velocity = velocity;

  refreshLaunchpadGridLedState();
}

void setLaunchpadLedColor(byte note, byte color) {
  static const uint8_t kLaunchpadSysexHeader[] = {0xF0, 0x00, 0x20, 0x29, 0x02, 0x0C};
  uint8_t data[sizeof(kLaunchpadSysexHeader) + 4];
  memcpy(data, kLaunchpadSysexHeader, sizeof(kLaunchpadSysexHeader));
  data[sizeof(kLaunchpadSysexHeader)] = 0x03;
  data[sizeof(kLaunchpadSysexHeader) + 1] = 0x00;
  data[sizeof(kLaunchpadSysexHeader) + 2] = note;
  data[sizeof(kLaunchpadSysexHeader) + 3] = color;
  launchpad.sendSysEx(sizeof(kLaunchpadSysexHeader) + 4, data, false);
  delayMicroseconds(100);
}

void sendLaunchpadProgramMode() {
  static const uint8_t kProgramModeSysex[] = {0xF0, 0x00, 0x20, 0x29, 0x02, 0x0C, 0x0E, 0x01, 0xF7};
  launchpad.sendSysEx(sizeof(kProgramModeSysex), kProgramModeSysex, true);
}

void clearCurrentStep(uint8_t step) {
  for (uint8_t channel = 0; channel < kMidiChannelCount; ++channel) {
    g_sequence[step][channel].active = false;
  }
}

void recordCurrentStep(byte channel, byte note, byte velocity) {
  if (!g_recording) {
    return;
  }

  if (!g_overdub) {
    clearCurrentStep(g_stepIndex);
  }

  StepLaneState& lane = g_sequence[g_stepIndex][channel];
  lane.active = (velocity > 0);
  lane.note = note;
  lane.velocity = velocity;
}

void refreshLaunchpadGridLedState() {
  for (uint8_t note = 0; note < 64; ++note) {
    const uint8_t row = note / 8;
    const uint8_t col = note % 8;
    const uint8_t step = col;
    const uint8_t channel = row;
    const bool isCurrentStep = (step == (g_stepIndex % 8));
    const bool isActiveLane = g_sequence[step][channel].active;

    uint8_t color = kLaunchpadColorOff;
    if (isCurrentStep && isActiveLane) {
      color = kLaunchpadColorGreenHigh;
    } else if (isCurrentStep) {
      color = kLaunchpadColorGreenLow;
    } else if (isActiveLane) {
      color = kLaunchpadColorAmberLow;
    }

    setLaunchpadLedColor(note, color);
  }
}

void testLaunchpadControlButtons() {
  for (uint8_t i = 0; i < 8; ++i) {
    setLaunchpadLedColor(kLaunchpadTopRowNoteMin + i, kLaunchpadColorGreenHigh);
  }
  for (uint8_t i = 0; i < 8; ++i) {
    setLaunchpadLedColor(kLaunchpadRightColumnNoteMin + i, kLaunchpadColorAmberHigh);
  }
  delay(750);
}

void refreshLaunchpadControlLedState() {
  for (uint8_t i = 0; i < 8; ++i) {
    setLaunchpadLedColor(kLaunchpadTopRowNoteMin + i, kLaunchpadColorOff);
  }
  for (uint8_t i = 0; i < 8; ++i) {
    setLaunchpadLedColor(kLaunchpadRightColumnNoteMin + i, kLaunchpadColorOff);
  }

  setLaunchpadLedColor(kLaunchpadTopRowNoteMin + 0, g_tempoBpm <= kMinTempoBpm ? kLaunchpadColorAmberLow : kLaunchpadColorGreenLow);
  setLaunchpadLedColor(kLaunchpadTopRowNoteMin + 1, g_tempoBpm >= kMaxTempoBpm ? kLaunchpadColorAmberLow : kLaunchpadColorGreenLow);
  setLaunchpadLedColor(kLaunchpadTopRowNoteMin + 2, g_microstepDivisions > 1 ? kLaunchpadColorAmberHigh : kLaunchpadColorOff);
  setLaunchpadLedColor(kLaunchpadTopRowNoteMin + 3, g_swingPct < kSwingMax ? kLaunchpadColorAmberLow : kLaunchpadColorAmberHigh);
  setLaunchpadLedColor(kLaunchpadTopRowNoteMin + 4, g_swingPct > 0 ? kLaunchpadColorAmberLow : kLaunchpadColorOff);
  setLaunchpadLedColor(kLaunchpadTopRowNoteMin + 5, g_ratchetCount > 1 ? kLaunchpadColorGreenLow : kLaunchpadColorOff);
  setLaunchpadLedColor(kLaunchpadTopRowNoteMin + 6, g_ratchetCount > 1 ? kLaunchpadColorRedLow : kLaunchpadColorOff);
  setLaunchpadLedColor(kLaunchpadTopRowNoteMin + 7, g_running ? kLaunchpadColorGreenHigh : kLaunchpadColorRedHigh);

  setLaunchpadLedColor(kLaunchpadRightColumnNoteMin + 0, kLaunchpadColorRedHigh);
  setLaunchpadLedColor(kLaunchpadRightColumnNoteMin + 1, kLaunchpadColorGreenHigh);
  setLaunchpadLedColor(kLaunchpadRightColumnNoteMin + 2, kLaunchpadColorBlueLow);
  setLaunchpadLedColor(kLaunchpadRightColumnNoteMin + 3, kLaunchpadColorBlueLow);
  setLaunchpadLedColor(kLaunchpadRightColumnNoteMin + 4, kLaunchpadColorAmberLow);
  setLaunchpadLedColor(kLaunchpadRightColumnNoteMin + 5, kLaunchpadColorAmberLow);
  setLaunchpadLedColor(kLaunchpadRightColumnNoteMin + 6, g_recording ? kLaunchpadColorRedHigh : kLaunchpadColorOff);
  setLaunchpadLedColor(kLaunchpadRightColumnNoteMin + 7, g_overdub ? kLaunchpadColorBlueLow : kLaunchpadColorOff);
}

void handleLaunchpadControl(byte note) {
  switch (note) {
    case kLaunchpadTopRowNoteMin + 0:
      if (g_tempoBpm > kMinTempoBpm) {
        g_tempoBpm -= 10;
      }
      break;
    case kLaunchpadTopRowNoteMin + 1:
      if (g_tempoBpm < kMaxTempoBpm) {
        g_tempoBpm += 10;
      }
      break;
    case kLaunchpadTopRowNoteMin + 2:
      g_microstepDivisions = (g_microstepDivisions >= kMicrostepMax) ? 1 : (g_microstepDivisions * 2);
      break;
    case kLaunchpadTopRowNoteMin + 3:
      if (g_swingPct < kSwingMax) {
        g_swingPct += 5;
      }
      break;
    case kLaunchpadTopRowNoteMin + 4:
      if (g_swingPct > 0) {
        g_swingPct -= 5;
      }
      break;
    case kLaunchpadTopRowNoteMin + 5:
      if (g_ratchetCount < kRatchetMax) {
        g_ratchetCount += 1;
      }
      break;
    case kLaunchpadTopRowNoteMin + 6:
      if (g_ratchetCount > 1) {
        g_ratchetCount -= 1;
      }
      break;
    case kLaunchpadTopRowNoteMin + 7:
      g_running = !g_running;
      if (g_running) {
        midiPort.sendStart();
      } else {
        midiPort.sendStop();
      }
      break;
    case kLaunchpadRightColumnNoteMin + 0:
      g_running = false;
      midiPort.sendStop();
      break;
    case kLaunchpadRightColumnNoteMin + 1:
      g_running = true;
      midiPort.sendStart();
      break;
    case kLaunchpadRightColumnNoteMin + 2:
      memset(g_sequence, 0, sizeof(g_sequence));
      refreshLaunchpadGridLedState();
      break;
    case kLaunchpadRightColumnNoteMin + 3:
      g_stepIndex = 0;
      break;
    case kLaunchpadRightColumnNoteMin + 4:
      g_tempoBpm = 120;
      break;
    case kLaunchpadRightColumnNoteMin + 5:
      g_swingPct = 0;
      break;
    case kLaunchpadRightColumnNoteMin + 6:
      g_recording = !g_recording;
      break;
    case kLaunchpadRightColumnNoteMin + 7:
      g_overdub = !g_overdub;
      break;
    default:
      break;
  }

  refreshLaunchpadControlLedState();
}

void onLaunchpadNoteOn(byte channel, byte note, byte velocity) {
  g_seenLaunchpadInput = true;
  Serial.printf("Launchpad note on ch=%u note=%u vel=%u\n", channel + 1, note, velocity);
  if (isLaunchpadControlNote(note)) {
    handleLaunchpadControl(note);
    return;
  }
  if (isLaunchpadGridPad(note)) {
    stageLaunchpadPad(channel, note, velocity, true);
  }
}

void onLaunchpadNoteOff(byte channel, byte note, byte velocity) {
  g_seenLaunchpadInput = true;
  Serial.printf("Launchpad note off ch=%u note=%u vel=%u\n", channel + 1, note, velocity);
  if (isLaunchpadControlNote(note)) {
    return;
  }
  if (isLaunchpadGridPad(note)) {
    stageLaunchpadPad(channel, note, velocity, false);
  }
}

void onLaunchpadControlChange(byte channel, byte control, byte value) {
  (void)channel;
  (void)control;
  (void)value;
}

void handleHardwareMidiIn() {
  const auto type = midiPort.getType();
  const auto channel = midiPort.getChannel();
  const auto data1 = midiPort.getData1();
  const auto data2 = midiPort.getData2();

  if (g_recording) {
    if (type == midi::NoteOn) {
      recordCurrentStep(channel, data1, data2);
    } else if (type == midi::NoteOff) {
      recordCurrentStep(channel, data1, 0);
    }
  }

  if (type == midi::NoteOn && data2 > 0) {
    Serial.printf("HW MIDI IN ch=%u note=%u vel=%u\n", channel + 1, data1, data2);
  } else if (type == midi::NoteOff || (type == midi::NoteOn && data2 == 0)) {
    Serial.printf("HW MIDI IN ch=%u note=%u off\n", channel + 1, data1);
  }
}

void sendActiveStepNotes(uint8_t step) {
  for (uint8_t channel = 0; channel < kMidiChannelCount; ++channel) {
    StepLaneState& lane = g_sequence[step][channel];
    if (!lane.active) {
      continue;
    }

    const uint16_t swingDelayUs = (step % 2 == 1 && g_swingPct > 0)
        ? (calculateStepDurationMs() * g_swingPct / 100U) * 1000U / 2U
        : 0;
    if (swingDelayUs > 0) {
      delayMicroseconds(swingDelayUs);
    }

    for (uint8_t ratchet = 0; ratchet < g_ratchetCount; ++ratchet) {
      sendMidiMessage(channel, lane.note, lane.velocity, true);
      delayMicroseconds(250);
      sendMidiMessage(channel, lane.note, 0, false);
      if (ratchet + 1 < g_ratchetCount) {
        delayMicroseconds(static_cast<uint32_t>(calculateStepDurationMs()) * 1000U / g_ratchetCount);
      }
    }
  }
}

void suppressLastStepNotes(uint8_t step) {
  for (uint8_t channel = 0; channel < kMidiChannelCount; ++channel) {
    StepLaneState& lane = g_sequence[step][channel];
    if (lane.active) {
      sendMidiMessage(channel, lane.note, 0, false);
    }
  }
}

void advanceSequencerStep() {
  if (g_hasPlayedStep) {
    suppressLastStepNotes(g_lastPlayedStep);
  }

  sendActiveStepNotes(g_stepIndex);
  g_lastPlayedStep = g_stepIndex;
  g_stepIndex = (g_stepIndex + 1) % kStepCount;
  g_hasPlayedStep = true;
}

void debugPrintState() {
  Serial.print("tempo=");
  Serial.print(g_tempoBpm);
  Serial.print(" micro=");
  Serial.print(g_microstepDivisions);
  Serial.print(" swing=");
  Serial.print(g_swingPct);
  Serial.print(" ratchet=");
  Serial.print(g_ratchetCount);
  Serial.print(" step=");
  Serial.println(g_stepIndex);
}
}  // namespace

void setup() {
  pinMode(LED_BUILTIN, OUTPUT);
  digitalWriteFast(LED_BUILTIN, LOW);

  Serial.begin(115200);
  while (!Serial && millis() < 1000) {
    ;
  }

  Serial1.begin(31250);
  midiPort.begin(MIDI_CHANNEL_OMNI);
  midiPort.sendStart();

  launchpad.setHandleNoteOn(onLaunchpadNoteOn);
  launchpad.setHandleNoteOff(onLaunchpadNoteOff);
  launchpad.setHandleControlChange(onLaunchpadControlChange);

  myusb.begin();

  delay(500);
  sendLaunchpadProgramMode();
  delay(250);
  testLaunchpadControlButtons();
  refreshLaunchpadControlLedState();
  refreshLaunchpadGridLedState();





  Serial.println("Launchpad USB host + 16-channel MIDI ready");
  Serial.println("Waiting for Launchpad X on the Teensy USB host port...");

  refreshLaunchpadControlLedState();
}

void loop() {
  myusb.Task();
  launchpad.read();

  if (midiPort.read()) {
    handleHardwareMidiIn();
  }

  if (g_running && g_clockPulseTimer >= calculateClockPulseMs()) {
    g_clockPulseTimer = 0;
    sendStepClockPulse();
  }

  if (g_running && g_stepTimer >= calculateStepDurationMs()) {
    g_stepTimer = 0;
    advanceSequencerStep();
    debugPrintState();
    refreshLaunchpadGridLedState();
  }

  if (g_ledTimer >= 250) {
    g_ledTimer = 0;
    digitalWriteFast(LED_BUILTIN, !digitalReadFast(LED_BUILTIN));
  }

  if (g_statusTimer >= 1000) {
    g_statusTimer = 0;
    if (!g_seenLaunchpadInput) {
      Serial.println("USB host task running; no Launchpad input seen yet");
    }
  }
}