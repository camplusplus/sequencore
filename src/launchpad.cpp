#include "launchpad.h"

#include "app_state.h"
#include "sequencer.h"

#include <string.h>

// -----------------------------------------------------------------------------
// Launchpad note classification
// -----------------------------------------------------------------------------

bool isLaunchpadTopRowControlNote(byte note)
{
  return note >= kLaunchpadTopRowControlNoteMin &&
         note <= kLaunchpadTopRowControlNoteMax;
}

bool isLaunchpadRightColumnControlNote(byte note)
{
  return note >= kLaunchpadRightColumnControlNoteMin &&
         note <= kLaunchpadRightColumnControlNoteMax &&
         ((note - kLaunchpadRightColumnControlNoteMin) % 10 == 0);
}

bool isLaunchpadControlNote(byte note)
{
  return isLaunchpadTopRowControlNote(note) ||
         isLaunchpadRightColumnControlNote(note);
}

bool isLaunchpadGridPad(byte note)
{
  return note >= kLaunchpadGridNoteMin &&
         note <= kLaunchpadGridNoteMax &&
         !isLaunchpadControlNote(note);
}

// -----------------------------------------------------------------------------
// Launchpad LEDs
// -----------------------------------------------------------------------------

void setLaunchpadLedColor(
    byte note,
    byte color)
{
  static const uint8_t kLaunchpadSysexHeader[] =
      {
          0xF0,
          0x00,
          0x20,
          0x29,
          0x02,
          0x0C};

  uint8_t data[
      sizeof(kLaunchpadSysexHeader) + 4];

  memcpy(
      data,
      kLaunchpadSysexHeader,
      sizeof(kLaunchpadSysexHeader));

  data[sizeof(kLaunchpadSysexHeader)] = 0x03;
  data[sizeof(kLaunchpadSysexHeader) + 1] = 0x00;
  data[sizeof(kLaunchpadSysexHeader) + 2] = note;
  data[sizeof(kLaunchpadSysexHeader) + 3] = color;

  launchpad.sendSysEx(
      sizeof(kLaunchpadSysexHeader) + 4,
      data,
      false);
}

void sendLaunchpadProgramMode()
{
  static const uint8_t kProgramModeSysex[] =
      {
          0xF0,
          0x00,
          0x20,
          0x29,
          0x02,
          0x0C,
          0x0E,
          0x01,
          0xF7};

  launchpad.sendSysEx(
      sizeof(kProgramModeSysex),
      kProgramModeSysex,
      true);
}

void refreshLaunchpadGridLedState()
{
  for (uint8_t note = 0;
       note < kLaunchpadRightColumnControlNoteMax;
       ++note)
  {
    if (!isLaunchpadControlNote(note))
    {
      setLaunchpadLedColor(
          note,
          kLaunchpadColorWhiteHigh);
    }
  }

  // Channel scrolling.
  setLaunchpadLedColor(
      kLaunchpadTopRowControlNoteMin + 0,
      g_channelOffset > 0
          ? kLaunchpadColorWhiteLow
          : kLaunchpadColorOff);

  setLaunchpadLedColor(
      kLaunchpadTopRowControlNoteMin + 1,
      (g_channelOffset + 8) < kMidiChannelCount
          ? kLaunchpadColorWhiteLow
          : kLaunchpadColorOff);

  // Step scrolling.
  setLaunchpadLedColor(
      kLaunchpadTopRowControlNoteMin + 2,
      g_stepOffset > 0
          ? kLaunchpadColorWhiteLow
          : kLaunchpadColorOff);

  setLaunchpadLedColor(
      kLaunchpadTopRowControlNoteMin + 3,
      (g_stepOffset + 8) < kStepCount
          ? kLaunchpadColorWhiteLow
          : kLaunchpadColorOff);
}

void refreshLaunchpadControlLedState()
{
  // Turn off all top-row control LEDs.
  for (uint8_t i = 0; i < 8; ++i)
  {
    setLaunchpadLedColor(
        kLaunchpadTopRowControlNoteMin + i,
        kLaunchpadColorOff);
  }

  // Turn off all right-column LEDs.
  for (uint8_t i = 0; i < 80; i += 10)
  {
    setLaunchpadLedColor(
        kLaunchpadRightColumnControlNoteMin + i,
        kLaunchpadColorOff);
  }

  // Channel scrolling.
  setLaunchpadLedColor(
      kLaunchpadTopRowControlNoteMin + 0,
      g_channelOffset > 0
          ? kLaunchpadColorWhiteLow
          : kLaunchpadColorOff);

  setLaunchpadLedColor(
      kLaunchpadTopRowControlNoteMin + 1,
      (g_channelOffset + 8) < kMidiChannelCount
          ? kLaunchpadColorWhiteLow
          : kLaunchpadColorOff);

  // Step scrolling.
  setLaunchpadLedColor(
      kLaunchpadTopRowControlNoteMin + 2,
      g_stepOffset > 0
          ? kLaunchpadColorWhiteLow
          : kLaunchpadColorOff);

  setLaunchpadLedColor(
      kLaunchpadTopRowControlNoteMin + 3,
      (g_stepOffset + 8) < kStepCount
          ? kLaunchpadColorWhiteLow
          : kLaunchpadColorOff);

  // Tempo.
  setLaunchpadLedColor(
      kLaunchpadTopRowControlNoteMin + 4,
      g_tempoBpm <= kMinTempoBpm
          ? kLaunchpadColorAmberLow
          : kLaunchpadColorWhiteLow);

  setLaunchpadLedColor(
      kLaunchpadTopRowControlNoteMin + 5,
      g_tempoBpm >= kMaxTempoBpm
          ? kLaunchpadColorAmberLow
          : kLaunchpadColorWhiteLow);

  // Microstep.
  setLaunchpadLedColor(
      kLaunchpadTopRowControlNoteMin + 6,
      g_microstepDivisions > 1
          ? kLaunchpadColorAmberHigh
          : kLaunchpadColorOff);

  // Run/stop.
  setLaunchpadLedColor(
      kLaunchpadTopRowControlNoteMin + 7,
      g_running
          ? kLaunchpadColorWhiteHigh
          : kLaunchpadColorRedHigh);

  // Right column.
  setLaunchpadLedColor(
      kLaunchpadRightColumnControlNoteMin + 0,
      kLaunchpadColorRedHigh);

  setLaunchpadLedColor(
      kLaunchpadRightColumnControlNoteMin + 10,
      kLaunchpadColorWhiteHigh);

  setLaunchpadLedColor(
      kLaunchpadRightColumnControlNoteMin + 20,
      kLaunchpadColorBlueLow);

  setLaunchpadLedColor(
      kLaunchpadRightColumnControlNoteMin + 30,
      kLaunchpadColorBlueLow);

  setLaunchpadLedColor(
      kLaunchpadRightColumnControlNoteMin + 40,
      kLaunchpadColorAmberLow);

  setLaunchpadLedColor(
      kLaunchpadRightColumnControlNoteMin + 50,
      kLaunchpadColorAmberLow);

  setLaunchpadLedColor(
      kLaunchpadRightColumnControlNoteMin + 60,
      g_recording
          ? kLaunchpadColorRedHigh
          : kLaunchpadColorOff);

  setLaunchpadLedColor(
      kLaunchpadRightColumnControlNoteMin + 70,
      g_overdub
          ? kLaunchpadColorBlueLow
          : kLaunchpadColorOff);
}

// -----------------------------------------------------------------------------
// Launchpad pad handling
// -----------------------------------------------------------------------------

void stageLaunchpadPad(
    byte channel,
    byte note,
    byte velocity,
    bool active)
{
  (void)channel;

  if (!isLaunchpadGridPad(note))
  {
    return;
  }

  const uint8_t row = note / 8;
  const uint8_t col = note % 8;

  const uint8_t step =
      (col + g_stepOffset) % kStepCount;

  const uint8_t laneChannel =
      (row + g_channelOffset) % kMidiChannelCount;

  StepLaneState &lane =
      g_sequence[step][laneChannel];

  lane.active = active;
  lane.note = note + 48;
  lane.velocity = velocity;

  refreshLaunchpadGridLedState();
}

// -----------------------------------------------------------------------------
// Launchpad controls
// -----------------------------------------------------------------------------

void handleLaunchpadControl(byte note)
{
  switch (note)
  {
  // ---------------------------------------------------------------------------
  // Channel scrolling
  // ---------------------------------------------------------------------------

  case kLaunchpadTopRowControlNoteMin + 0:
    if (g_channelOffset > 0)
    {
      g_channelOffset = 0;
    }
    break;

  case kLaunchpadTopRowControlNoteMin + 1:
    if (g_channelOffset == 0)
    {
      g_channelOffset = 8;
    }
    break;

  // ---------------------------------------------------------------------------
  // Step scrolling
  // ---------------------------------------------------------------------------

  case kLaunchpadTopRowControlNoteMin + 2:
    if (g_stepOffset > 0)
    {
      g_stepOffset -= 8;
    }
    break;

  case kLaunchpadTopRowControlNoteMin + 3:
    if ((g_stepOffset + 8) < kStepCount)
    {
      g_stepOffset += 8;
    }
    break;

  // ---------------------------------------------------------------------------
  // Tempo
  // ---------------------------------------------------------------------------

  case kLaunchpadTopRowControlNoteMin + 4:
    if (g_tempoBpm > kMinTempoBpm)
    {
      g_tempoBpm -= 10;
    }
    break;

  case kLaunchpadTopRowControlNoteMin + 5:
    if (g_tempoBpm < kMaxTempoBpm)
    {
      g_tempoBpm += 10;
    }
    break;

  // ---------------------------------------------------------------------------
  // Microsteps
  // ---------------------------------------------------------------------------

  case kLaunchpadTopRowControlNoteMin + 6:
    g_microstepDivisions =
        (g_microstepDivisions >= kMicrostepMax)
            ? 1
            : (g_microstepDivisions * 2);
    break;

  // ---------------------------------------------------------------------------
  // Play / stop
  // ---------------------------------------------------------------------------

  case kLaunchpadTopRowControlNoteMin + 7:
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
    break;

  // ---------------------------------------------------------------------------
  // Right-column controls
  // ---------------------------------------------------------------------------

  case kLaunchpadRightColumnControlNoteMin + 0:
    g_running = false;
    midiPort.sendStop();
    break;

  case kLaunchpadRightColumnControlNoteMin + 10:
    g_running = true;
    midiPort.sendStart();
    break;

  case kLaunchpadRightColumnControlNoteMin + 20:
    memset(
        g_sequence,
        0,
        sizeof(g_sequence));

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

// -----------------------------------------------------------------------------
// USB MIDI callbacks
// -----------------------------------------------------------------------------

void onLaunchpadNoteOn(
    byte channel,
    byte note,
    byte velocity)
{
  g_seenLaunchpadInput = true;

  stageLaunchpadPad(
      channel,
      note,
      velocity,
      true);
}

void onLaunchpadNoteOff(
    byte channel,
    byte note,
    byte velocity)
{
  g_seenLaunchpadInput = true;

  stageLaunchpadPad(
      channel,
      note,
      velocity,
      false);
}

void onLaunchpadControlChange(
    byte channel,
    byte control,
    byte value)
{
  (void)channel;

  // ---------------------------------------------------------------------------
  // RIGHT COLUMN = SELECT MIDI RECORDING CHANNEL
  // ---------------------------------------------------------------------------

  if (isLaunchpadRightColumnControlNote(control))
  {
    if (value > 0)
    {
      // 89 -> channel 1
      // 79 -> channel 2
      // 69 -> channel 3
      // ...
      // 19 -> channel 8

      uint8_t channelNumber =
          (kLaunchpadRightColumnControlNoteMax -
           control) /
              10 +
          1;

      g_recordingChannelOffset =
          channelNumber - 1;

      channelNumber += g_channelOffset;

      // Holding this button means RECORD.
      g_recordingHeldNote = true;
      g_recording = true;

      refreshLaunchpadControlLedState();
      return;
    }

    // -------------------------------------------------------------------------
    // RELEASE RECORD CHANNEL BUTTON
    // -------------------------------------------------------------------------

    if (value == 0 && g_recordingHeldNote)
    {
      g_recordingHeldNote = false;
      g_recording = false;

      // Make sure sequencer is running after recording.
      if (!g_running)
      {
        g_running = true;
        g_stepTimer = 0;
        g_clockPulseTimer = 0;

        midiPort.sendStart();
      }

      refreshLaunchpadControlLedState();
      return;
    }
  }

  // ---------------------------------------------------------------------------
  // OTHER CONTROL BUTTONS
  // ---------------------------------------------------------------------------

  if (value)
  {
    handleLaunchpadControl(control);
  }
}