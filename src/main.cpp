#include <Arduino.h>
#include <MIDI.h>
#include <USBHost_t36.h>

// Physical MIDI DIN/DOUT on Teensy Serial1.
// Pins: TX = 1, RX = 0.
MIDI_CREATE_INSTANCE(HardwareSerial, Serial1, midiPort);

// Connect the Novation Launchpad X to Teensy's USB host port, solder edge +5V to host +5V pin.
USBHost myusb;
USBHub hub1(myusb);
MIDIDevice_BigBuffer launchpad(myusb);

namespace
{
  // todo increase this with modifier
  constexpr uint8_t kStepCount = 16;
  constexpr uint8_t kMidiChannelCount = 16;
  uint8_t g_stepOffset = 0;    // For scrolling steps left/right
  uint8_t g_channelOffset = 0; // For scrolling channels up/down
  constexpr uint8_t kLaunchpadGridNoteMin = 0;
  // cannot be phisically more than 88, maybe virtually
  constexpr uint8_t kLaunchpadGridNoteMax = 88;
  constexpr uint8_t kLaunchpadTopRowControlNoteMin = 91;
  constexpr uint8_t kLaunchpadTopRowControlNoteMax = 98;
  constexpr uint8_t kLaunchpadRightColumnControlNoteMin = 19;
  constexpr uint8_t kLaunchpadRightColumnControlNoteMax = 89;
  constexpr uint16_t kMinTempoBpm = 60;
  constexpr uint16_t kMaxTempoBpm = 200;
  constexpr uint8_t kRatchetMax = 4;
  constexpr uint8_t kMicrostepMax = 8;
  constexpr uint8_t kSwingMax = 50;
  constexpr uint8_t kLaunchpadColorOff = 0;
  constexpr uint8_t kLaunchpadColorWhiteLow = 1;
  constexpr uint8_t kLaunchpadColorWhiteHigh = 3;
  constexpr uint8_t kLaunchpadColorAmberLow = 7;
  constexpr uint8_t kLaunchpadColorAmberHigh = 9;
  constexpr uint8_t kLaunchpadColorRedLow = 4;
  constexpr uint8_t kLaunchpadColorRedHigh = 6;
  constexpr uint8_t kLaunchpadColorBlueLow = 10;
  constexpr uint8_t kLaunchpadColorYellowLow = 12;
  constexpr uint8_t kLaunchpadColorYellowHigh = 14;
  constexpr uint8_t kLaunchpadColorGreenLow = 16;
  constexpr uint8_t kLaunchpadColorGreenHigh = 18;

  struct StepLaneState
  {
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
  bool g_recordingHeldNote = false;     // Flag for recording when holding right-column control note
  uint8_t g_recordingChannelOffset = 0; // Channel offset for recording
  elapsedMillis g_stepTimer;
  elapsedMillis g_clockPulseTimer;
  elapsedMillis g_ledTimer;
  elapsedMillis g_statusTimer;
  elapsedMillis g_launchpadInitTimer;
  bool g_seenLaunchpadInput = false;
  bool g_launchpadProgramModeSent = false;
  bool g_launchpadTestPatternSent = false;
  bool g_launchpadControlLedsInitialized = false;
  byte g_lastPressedControlNote = 0xFF; // 0xFF means no control button recently pressed
  byte g_controlFlashNote = 0xFF;       // which control note is currently flashing White

  void refreshLaunchpadGridLedState();
  void refreshLaunchpadControlLedState();

  bool isLaunchpadTopRowControlNote(byte note)
  {
    return note >= kLaunchpadTopRowControlNoteMin && note <= kLaunchpadTopRowControlNoteMax;
  }

  // Right-column control notes occur every 10 (step=19,29,...89).
  bool isLaunchpadRightColumnControlNote(byte note)
  {
    return note >= kLaunchpadRightColumnControlNoteMin &&
           note <= kLaunchpadRightColumnControlNoteMax &&
           ((note - kLaunchpadRightColumnControlNoteMin) % 10 == 0);
  }

  // Control buttons occupy top-row (91–98) and right-column (19,29,...89).
  bool isLaunchpadControlNote(byte note)
  {
    return isLaunchpadTopRowControlNote(note) || isLaunchpadRightColumnControlNote(note);
  }

  bool isLaunchpadGridPad(byte note)
  {
    return note >= kLaunchpadGridNoteMin && note <= kLaunchpadGridNoteMax && !isLaunchpadControlNote(note);
  }

  uint16_t calculateStepDurationMs()
  {
    return static_cast<uint16_t>(60000.0f / static_cast<float>(g_tempoBpm) / 4.0f / static_cast<float>(g_microstepDivisions));
  }

  uint16_t calculateClockPulseMs()
  {
    return static_cast<uint16_t>(60000.0f / static_cast<float>(g_tempoBpm) / 24.0f);
  }

  void sendMidiMessage(byte channel, byte note, byte velocity, bool noteOn)
  {
    // Internal sequencer uses 0..15.
    // MIDI library uses 1..16.
    if (channel >= kMidiChannelCount)
    {
      channel = 0;
    }

    const byte midiChannel = channel + 1;

    midiPort.send(
        noteOn ? midi::NoteOn : midi::NoteOff,
        note,
        velocity,
        midiChannel);
  }

  void sendStepClockPulse()
  {
    if (!g_running)
    {
      return;
    }
    midiPort.sendClock();
  }

  void stageLaunchpadPad(byte channel, byte note, byte velocity, bool active)
  {
    if (!isLaunchpadGridPad(note))
    {
      return;
    }

    const uint8_t row = note / 8;
    const uint8_t col = note % 8;
    const uint8_t step = (col + g_stepOffset) % kStepCount;
    const uint8_t laneChannel = (row + g_channelOffset) % kMidiChannelCount;
    StepLaneState &lane = g_sequence[step][laneChannel];
    lane.active = active;
    lane.note = note + 48;
    lane.velocity = velocity;

    refreshLaunchpadGridLedState();
  }

  void setLaunchpadLedColor(byte note, byte color)
  {
    static const uint8_t kLaunchpadSysexHeader[] = {0xF0, 0x00, 0x20, 0x29, 0x02, 0x0C};
    uint8_t data[sizeof(kLaunchpadSysexHeader) + 4];
    memcpy(data, kLaunchpadSysexHeader, sizeof(kLaunchpadSysexHeader));
    data[sizeof(kLaunchpadSysexHeader)] = 0x03;
    data[sizeof(kLaunchpadSysexHeader) + 1] = 0x00;
    data[sizeof(kLaunchpadSysexHeader) + 2] = note;
    data[sizeof(kLaunchpadSysexHeader) + 3] = color;
    launchpad.sendSysEx(sizeof(kLaunchpadSysexHeader) + 4, data, false);
    // delayMicroseconds(100);
  }

  void sendLaunchpadProgramMode()
  {
    static const uint8_t kProgramModeSysex[] = {0xF0, 0x00, 0x20, 0x29, 0x02, 0x0C, 0x0E, 0x01, 0xF7};
    launchpad.sendSysEx(sizeof(kProgramModeSysex), kProgramModeSysex, true);
  }

  void clearCurrentStep(uint8_t step)
  {
    for (uint8_t channel = 0; channel < kMidiChannelCount; ++channel)
    {
      g_sequence[step][channel].active = false;
    }
  }

  void recordCurrentStep(byte channel, byte note, byte velocity)
  {
    Serial.printf("rec=%u, %u, %u \n", channel, note, velocity);
    if (!g_recording)
    {
      return;
    }

    if (!g_overdub)
    {
      clearCurrentStep(g_stepIndex);
    }

    StepLaneState &lane = g_sequence[g_stepIndex][channel];
    lane.active = (velocity > 0);
    lane.note = note;
    lane.velocity = velocity;
  }

  void refreshLaunchpadGridLedState()
  {
    for (uint8_t note = 0; note < kLaunchpadRightColumnControlNoteMax; ++note)
    {
      if (!isLaunchpadControlNote(note))
        setLaunchpadLedColor(note, kLaunchpadColorWhiteHigh);
    }

    // Light up the scrolling indicator LEDs
    // First two top row buttons (91, 92) for channel scrolling
    setLaunchpadLedColor(kLaunchpadTopRowControlNoteMin + 2, g_channelOffset > 0 ? kLaunchpadColorWhiteLow : kLaunchpadColorOff);
    setLaunchpadLedColor(kLaunchpadTopRowControlNoteMin + 3, (g_channelOffset + 8) < kMidiChannelCount ? kLaunchpadColorWhiteLow : kLaunchpadColorOff);

    // Next two top row buttons (93, 94) for step scrolling
    setLaunchpadLedColor(kLaunchpadTopRowControlNoteMin + 4, g_stepOffset > 0 ? kLaunchpadColorWhiteLow : kLaunchpadColorOff);
    setLaunchpadLedColor(kLaunchpadTopRowControlNoteMin + 5, (g_stepOffset + 8) < kStepCount ? kLaunchpadColorWhiteLow : kLaunchpadColorOff);
  }

  void refreshLaunchpadControlLedState()
  {
    // Turn off all control LEDs first
    for (uint8_t i = 0; i < 8; ++i)
    {
      setLaunchpadLedColor(kLaunchpadTopRowControlNoteMin + i, kLaunchpadColorOff);
    }
    for (uint8_t i = 0; i < 80; i += 10)
    {
      setLaunchpadLedColor(kLaunchpadRightColumnControlNoteMin + i, kLaunchpadColorOff);
    }

    // Set channel scrolling LEDs
    setLaunchpadLedColor(kLaunchpadTopRowControlNoteMin + 0, g_channelOffset > 0 ? kLaunchpadColorWhiteLow : kLaunchpadColorOff);
    setLaunchpadLedColor(kLaunchpadTopRowControlNoteMin + 1, (g_channelOffset + 8) < kMidiChannelCount ? kLaunchpadColorWhiteLow : kLaunchpadColorOff);

    // Set step scrolling LEDs
    setLaunchpadLedColor(kLaunchpadTopRowControlNoteMin + 2, g_stepOffset > 0 ? kLaunchpadColorWhiteLow : kLaunchpadColorOff);
    setLaunchpadLedColor(kLaunchpadTopRowControlNoteMin + 3, (g_stepOffset + 8) < kStepCount ? kLaunchpadColorWhiteLow : kLaunchpadColorOff);

    // Set tempo control LEDs
    setLaunchpadLedColor(kLaunchpadTopRowControlNoteMin + 4, g_tempoBpm <= kMinTempoBpm ? kLaunchpadColorAmberLow : kLaunchpadColorWhiteLow);
    setLaunchpadLedColor(kLaunchpadTopRowControlNoteMin + 5, g_tempoBpm >= kMaxTempoBpm ? kLaunchpadColorAmberLow : kLaunchpadColorWhiteLow);

    // Set other control LEDs
    setLaunchpadLedColor(kLaunchpadTopRowControlNoteMin + 6, g_microstepDivisions > 1 ? kLaunchpadColorAmberHigh : kLaunchpadColorOff);
    setLaunchpadLedColor(kLaunchpadTopRowControlNoteMin + 7, g_running ? kLaunchpadColorWhiteHigh : kLaunchpadColorRedHigh);

    // Set right column LEDs
    setLaunchpadLedColor(kLaunchpadRightColumnControlNoteMin + 0, kLaunchpadColorRedHigh);
    setLaunchpadLedColor(kLaunchpadRightColumnControlNoteMin + 10, kLaunchpadColorWhiteHigh);
    setLaunchpadLedColor(kLaunchpadRightColumnControlNoteMin + 20, kLaunchpadColorBlueLow);
    setLaunchpadLedColor(kLaunchpadRightColumnControlNoteMin + 30, kLaunchpadColorBlueLow);
    setLaunchpadLedColor(kLaunchpadRightColumnControlNoteMin + 40, kLaunchpadColorAmberLow);
    setLaunchpadLedColor(kLaunchpadRightColumnControlNoteMin + 50, kLaunchpadColorAmberLow);
    setLaunchpadLedColor(kLaunchpadRightColumnControlNoteMin + 60, g_recording ? kLaunchpadColorRedHigh : kLaunchpadColorOff);
    setLaunchpadLedColor(kLaunchpadRightColumnControlNoteMin + 70, g_overdub ? kLaunchpadColorBlueLow : kLaunchpadColorOff);
  }

  void playAllChannelSequence()
  {
    // Temporarily stop the sequencer
    bool wasRunning = g_running;
    g_running = false;

    // Play all steps in sequence
    for (uint8_t step = 0; step < kStepCount; ++step)
    {
      // For each step, play notes from all channels
      for (uint8_t channel = 0; channel < kMidiChannelCount; ++channel)
      {
        StepLaneState &lane = g_sequence[step][channel];
        if (/*true ||*/ lane.active)
        {
          sendMidiMessage(channel, lane.note, lane.velocity, true);
          // sendMidiMessage(1, 60, 127, true);
          Serial.printf("play=%u, %u, %u \n", channel, lane.note, lane.velocity);
          delayMicroseconds(250); // Short delay between notes
          sendMidiMessage(channel, lane.note, 0, false);
        }
      }

      // Delay between steps to maintain tempo
      uint16_t stepDuration = calculateStepDurationMs();
      delay(stepDuration);
    }

    // Restore sequencer state
    g_running = wasRunning;
    if (g_running)
    {
      midiPort.sendStart();
    }
  }

  void handleLaunchpadControl(byte note)
  {
    Serial.printf("handled");
    switch (note)
    {
    case kLaunchpadTopRowControlNoteMin + 0:
      // Channel scrolling up
      if (g_channelOffset > 0)
      {
        g_channelOffset -= 8;
      }
      Serial.printf("go up=%u\n", g_channelOffset);
      break;
    case kLaunchpadTopRowControlNoteMin + 1:
      // Channel scrolling down
      if ((g_channelOffset + 8) < kMidiChannelCount)
      {
        g_channelOffset += 8;
      }
      Serial.printf("go down=%u\n", g_channelOffset);
      break;
    case kLaunchpadTopRowControlNoteMin + 2:
      // Step scrolling left
      if (g_stepOffset > 0)
      {
        g_stepOffset -= 8;
      }
      Serial.printf("go left=%u\n", g_stepOffset);
      break;
    case kLaunchpadTopRowControlNoteMin + 3:
      // Step scrolling right
      if ((g_stepOffset + 8) < kStepCount)
      {
        g_stepOffset += 8;
      }
      Serial.printf("go right=%u\n", g_stepOffset);
      break;
    case kLaunchpadTopRowControlNoteMin + 4:
      if (g_tempoBpm > kMinTempoBpm)
      {
        g_tempoBpm -= 10;
      }
      Serial.printf("go g_tempoBpm=%u\n", g_tempoBpm);
      break;
    case kLaunchpadTopRowControlNoteMin + 5:
      if (g_tempoBpm < kMaxTempoBpm)
      {
        g_tempoBpm += 10;
      }
      Serial.printf("go g_tempoBpm=%u\n", g_tempoBpm);
      break;
    case kLaunchpadTopRowControlNoteMin + 6:
      g_microstepDivisions = (g_microstepDivisions >= kMicrostepMax) ? 1 : (g_microstepDivisions * 2);
      Serial.printf("go g_microstepDivisions=%u\n", g_microstepDivisions);
      break;
    case kLaunchpadTopRowControlNoteMin + 7: // Note 97 - play all channel sequence, note 98 is just light, no pad
      g_running = !g_running;
      if (g_running)
      {
        midiPort.sendStart();
        playAllChannelSequence();
      }
      else
      {
        midiPort.sendStop();
      }
      Serial.printf("go g_running=%u\n", g_running);
      break;
    case kLaunchpadRightColumnControlNoteMin + 0:
      g_running = false;
      midiPort.sendStop();
      break;
    case kLaunchpadRightColumnControlNoteMin + 10:
      g_running = true;
      midiPort.sendStart();
      break;
    case kLaunchpadRightColumnControlNoteMin + 20:
      memset(g_sequence, 0, sizeof(g_sequence));
      refreshLaunchpadGridLedState();
      break;
    case kLaunchpadRightColumnControlNoteMin + 30:
      g_stepIndex = 0;
      break;
    case kLaunchpadRightColumnControlNoteMin + 40:
      g_tempoBpm = 120;
      break;
    case kLaunchpadRightColumnControlNoteMin + 50:
      g_swingPct = 0;
      break;
    case kLaunchpadRightColumnControlNoteMin + 60:
      g_recording = !g_recording;
      break;
    case kLaunchpadRightColumnControlNoteMin + 70:
      g_overdub = !g_overdub;
      break;
    default:
      break;
    }

    refreshLaunchpadControlLedState();
  }

  void onLaunchpadNoteOn(byte channel, byte note, byte velocity)
  {
    g_seenLaunchpadInput = true;
    Serial.printf("Launchpad note on ch=%u note=%u vel=%u\n", channel + 1, note, velocity);
    stageLaunchpadPad(channel, note, velocity, true);
  }

  void onLaunchpadNoteOff(byte channel, byte note, byte velocity)
  {
    g_seenLaunchpadInput = true;
    Serial.printf("Launchpad note off ch=%u note=%u vel=%u\n", channel + 1, note, velocity);
    stageLaunchpadPad(channel, note, velocity, false);
  }

  void onLaunchpadControlChange(byte channel, byte control, byte value)
  {
    Serial.printf("Launchpad  ch=%u control=%u val=%u\n", channel, control, value);
    if (value && isLaunchpadControlNote(control))
    {
      // Check if this is a right-column control note for recording
      if (isLaunchpadRightColumnControlNote(control))
      {
        // Map specific control notes to MIDI channels
        // 89 no offset -> channel 1, 79 no offset -> channel 2, ..., 19 no offset -> channel 8, 89 + offset -> channel 9 ... 19 + offset -> channel 16
        // The control notes are: 89, 79, 69, 59, 49, 39, 29, 19 (every 10 notes)
        uint8_t channelNumber = (kLaunchpadRightColumnControlNoteMax - control) / 10 + 1;
        g_recordingChannelOffset = channelNumber - 1; // Zero-based indexing

        // Set recording state for this channel
        g_recordingHeldNote = true;

        Serial.printf("Recording on channel %u (offset %u)\n", channelNumber, g_recordingChannelOffset);
      }
      else
      {
        handleLaunchpadControl(control);
      }
    }
    else if (!value && isLaunchpadRightColumnControlNote(control) && g_recordingHeldNote)
    {
      // Stop recording when releasing the right-column control note
      g_recordingHeldNote = false;
      Serial.printf("Stopped recording\n");
    }
    (void)channel;
    (void)control;
    (void)value;
  }

  void handleHardwareMidiIn()
  {
    const auto type = midiPort.getType();
    const auto channel = midiPort.getChannel();
    const auto data1 = midiPort.getData1();
    const auto data2 = midiPort.getData2();

    Serial.printf(
        "HW MIDI IN type=%u ch=%u data1=%u data2=%u\n",
        type,
        channel,
        data1,
        data2);

    // MIDI library channel is 1..16.
    // Convert to our internal 0..15 representation.
    const uint8_t internalChannel =
        (channel >= 1 && channel <= 16) ? (channel - 1) : 0;

    if (g_recordingHeldNote)
    {
      // g_recordingChannelOffset is already 0..15.
      const uint8_t actualChannel = g_recordingChannelOffset;

      if (type == midi::NoteOn && data2 > 0)
      {
        Serial.printf(
            "REC NoteOn MIDI ch=%u note=%u vel=%u -> lane %u\n",
            channel,
            data1,
            data2,
            actualChannel);

        recordCurrentStep(actualChannel, data1, data2);
      }
      else if (type == midi::NoteOff ||
               (type == midi::NoteOn && data2 == 0))
      {
        Serial.printf(
            "REC NoteOff MIDI ch=%u note=%u -> lane %u\n",
            channel,
            data1,
            actualChannel);

        recordCurrentStep(actualChannel, data1, 0);
      }
    }
    else if (g_recording)
    {
      if (type == midi::NoteOn && data2 > 0)
      {
        Serial.printf(
            "REC NoteOn ch=%u note=%u vel=%u -> lane %u\n",
            channel,
            data1,
            data2,
            internalChannel);

        recordCurrentStep(internalChannel, data1, data2);
      }
      else if (type == midi::NoteOff ||
               (type == midi::NoteOn && data2 == 0))
      {
        Serial.printf(
            "REC NoteOff ch=%u note=%u -> lane %u\n",
            channel,
            data1,
            internalChannel);

        recordCurrentStep(internalChannel, data1, 0);
      }
    }

    // Debug
    if (type == midi::NoteOn && data2 > 0)
    {
      Serial.printf(
          "HW MIDI IN: NoteOn ch=%u note=%u vel=%u\n",
          channel,
          data1,
          data2);
    }
    else if (type == midi::NoteOff ||
             (type == midi::NoteOn && data2 == 0))
    {
      Serial.printf(
          "HW MIDI IN: NoteOff ch=%u note=%u\n",
          channel,
          data1);
    }
  }

  void sendActiveStepNotes(uint8_t step)
  {
    for (uint8_t channel = 0; channel < kMidiChannelCount; ++channel)
    {
      StepLaneState &lane = g_sequence[step][channel];
      if (!lane.active)
      {
        continue;
      }

      const uint16_t swingDelayUs = (step % 2 == 1 && g_swingPct > 0)
                                        ? (calculateStepDurationMs() * g_swingPct / 100U) * 1000U / 2U
                                        : 0;
      if (swingDelayUs > 0)
      {
        delayMicroseconds(swingDelayUs);
      }

      for (uint8_t ratchet = 0; ratchet < g_ratchetCount; ++ratchet)
      {
        sendMidiMessage(channel, lane.note, lane.velocity, true);
        delayMicroseconds(250);
        sendMidiMessage(channel, lane.note, 0, false);
        if (ratchet + 1 < g_ratchetCount)
        {
          delayMicroseconds(static_cast<uint32_t>(calculateStepDurationMs()) * 1000U / g_ratchetCount);
        }
      }
    }
  }

  void suppressLastStepNotes(uint8_t step)
  {
    for (uint8_t channel = 0; channel < kMidiChannelCount; ++channel)
    {
      StepLaneState &lane = g_sequence[step][channel];
      if (lane.active)
      {
        sendMidiMessage(channel, lane.note, 0, false);
      }
    }
  }

  void advanceSequencerStep()
  {
    if (g_hasPlayedStep)
    {
      suppressLastStepNotes(g_lastPlayedStep);
    }

    sendActiveStepNotes(g_stepIndex);
    g_lastPlayedStep = g_stepIndex;
    g_stepIndex = (g_stepIndex + 1) % kStepCount;
    g_hasPlayedStep = true;
  }

  void debugPrintState()
  {
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
} // namespace

void setup()
{
  pinMode(LED_BUILTIN, OUTPUT);
  digitalWriteFast(LED_BUILTIN, LOW);

  Serial.begin(115200);
  while (!Serial && millis() < 1000)
  {
    ;
  }

  Serial1.begin(31250);
  // Initialize hardware DIN MIDI.
  // MIDI library channels are 1..16.
  // OMNI makes MIDI.read() accept input from all channels.
  midiPort.begin(MIDI_CHANNEL_OMNI);

  launchpad.setHandleNoteOn(onLaunchpadNoteOn);
  launchpad.setHandleNoteOff(onLaunchpadNoteOff);
  launchpad.setHandleControlChange(onLaunchpadControlChange);

  myusb.begin();

  delay(500);
  sendLaunchpadProgramMode();
  delay(250);
  refreshLaunchpadControlLedState();
  refreshLaunchpadGridLedState();

  Serial.println("Launchpad USB host + 16-channel MIDI ready");
  Serial.println("Waiting for Launchpad X on the Teensy USB host port...");
}

void loop()
{
  myusb.Task();
  launchpad.read();

  // Hardware DIN MIDI IN on Serial1 RX pin 0.
  // while (midiPort.read())
  if (midiPort.read())
  {
    handleHardwareMidiIn();
  }

  // Test OUT
  // midiPort.sendNoteOn(60, 127, 1);
  // Serial.println("NOTE ON  ch=1 note=60 vel=127");
  // delay(500);
  // midiPort.sendNoteOff(60, 0, 1);
  // Serial.println("NOTE OFF ch=1 note=60");
  // delay(500);

  refreshLaunchpadControlLedState();

  if (g_running && g_clockPulseTimer >= calculateClockPulseMs())
  {
    g_clockPulseTimer = 0;
    sendStepClockPulse();
  }

  if (g_running && g_stepTimer >= calculateStepDurationMs())
  {
    g_stepTimer = 0;
    advanceSequencerStep();
    debugPrintState();
    refreshLaunchpadGridLedState();
  }

  if (g_ledTimer >= 250)
  {
    g_ledTimer = 0;
    digitalWriteFast(LED_BUILTIN, !digitalReadFast(LED_BUILTIN));
  }

  if (g_statusTimer >= 1000)
  {
    g_statusTimer = 0;

    if (!g_seenLaunchpadInput)
    {
      // Serial.println("USB host task running; no Launchpad input seen yet");
    }
  }
}
