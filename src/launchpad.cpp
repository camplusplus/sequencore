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

bool isNoteOnTheRightEdge(byte note)
{
  return (note - kLaunchpadRightColumnControlNoteMin) % 10 == 0;
}

bool isLaunchpadRightColumnControlNote(byte note)
{
  return note >= kLaunchpadRightColumnControlNoteMin &&
         note <= kLaunchpadRightColumnControlNoteMax &&
         isNoteOnTheRightEdge(note);
}

bool isLaunchpadControlNote(byte note)
{
  return isLaunchpadTopRowControlNote(note) ||
         isLaunchpadRightColumnControlNote(note);
}

// notes are 8x8 grid on top starting with 81-88,
// next rows 71-78, 61-68, 51-58, 41-48, 31-38, 21-28, 11-18
bool isLaunchpadGridPad(byte note)
{
  return note >= kLaunchpadGridNoteMin &&
         note <= kLaunchpadGridNoteMax &&
         note % 10 != 0 &&
         !isNoteOnTheRightEdge(note);
}

// -----------------------------------------------------------------------------
// Launchpad LEDs
// -----------------------------------------------------------------------------

namespace
{
  // The Launchpad note numbers that map to an LED.
  constexpr uint8_t kLedNoteMin = 11;
  constexpr uint8_t kLedNoteMax = 98;
  constexpr size_t kLedCacheSize =
      kLedNoteMax - kLedNoteMin + 1;

  // A color value is 0..18; 0xFF is a valid "not yet synced" sentinel.
  constexpr byte kLedColorUnsynced = 0xFF;

  // Last color actually sent to each LED. Re-sending an identical
  // SysEx command re-triggers the Launchpad LED driver, which makes
  // the pad visibly flicker. Caching lets refresh functions run at
  // any rate without USB traffic or flicker unless a color changes.
  byte g_launchpadLedCache[kLedCacheSize];

  void resetLaunchpadLedCache()
  {
    memset(
        g_launchpadLedCache,
        kLedColorUnsynced,
        sizeof(g_launchpadLedCache));
  }
}

void setLaunchpadLedColor(
    byte note,
    byte color)
{
  // Only Launchpad LED note numbers are valid targets.
  if (note < kLedNoteMin ||
      note > kLedNoteMax)
  {
    return;
  }

  // Skip LEDs whose color has not changed. The first refresh after
  // boot/program mode still sends every LED because the cache is
  // initialized to kLedColorUnsynced.
  const size_t index = note - kLedNoteMin;
  if (g_launchpadLedCache[index] == color)
  {
    return;
  }

  g_launchpadLedCache[index] = color;

  static const uint8_t kLaunchpadSysexHeader[] =
      {
          0xF0,
          0x00,
          0x20,
          0x29,
          0x02,
          0x0C};

  uint8_t data[sizeof(kLaunchpadSysexHeader) + 4];

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

  // Program mode resets the Launchpad display; force a full LED
  // re-sync on the next refresh so the cache never lies about the
  // hardware state.
  resetLaunchpadLedCache();
}

static bool isChannelRecorded(uint8_t channel)
{
  if (channel >= kMidiChannelCount)
  {
    return false;
  }

  for (uint8_t step = 0;
       step < g_sequenceLength;
       ++step)
  {
    if (g_sequence[step][channel].active)
    {
      return true;
    }
  }

  return false;
}

void refreshLaunchpadGridLedState()
{
  for (uint8_t note = kLaunchpadGridNoteMin;
       note <= kLaunchpadGridNoteMax;
       ++note)
  {
    if (!isLaunchpadGridPad(note))
    {
      continue;
    }

    // Grid pads show the sequence step for the visible column.
    // Steps with a recorded note light up white; empty steps stay off.
    // Columns beyond the current sequence length stay off.
    const uint8_t row = 8 - (note / 10);
    const uint8_t col = (note % 10) - 1;

    if ((uint16_t)g_stepOffset + col >= g_sequenceLength)
    {
      setLaunchpadLedColor(note, kLaunchpadColorOff);
      continue;
    }

    const uint8_t step =
        g_stepOffset + col;

    const uint8_t laneChannel =
        (row + g_channelOffset) % kMidiChannelCount;

    byte color;

    // Priority: step mute (whole column) > cell mute > active > off.
    if (g_stepMuted[step])
    {
      // Whole column muted (long-press in green mode).
      color = kLaunchpadColorGreenLow;
    }
    else if (g_sequence[step][laneChannel].muted)
    {
      // Individual cell muted (short-press in green mode).
      color = kLaunchpadColorGreenLow;
    }
    else if (g_sequence[step][laneChannel].active)
    {
      color = kLaunchpadColorWhiteHigh;
    }
    else
    {
      color = kLaunchpadColorOff;
    }

    setLaunchpadLedColor(note, color);
  }

  // Channel scrolling.
  // Pad 91 doubles as the shuffle-up pad in green modifier mode, so
  // its LED shows the shuffle state of the last pressed channel.
  setLaunchpadLedColor(
      kLaunchpadTopRowControlNoteMin + 0,
      g_modifierMode == 1
          ? (g_channelShuffle[g_lastPressedChannel] > 0
                 ? kLaunchpadColorAmberHigh
                 : kLaunchpadColorGreenLow)
          : (g_channelOffset > 0
                 ? kLaunchpadColorWhiteLow
                 : kLaunchpadColorOff));

  setLaunchpadLedColor(
      kLaunchpadTopRowControlNoteMin + 1,
      g_modifierMode == 1
          ? (g_channelShuffle[g_lastPressedChannel] >= kShuffleMax
                 ? kLaunchpadColorAmberHigh
                 : kLaunchpadColorGreenLow)
          : ((g_channelOffset + 8) < kMidiChannelCount
                 ? kLaunchpadColorWhiteLow
                 : kLaunchpadColorOff));

  // Step scrolling / sequence length (green modifier mode).
  setLaunchpadLedColor(
      kLaunchpadTopRowControlNoteMin + 2,
      g_modifierMode == 1
          ? (g_sequenceLength <= kMinSequenceLength
                 ? kLaunchpadColorAmberHigh
                 : kLaunchpadColorGreenLow)
          : (g_stepOffset > 0
                 ? kLaunchpadColorWhiteLow
                 : kLaunchpadColorOff));

  setLaunchpadLedColor(
      kLaunchpadTopRowControlNoteMin + 3,
      g_modifierMode == 1
          ? (g_sequenceLength >= kMaxSequenceLength
                 ? kLaunchpadColorAmberHigh
                 : kLaunchpadColorGreenLow)
          : ((g_stepOffset + 8) < g_sequenceLength
                 ? kLaunchpadColorWhiteLow
                 : kLaunchpadColorOff));
}

void refreshLaunchpadControlLedState()
{
  // Every pad gets its final color written directly (no off pre-clear),
  // the same way the grid refresh works. Pre-clearing would make each lit
  // pad receive an off/on SysEx pair on every refresh, which re-triggers
  // the Launchpad LED driver and makes the pad visibly blink.

  // Channel scrolling / shuffle (green modifier mode).
  setLaunchpadLedColor(
      kLaunchpadTopRowControlNoteMin + 0,
      g_modifierMode == 1
          ? (g_channelShuffle[g_lastPressedChannel] > 0
                 ? kLaunchpadColorAmberHigh
                 : kLaunchpadColorGreenLow)
          : (g_channelOffset > 0
                 ? kLaunchpadColorWhiteLow
                 : kLaunchpadColorOff));

  setLaunchpadLedColor(
      kLaunchpadTopRowControlNoteMin + 1,
      g_modifierMode == 1
          ? (g_channelShuffle[g_lastPressedChannel] >= kShuffleMax
                 ? kLaunchpadColorAmberHigh
                 : kLaunchpadColorGreenLow)
          : ((g_channelOffset + 8) < kMidiChannelCount
                 ? kLaunchpadColorWhiteLow
                 : kLaunchpadColorOff));

  // Step scrolling / sequence length (green modifier mode).
  setLaunchpadLedColor(
      kLaunchpadTopRowControlNoteMin + 2,
      g_modifierMode == 1
          ? (g_sequenceLength <= kMinSequenceLength
                 ? kLaunchpadColorAmberHigh
                 : kLaunchpadColorGreenLow)
          : (g_stepOffset > 0
                 ? kLaunchpadColorWhiteLow
                 : kLaunchpadColorOff));

  setLaunchpadLedColor(
      kLaunchpadTopRowControlNoteMin + 3,
      g_modifierMode == 1
          ? (g_sequenceLength >= kMaxSequenceLength
                 ? kLaunchpadColorAmberHigh
                 : kLaunchpadColorGreenLow)
          : ((g_stepOffset + 8) < g_sequenceLength
                 ? kLaunchpadColorWhiteLow
                 : kLaunchpadColorOff));

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

  // Modifier (Pad 97): 0 = off, 1 = green (mute), 2 = red (delete).
  setLaunchpadLedColor(
      kLaunchpadTopRowControlNoteMin + 6,
      g_modifierMode == 1
          ? kLaunchpadColorGreenHigh
          : (g_modifierMode == 2
                 ? kLaunchpadColorRedHigh
                 : kLaunchpadColorOff));

  // Run/stop.
  setLaunchpadLedColor(
      kLaunchpadTopRowControlNoteMin + 7,
      g_running
          ? kLaunchpadColorWhiteHigh
          : kLaunchpadColorRedHigh);

  // Right column: channel control pads.
  //   green  = muted
  //   yellow = has recorded steps
  //   white  = empty
  for (uint8_t i = 0; i < 8; ++i)
  {
    const byte note =
        kLaunchpadRightColumnControlNoteMin + i * 10;

    // 89 -> channel 1, 79 -> channel 2, ... 19 -> channel 8
    const uint8_t channelNumber =
        (kLaunchpadRightColumnControlNoteMax -
         note) /
            10 +
        1;

    const uint8_t internalChannel =
        (channelNumber - 1) + g_channelOffset;

    byte color;
    if (g_channelMuteMask & (1U << internalChannel))
    {
      color = kLaunchpadColorGreenHigh;
    }
    else if (internalChannel < kMidiChannelCount &&
             isChannelRecorded(internalChannel))
    {
      color = kLaunchpadColorYellowHigh;
    }
    else
    {
      color = kLaunchpadColorWhiteHigh;
    }

    setLaunchpadLedColor(note, color);
  }
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
  (void)velocity;

  if (!isLaunchpadGridPad(note))
  {
    return;
  }

  // Map the pad note to its grid row/column (must match the LED refresh).
  //   81..88 -> row 0, 71..78 -> row 1, ... 11..18 -> row 7
  const uint8_t row = 8 - (note / 10);
  const uint8_t col = (note % 10) - 1;

  const uint16_t visibleStep =
      (uint16_t)g_stepOffset + col;

  const uint8_t step =
      (visibleStep >= g_sequenceLength)
          ? 0
          : (uint8_t)visibleStep;

  const uint8_t laneChannel =
      (row + g_channelOffset) % kMidiChannelCount;

  StepLaneState &lane =
      g_sequence[step][laneChannel];

  // Modifier mode 1 (green, pad 97):
  //   short touch = mute/unmute just this cell (channel at that step)
  //   long hold   = mute/unmute the whole step/column (handled in loop)
  if (g_modifierMode == 1)
  {
    if (active)
    {
      // Pads in columns beyond the sequence length do nothing.
      if (visibleStep >= g_sequenceLength)
      {
        return;
      }

      // Start tracking the hold. The release below decides short vs long.
      g_gridHoldStartMs = millis();
      g_gridHoldActive = true;
      g_gridHoldTriggered = false;
      g_gridHoldStep = step;
      g_gridHoldChannel = laneChannel;
    }
    else
    {
      // Release: if the hold was not long enough, toggle this cell's mute.
      g_gridHoldActive = false;

      if (!g_gridHoldTriggered &&
          g_gridHoldStep < g_sequenceLength)
      {
        StepLaneState &heldLane =
            g_sequence[g_gridHoldStep][g_gridHoldChannel];

        heldLane.muted = !heldLane.muted;
        refreshLaunchpadGridLedState();
      }
    }

    return;
  }

  // Only a pad touch (NoteOn) acts on the grid. Release does nothing.
  if (!active)
  {
    return;
  }

  // Pads in columns beyond the sequence length are not actionable.
  if (visibleStep >= g_sequenceLength)
  {
    return;
  }

  // Modifier mode 2 (red, pad 97): touching a grid pad deletes the
  // recorded note for that step/lane.
  if (g_modifierMode == 2)
  {
    lane.active = false;
    lane.muted = false;

    refreshLaunchpadGridLedState();
    return;
  }

  // While holding the record button, touching a grid pad records
  // the last hardware MIDI keyboard note onto that step.
  if (g_recordingHeldNote)
  {
    lane.active = true;
    lane.muted = false;
    lane.note = g_lastHwNote;
    lane.velocity = g_lastHwVelocity;

    refreshLaunchpadGridLedState();
  }
}

// -----------------------------------------------------------------------------
// Launchpad tempo
// -----------------------------------------------------------------------------

void adjustLaunchpadTempo(int16_t delta)
{
  const int16_t newTempo =
      static_cast<int16_t>(g_tempoBpm) + delta;

  if (newTempo < static_cast<int16_t>(kMinTempoBpm))
  {
    g_tempoBpm = kMinTempoBpm;
  }
  else if (newTempo > static_cast<int16_t>(kMaxTempoBpm))
  {
    g_tempoBpm = kMaxTempoBpm;
  }
  else
  {
    g_tempoBpm = static_cast<uint16_t>(newTempo);
  }
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
    if ((g_stepOffset + 8) < g_sequenceLength)
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
      g_tempoBpm -= 1;
    }
    break;

  case kLaunchpadTopRowControlNoteMin + 5:
    if (g_tempoBpm < kMaxTempoBpm)
    {
      g_tempoBpm += 1;
    }
    break;

    // ---------------------------------------------------------------------------
    // Microsteps
    // ---------------------------------------------------------------------------

    // case kLaunchpadTopRowControlNoteMin + 6:
    //   g_microstepDivisions =
    //       (g_microstepDivisions >= kMicrostepMax)
    //           ? 1
    //           : (g_microstepDivisions * 2);
    //   break;

    // ---------------------------------------------------------------------------
    // Play / stop
    // ---------------------------------------------------------------------------

  case kLaunchpadTopRowControlNoteMin + 7:
    g_running = !g_running;

    if (g_running)
    {
      // Resume cleanly: reset the step/clock timers so the next step
      // begins on a fresh boundary. Playback then keeps looping
      // forever until explicitly paused again.
      g_stepTimer = 0;
      g_clockPulseTimer = 0;

      midiOutSendStart();
    }
    else
    {
      // Pause: stays stopped until played again.
      midiOutSendStop();
    }
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
  // RIGHT COLUMN
  //   Modifier mode 1 (green) = mute channel
  //   Modifier mode 2 (red)   = delete channel
  //   Modifier mode 0 (none)  = select recording channel (hold)
  // ---------------------------------------------------------------------------

  if (isLaunchpadRightColumnControlNote(control))
  {
    // 89 -> channel 1
    // 79 -> channel 2
    // 69 -> channel 3
    // ...
    // 19 -> channel 8
    const uint8_t channelNumber =
        (kLaunchpadRightColumnControlNoteMax -
         control) /
            10 +
        1;

    const uint8_t internalChannel =
        (channelNumber - 1) + g_channelOffset;

    // Track the last pressed right-column channel pad so the green-mode
    // shuffle pads (91/92) act on that channel.
    if (value != 0 && internalChannel < kMidiChannelCount)
    {
      g_lastPressedChannel = internalChannel;
    }

    // ---------------------------------------------------------------------------
    // MODIFIER MODE: mute / delete
    // ---------------------------------------------------------------------------

    if (g_modifierMode > 0)
    {
      // Act only on press so mute is a persistent toggle, not momentary.
      // (Right-column pads send a CC on press and another on release.)
      if (value != 0)
      {
        if (internalChannel < kMidiChannelCount)
        {
          if (g_modifierMode == 1)
          {
            // Toggle mute for this channel.
            g_channelMuteMask ^= (1U << internalChannel);
          }
          else if (g_modifierMode == 2)
          {
            // Delete the entire channel lane.
            deleteChannel(internalChannel);
          }
        }

        refreshLaunchpadControlLedState();
        refreshLaunchpadGridLedState();
      }
      return;
    }

    // ---------------------------------------------------------------------------
    // NONE MODE: select recording channel (hold)
    // ---------------------------------------------------------------------------

    if (value > 0)
    {
      g_recordingChannelOffset =
          channelNumber - 1;

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

        midiOutSendStart();
      }

      refreshLaunchpadControlLedState();
      return;
    }
  }

  // ---------------------------------------------------------------------------
  // TOP ROW MODIFIER BUTTON (PAD 97)
  // ---------------------------------------------------------------------------

  if (isLaunchpadTopRowControlNote(control))
  {
    // Pad 97 (note 91+6) is the modifier: a 3-state toggle that cycles
    // on each press. 0 = none (off), 1 = green (mute), 2 = red (delete).
    // While set, the right column pads act on the current channel.
    if (control == kLaunchpadTopRowControlNoteMin + 6)
    {
      if (value != 0)
      {
        g_modifierMode = (g_modifierMode + 1) % 3;
      }

      refreshLaunchpadControlLedState();
    }
    else if (g_modifierMode == 1 &&
             (control == kLaunchpadTopRowControlNoteMin + 0 ||
              control == kLaunchpadTopRowControlNoteMin + 1 ||
              control == kLaunchpadTopRowControlNoteMin + 2 ||
              control == kLaunchpadTopRowControlNoteMin + 3) &&
             value != 0)
    {
      if (control == kLaunchpadTopRowControlNoteMin + 0)
      {
        // Green modifier mode: pad 91 = shuffle up
        // for the last pressed channel.
        adjustChannelShuffle(g_lastPressedChannel, 1);
      }
      else if (control == kLaunchpadTopRowControlNoteMin + 1)
      {
        // Green modifier mode: pad 92 = shuffle down
        // for the last pressed channel.
        adjustChannelShuffle(g_lastPressedChannel, -1);
      }
      else if (control == kLaunchpadTopRowControlNoteMin + 2)
      {
        // Green modifier mode: pad 93 = sequence length 8 steps down.
        adjustSequenceLength(-8);
      }
      else
      {
        // Green modifier mode: pad 94 = sequence length 8 steps up.
        adjustSequenceLength(8);
      }

      refreshLaunchpadControlLedState();
      refreshLaunchpadGridLedState();
    }
    else if (value != 0)
    {
      handleLaunchpadControl(control);
    }
    return;
  }

  // ---------------------------------------------------------------------------
  // OTHER CONTROL BUTTONS
  // ---------------------------------------------------------------------------

  if (value)
  {
    handleLaunchpadControl(control);
  }
}