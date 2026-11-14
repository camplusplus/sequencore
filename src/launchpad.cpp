#include "launchpad.h"

#include "app_state.h"
#include "sd_store.h"
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

// -----------------------------------------------------------------------------
// Launchpad note mode (Live / Note Layout / Scale Mode)
// -----------------------------------------------------------------------------

// Send one single-parameter Launchpad X command:
//   F0 00 20 29 02 0C <command> <param> F7
// Same shape as sendLaunchpadProgramMode() (0E 01), which the device
// already accepts from this sketch.
static void sendLaunchpadModeCommand(
    byte command,
    byte param)
{
  const uint8_t sysex[] =
      {
          0xF0,
          0x00,
          0x20,
          0x29,
          0x02,
          0x0C,
          command,
          param,
          0xF7};

  launchpad.sendSysEx(
      sizeof(sysex),
      sysex,
      true);
}

// 0E 00 = Live Mode. This is what re-enables the device's native note
// behaviour; 0E 01 (Programmer Mode) is what sendLaunchpadProgramMode()
// sends, and it is what suppresses NoteOn from the grid pads.
void sendLaunchpadLiveMode()
{
  sendLaunchpadModeCommand(
      0x0E,
      0x00);
}

// 00 01 = Note Layout: the grid pads are laid out as MIDI pitches.
void sendLaunchpadNoteLayout()
{
  sendLaunchpadModeCommand(
      0x00,
      0x01);
}

// 15 01 = Scale Mode on: the note layout snaps to a scale.
void sendLaunchpadScaleMode()
{
  sendLaunchpadModeCommand(
      0x15,
      0x01);
}

void enterLaunchpadNoteMode()
{
  g_launchpadNoteMode = true;
  g_launchpadRecordStep = 0;

  // Note mode is no longer a modifier state, so the cycle colours stay off
  // while it runs; leaving note mode lands on 0 the same way.
  g_modifierMode = 0;

  // The gesture (hold the play/pause pad) always starts from a clean slate.
  g_noteModeHoldActive = false;
  g_noteModeHoldTriggered = false;

  sendLaunchpadLiveMode();
  sendLaunchpadNoteLayout();
  sendLaunchpadScaleMode();

  // The device rewrites its own display when it leaves Programmer Mode,
  // so drop every cached LED color and let the next refresh re-send them.
  resetLaunchpadLedCache();
}

void exitLaunchpadNoteMode()
{
  g_launchpadNoteMode = false;

  // Leaving note mode always lands back on the "no modifier" state; the
  // gesture that leaves it is the held play/pause pad (98).
  g_modifierMode = 0;
  g_launchpadRecordStep = 0;

  // Leaving note mode closes a paused echo session: the pads sounded live
  // over a stopped transport, so put the outputs back into the stopped
  // state the pause established instead of leaving a dangling START.
  if (g_launchpadEchoOpen)
  {
    // The exit fires while the play/pause pad is still held down, so a grid
    // pad that was left held has no release coming to the note-mode echo
    // path - release whatever is still sounding so the instrument has no
    // stuck key.
    if (g_launchpadEchoNote != 0)
    {
      const uint8_t laneChannel =
          (uint8_t)((g_recordingChannelOffset + g_channelOffset) %
                    kMidiChannelCount);

      midiOutSendNoteOff(
          g_launchpadEchoNote,
          0,
          (byte)(laneChannel + 1));

      // Same for the pressure the touch left behind.
      midiOutSendPolyAfterTouch(
          g_launchpadEchoNote,
          0,
          (byte)(laneChannel + 1));

      if (g_launchpadEchoPressure != 0)
      {
        midiOutSendAfterTouch(
            0,
            (byte)(laneChannel + 1));

        g_launchpadEchoPressure = 0;
      }

      g_launchpadEchoNote = 0;
    }

    if (!g_running)
    {
      midiOutSendStop();
    }

    g_launchpadEchoOpen = false;
  }

  sendLaunchpadProgramMode();
}

// Cycle the pad 97 modifier mode and enter/leave launchpad note mode
// accordingly. Reached only from the Control Change path (Programmer Mode)
// now: in Live Mode Novation's own firmware owns the top-row pads, so they
// never arrive as NoteOn here.
static void cycleLaunchpadModifierMode()
{
  // Note mode is not part of the cycle any more - it is entered and left by
  // holding the play/pause pad (98), and pad 97 stops reporting in Live Mode
  // anyway - so the cycle only ever walks the four modifier colours.
  g_modifierMode = (g_modifierMode + 1) % 5;
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
    if (g_sequence[step][channel].isSubstepActive())
    {
      return true;
    }
  }

  return false;
}

void refreshLaunchpadGridLedState()
{
  // Note mode: the Launchpad owns the grid display itself (its Note Layout
  // colors each pad with the pitch it plays), so do not fight it by writing
  // sequence-state colors over it. Control-row LEDs still refresh.
  if (g_launchpadNoteMode)
  {
    return;
  }

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

    // Microstep edit mode: the edited row temporarily shows the 8
    // substep slots of g_microstepEditStep for that lane (col k -> slot k).
    // Slot 0 is the main note (white); higher slots are extra substeps
    // (yellow); empty slots stay off.
    if (g_microstepEditing &&
        row == g_microstepEditChannel)
    {
      byte color;
      if (g_sequence[g_microstepEditStep][laneChannel].substep[col].active)
      {
        color = (col == 0)
                    ? kLaunchpadColorWhiteHigh
                    : kLaunchpadColorYellowHigh;
      }
      else
      {
        color = kLaunchpadColorOff;
      }

      setLaunchpadLedColor(note, color);
      continue;
    }

    // Chord edit mode: the selected row shows the base note plus any selected
    // upper notes. The base note remains in slot 0; each column adds a higher
    // note, so the row acts as an 8-note chord picker.
    if (g_chordEditing &&
        row == g_chordEditChannel)
    {
      StepLaneState &editLane =
          g_sequence[g_chordEditStep][g_chordEditChannel];
      byte color = kLaunchpadColorOff;

      // Page 0 is the base note + notes 1..7 above it. Pages 1..3 add
      // the remaining +8..+31 notes above the base.
      if (g_chordEditPage == 0)
      {
        if (col == 0 && editLane.substep[0].active)
        {
          color = kLaunchpadColorWhiteHigh;
        }
        else if (col > 0 &&
                 editLane.isChordNoteActive(col - 1))
        {
          color = kLaunchpadColorBlueLow;
        }
      }
      else
      {
        const uint8_t slot =
            g_chordEditPage * kMicrostepMax - 1 + col;
        if (slot < kChordNoteMax &&
            editLane.isChordNoteActive(slot))
        {
          color = kLaunchpadColorBlueLow;
        }
      }

      setLaunchpadLedColor(note, color);
      continue;
    }

    byte color;

    // Priority: step mute (whole column) > cell mute > chord > microsteps
    // > ordinary step > off.
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
    else if (g_sequence[step][laneChannel].hasChordNotes())
    {
      color = kLaunchpadColorBlueLow;
    }
    else if (g_sequence[step][laneChannel].substep[1].active ||
             g_sequence[step][laneChannel].substep[2].active ||
             g_sequence[step][laneChannel].substep[3].active ||
             g_sequence[step][laneChannel].substep[4].active ||
             g_sequence[step][laneChannel].substep[5].active ||
             g_sequence[step][laneChannel].substep[6].active ||
             g_sequence[step][laneChannel].substep[7].active)
    {
      color = kLaunchpadColorYellowHigh;
    }
    else if (g_sequence[step][laneChannel].isSubstepActive())
    {
      color = kLaunchpadColorWhiteHigh;
    }
    else
    {
      color = kLaunchpadColorOff;
    }

    setLaunchpadLedColor(note, color);
  }

  // Channel scrolling / swing (green modifier mode).
  setLaunchpadLedColor(
      kLaunchpadTopRowControlNoteMin + 0,
      g_modifierMode == 1
          ? (g_channelSwing[g_lastPressedChannel] >= kSwingMax
                 ? kLaunchpadColorAmberHigh
                 : kLaunchpadColorGreenLow)
          : (g_channelOffset > 0
                 ? kLaunchpadColorWhiteLow
                 : kLaunchpadColorOff));

  setLaunchpadLedColor(
      kLaunchpadTopRowControlNoteMin + 1,
      g_modifierMode == 1
          ? (g_channelSwing[g_lastPressedChannel] == 0
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

  // Channel scrolling / swing (green modifier mode).
  setLaunchpadLedColor(
      kLaunchpadTopRowControlNoteMin + 0,
      g_modifierMode == 1
          ? (g_channelSwing[g_lastPressedChannel] >= kSwingMax
                 ? kLaunchpadColorAmberHigh
                 : kLaunchpadColorGreenLow)
          : (g_channelOffset > 0
                 ? kLaunchpadColorWhiteLow
                 : kLaunchpadColorOff));

  setLaunchpadLedColor(
      kLaunchpadTopRowControlNoteMin + 1,
      g_modifierMode == 1
          ? (g_channelSwing[g_lastPressedChannel] == 0
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

  // Modifier (Pad 97): 0 = off, 1 = green (mute), 2 = red (delete),
  // 3 = amber (save), 4 = blue (load). Note mode is not part of the cycle
  // any more, but keep this pad white while it runs so the mode is still
  // visible on the device.
  setLaunchpadLedColor(
      kLaunchpadTopRowControlNoteMin + 6,
      g_launchpadNoteMode
          ? kLaunchpadColorWhiteHigh
          : (g_modifierMode == 1
                 ? kLaunchpadColorGreenHigh
                 : (g_modifierMode == 2
                        ? kLaunchpadColorRedHigh
                        : (g_modifierMode == 3
                               ? kLaunchpadColorAmberHigh
                               : (g_modifierMode == 4
                                      ? kLaunchpadColorBlueLow
                                      : kLaunchpadColorOff)))));

  // Run/stop. A held play/pause pad is the enter/leave-note-mode gesture, so
  // amber means "this hold is being counted" instead of the run/pause color.
  setLaunchpadLedColor(
      kLaunchpadTopRowControlNoteMin + 7,
      g_noteModeHoldActive
          ? kLaunchpadColorAmberHigh
          : (g_running
                 ? kLaunchpadColorWhiteHigh
                 : kLaunchpadColorRedHigh));

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

// -----------------------------------------------------------------------------
// Launchpad-only recording (note mode)
// -----------------------------------------------------------------------------

// In note mode the grid pads supply the pitch themselves, so a pad touch
// arrives as a real NoteOn carrying a MIDI pitch instead of the 11-98 pad
// address that Programmer Mode sends as a Control Change. Record that pitch
// at the launchpad write cursor so a sequence can be built without a
// hardware MIDI keyboard.
//
// The lane is the recording channel picked with the right-column buttons
// before entering note mode (shifted by the scroll offset) - the same lane
// the hardware keyboard records into. The Launchpad's own note channel is
// deliberately ignored: Live Mode reports its pads on MIDI channel 2, which
// would otherwise put every recorded pitch one lane over.
void recordLaunchpadNotePitch(
    byte channel,
    byte note,
    byte velocity)
{
  (void)channel;

  // Same lane as handleHardwareMidiIn: right-column selection + scroll.
  const uint8_t laneChannel =
      (uint8_t)((g_recordingChannelOffset + g_channelOffset) % kMidiChannelCount);

  if (g_launchpadRecordStep >= g_sequenceLength)
  {
    g_launchpadRecordStep = 0;
  }

  StepLaneState &lane =
      g_sequence[g_launchpadRecordStep][laneChannel];

  // A pad NoteOn always carries a velocity, but keep a non-zero fallback:
  // the sequencer treats velocity 0 as "note off" and would drop the note.
  const byte resolvedVelocity =
      velocity != 0
          ? velocity
          : g_launchpadRecordVelocity;

  lane.substep[0].active = true;
  lane.substep[0].note = note;
  lane.substep[0].velocity = resolvedVelocity;

  // A recorded cell replaces anything else that was in this step/lane.
  for (uint8_t k = 1; k < kMicrostepMax; ++k)
  {
    lane.substep[k].active = false;
  }

  lane.chordActiveMask = 0;
  lane.muted = false;

  // Overdub feedback: with the transport stopped nothing else would sound
  // the cell just written, so play it immediately on the lane it was
  // recorded to. START opens the echo session once because pausing already
  // sent STOP (and instruments ignore notes after that); the matching
  // release comes from onLaunchpadNoteOff. While the sequencer runs it plays
  // the cell itself, so the echo is skipped to avoid doubling every note.
  if (!g_running)
  {
    if (!g_launchpadEchoOpen)
    {
      midiOutSendStart();
      g_launchpadEchoOpen = true;
    }

    midiOutSendNoteOn(
        note,
        resolvedVelocity,
        (byte)(laneChannel + 1));

    // The X gives no pressure of its own, so reuse the pad's velocity as
    // aftertouch on the lane just sounded: poly for the pitch that was
    // touched (instruments with per-note pressure) and lane-wide for
    // instruments that only have one pressure per channel. Both are
    // answered by the release in onLaunchpadNoteOff.
    midiOutSendPolyAfterTouch(
        note,
        resolvedVelocity,
        (byte)(laneChannel + 1));

    midiOutSendAfterTouch(
        resolvedVelocity,
        (byte)(laneChannel + 1));

    g_launchpadEchoPressure = resolvedVelocity;
    g_launchpadEchoNote = note;
  }

  // Advance the write cursor so the next tapped pad lands on the next step.
  ++g_launchpadRecordStep;
  if (g_launchpadRecordStep >= g_sequenceLength)
  {
    g_launchpadRecordStep = 0;
  }

  refreshLaunchpadGridLedState();
}

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

  // Microstep edit mode: pressing a grid pad on any row other than the
  // one being edited exits the mode. The edited row's pads add/delete
  // notes in the corresponding substep slot (col k -> slot k), the same
  // way normal step editing works: record + press to add, red mode to
  // delete.
  if (g_microstepEditing)
  {
    if (!active)
      {
        return;
      }
    if (row != g_microstepEditChannel)
    {
      g_microstepEditing = false;
      g_microstepHoldActive = false;
      g_microstepHoldTriggered = false;
      refreshLaunchpadGridLedState();
      return;
    }

    StepLaneState &editLane =
      g_sequence[g_microstepEditStep][g_microstepEditChannel];

    if (g_modifierMode == 2)
    {
      // Red modifier mode: delete the note in this substep slot.
      editLane.substep[col].active = false;
      refreshLaunchpadGridLedState();
      return;
    }

    if (editLane.substep[col].active)
    {
      editLane.substep[col].active = false;
      editLane.substep[col].note = 0;
      editLane.substep[col].velocity = 0;
    }
    else
    {
      editLane.substep[col].active = true;
      editLane.substep[col].note = g_lastHwNote;
      editLane.substep[col].velocity = g_lastHwVelocity;
    }

    editLane.muted = false;
    refreshLaunchpadGridLedState();
    return;
  }

  // Chord edit mode: the selected lane already has a base note. Each pad in
  // the edited row adds a higher note relative to the current page, and the
  // page can be scrolled right up to 4 times to reach +31 additional notes.
  if (g_chordEditing)
  {
    if (!active)
    {
      return;
    }

    if (row != g_chordEditChannel)
    {
      g_chordEditing = false;
      refreshLaunchpadGridLedState();
      return;
    }

    StepLaneState &editLane =
        g_sequence[g_chordEditStep][g_chordEditChannel];

    if (!editLane.substep[0].active)
    {
      g_chordEditing = false;
      refreshLaunchpadGridLedState();
      return;
    }

    const byte baseNote = editLane.substep[0].note;
    const uint8_t page = g_chordEditPage;

    // Page 0 adds +1..+7, then pages 1..3 add +8..+31.
    const uint8_t slot =
        page == 0
            ? col - 1
            : page * kMicrostepMax - 1 + col;

    if (page == 0 && col == 0)
    {
      refreshLaunchpadGridLedState();
      return;
    }

    if (slot >= kChordNoteMax ||
        baseNote + slot + 1 > 127)
    {
      return;
    }

    const uint32_t slotMask = 1UL << slot;
    if ((editLane.chordActiveMask & slotMask) != 0)
    {
      editLane.chordActiveMask &= ~slotMask;
      editLane.chordNote[slot] = 0;
      editLane.chordVelocity[slot] = 0;
    }
    else
    {
      editLane.chordActiveMask |= slotMask;
      editLane.chordNote[slot] = baseNote + slot + 1;
      editLane.chordVelocity[slot] = editLane.substep[0].velocity;
    }

    refreshLaunchpadGridLedState();
    return;
  }

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
    // A pad release cancels any pending microstep hold on that pad.
    if (g_microstepHoldActive &&
        g_microstepHoldStep == step &&
        g_microstepHoldChannel == laneChannel)
    {
      g_microstepHoldActive = false;
    }

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
    for (uint8_t k = 0; k < kMicrostepMax; ++k)
    {
      lane.substep[k].active = false;
    }

    lane.chordActiveMask = 0;
    lane.muted = false;

    refreshLaunchpadGridLedState();
    return;
  }

  // While holding the record button, touching a grid pad records
  // the last hardware MIDI keyboard note onto that step. If the
  // step is already on this pad, delete it instead (toggle).
  if (g_recordingHeldNote)
  {
    if (lane.isSubstepActive())
    {
      for (uint8_t k = 0; k < kMicrostepMax; ++k)
      {
        lane.substep[k].active = false;
      }

      lane.muted = false;
    }
    else
    {
      lane.substep[0].active = true;
      lane.muted = false;
      lane.substep[0].note = g_lastHwNote;
      lane.substep[0].velocity = g_lastHwVelocity;
    }

    refreshLaunchpadGridLedState();
  }

  // Hold a grid pad (without recording) to open microstep editing, or chord
  // editing when the blue modifier is active. The hold is resolved in
  // handleMicrostepEditHold() on the next loop.
  if ((g_modifierMode == 0 || g_modifierMode == 4) &&
      !g_recordingHeldNote)
  {
    g_microstepHoldStartMs = millis();
    g_microstepHoldActive = true;
    g_microstepHoldTriggered = false;
    g_microstepHoldStep = step;
    g_microstepHoldChannel = laneChannel;
  }
}

// -----------------------------------------------------------------------------
// Chord edit (blue modifier + long-press a grid pad)
// -----------------------------------------------------------------------------

void enterChordEditMode(
    uint8_t step,
    uint8_t channel)
{
  if (step >= g_sequenceLength)
  {
    return;
  }

  if (channel >= kMidiChannelCount)
  {
    return;
  }

  StepLaneState &lane = g_sequence[step][channel];

  // A chord needs a base note already recorded at the step.
  if (!lane.substep[0].active)
  {
    return;
  }

  g_chordEditing = true;
  g_chordEditStep = step;
  g_chordEditChannel = channel;
  g_chordEditPage = 0;

  // A lane can be re-entered after its base note was changed. Keep existing
  // chord notes only when they still fit above the current base.
  for (uint8_t i = 0; i < kChordNoteMax; ++i)
  {
    if (lane.isChordNoteActive(i) &&
        lane.chordNote[i] <= lane.substep[0].note)
    {
      lane.chordActiveMask &= ~(1UL << i);
    }
  }

  refreshLaunchpadGridLedState();
}

// -----------------------------------------------------------------------------
// Microstep edit (long-press a grid pad)
// -----------------------------------------------------------------------------

void handleMicrostepEditHold()
{
  if (!g_microstepHoldActive)
  {
    return;
  }

  if (g_microstepHoldTriggered)
  {
    return;
  }

  if ((millis() - g_microstepHoldStartMs) < kMicrostepHoldMs)
  {
    return;
  }

  g_microstepHoldTriggered = true;
  g_microstepHoldActive = false;

  if (g_modifierMode == 4)
  {
    enterChordEditMode(g_microstepHoldStep, g_microstepHoldChannel);
    return;
  }

  g_microstepEditing = true;
  g_microstepEditStep = g_microstepHoldStep;
  g_microstepEditChannel = g_microstepHoldChannel;

  refreshLaunchpadGridLedState();
}

// -----------------------------------------------------------------------------
// Note-mode gesture (hold the play/pause pad to enter / leave note mode)
// -----------------------------------------------------------------------------

// Called every loop: a play/pause pad (98) held down starts a clock. Once it
// has been held past the window for the mode we are currently in -
// kNoteModeEnterHoldMs outside note mode, kNoteModeExitHoldMs inside it -
// the gesture fires while the pad is still held. The press never acts on the
// transport itself, so the release can tell a tap (play/pause) from a
// gesture that already switched note mode.
void handleLaunchpadNoteModeHold()
{
  if (!g_noteModeHoldActive || g_noteModeHoldTriggered)
  {
    return;
  }

  const uint16_t holdMs =
      g_launchpadNoteMode ? kNoteModeExitHoldMs : kNoteModeEnterHoldMs;

  if ((millis() - g_noteModeHoldStartMs) < holdMs)
  {
    return;
  }

  g_noteModeHoldTriggered = true;
  g_noteModeHoldActive = false;

  if (g_launchpadNoteMode)
  {
    exitLaunchpadNoteMode();
  }
  else
  {
    enterLaunchpadNoteMode();
  }

  refreshLaunchpadControlLedState();
}

// -----------------------------------------------------------------------------
// Launchpad pad handling
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

  // Reprogram the hardware clock timer for the new tempo.
  restartMidiClockTimer();
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
      restartMidiClockTimer();
    }
    break;

  case kLaunchpadTopRowControlNoteMin + 5:
    if (g_tempoBpm < kMaxTempoBpm)
    {
      g_tempoBpm += 1;
      restartMidiClockTimer();
    }
    break;

    // ---------------------------------------------------------------------------
    // Play / stop
    // ---------------------------------------------------------------------------

  case kLaunchpadTopRowControlNoteMin + 7:
    g_running = !g_running;
    // Remember an explicit stop so releasing a record-channel button
    // cannot silently restart the transport afterwards.
    g_userPaused = !g_running;

    if (g_running)
    {
      // Restart from the first step and reset timer phases so playback begins
      // cleanly at the start of the sequence.
      g_stepIndex = 0;
      g_stepTimer = 0;
      g_substepIndex = 0;
      memset(g_recordSkipStep, 0xFF, sizeof(g_recordSkipStep));
      restartMidiClockTimer();

      midiOutSendStart();
      advanceSequencerStep();  // Start the first step immediately.
    } 
    else
    {
      // Stop the MIDI transport; pressing again starts from step zero.
      // Clear anything still sounding first: the note-off for the step that
      // just played is normally sent at the next step boundary, and that
      // boundary never arrives while paused, so the last note would
      // otherwise keep ringing (and re-articulate on resume).
      releaseSoundingNotes();
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

  // Note mode: the pad touch already carries the pitch, so record it
  // directly instead of staging it as a grid-pad address.
  if (g_launchpadNoteMode)
  {
    // The bottom-right pad arrives as NoteOn on pitch 0 in Live Mode; that
    // is not a playable pitch, so drop it before it is recorded as one.
    // Note mode now leaves through the held play/pause pad (98), not here.
    if (note == kLaunchpadNoteModeIgnoredNote)
    {
      return;
    }

    // Nothing else is intercepted here: Novation's own Live-Mode firmware
    // already gives the top-row pads their transport/tempo/scroll behaviour,
    // so every other NoteOn is a pitch to record. The lane comes from the
    // right-column record buttons selected before entering note mode, not
    // from the Launchpad's own note channel (Live Mode reports its pads on
    // channel 2, which would shift everything one lane over).
    recordLaunchpadNotePitch(
        channel,
        note,
        velocity);
    return;
  }

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

  // Note mode: releasing a pad must not erase the recorded note. Deleting
  // is done after leaving note mode, in red (delete) modifier mode.
  if (g_launchpadNoteMode)
  {
    // The touch echoed the pitch live when the transport was stopped, so the
    // release has to answer it on the same lane - otherwise the instrument
    // keeps the key held down until the next note on that pitch arrives.
    if (!g_running && note != kLaunchpadNoteModeIgnoredNote)
    {
      const uint8_t laneChannel =
          (uint8_t)((g_recordingChannelOffset + g_channelOffset) %
                    kMidiChannelCount);

      midiOutSendNoteOff(
          note,
          velocity,
          (byte)(laneChannel + 1));

      // Answer the pressure the touch put on the lane, otherwise the
      // instrument keeps squeezing a key that is no longer held.
      midiOutSendPolyAfterTouch(
          note,
          0,
          (byte)(laneChannel + 1));

      if (g_launchpadEchoPressure != 0)
      {
        midiOutSendAfterTouch(
            0,
            (byte)(laneChannel + 1));

        g_launchpadEchoPressure = 0;
      }

      g_launchpadEchoNote = 0;
    }

    return;
  }

  stageLaunchpadPad(
      channel,
      note,
      velocity,
      false);
}

// -----------------------------------------------------------------------------
// Launchpad pressure (Live/Note Mode aftertouch)
// -----------------------------------------------------------------------------

// The X's grid pads are not pressure-sensitive, so these messages only ever
// show up from a controller that does have a pressure surface. Forward them
// to the six outputs so pressure lands on the same instrument the pitches do:
// in note mode the lane is the selected recording channel - exactly like
// recordLaunchpadNotePitch - because Live Mode reports its pads on channel 2,
// which would otherwise put the pressure one lane over. Outside note mode
// nothing is remapped: Programmer Mode never sends pressure, so anything that
// does arrive is passed through on its own channel.
void onLaunchpadAfterTouchPoly(
    byte channel,
    byte note,
    byte pressure)
{
  g_seenLaunchpadInput = true;

  if (!g_launchpadNoteMode)
  {
    midiOutSendPolyAfterTouch(note, pressure, channel);
    return;
  }

  const uint8_t laneChannel =
      (uint8_t)((g_recordingChannelOffset + g_channelOffset) %
                kMidiChannelCount);

  midiOutSendPolyAfterTouch(
      note,
      pressure,
      (byte)(laneChannel + 1));
}

void onLaunchpadAfterTouch(
    byte channel,
    byte pressure)
{
  g_seenLaunchpadInput = true;

  if (!g_launchpadNoteMode)
  {
    midiOutSendAfterTouch(pressure, channel);
    return;
  }

  const uint8_t laneChannel =
      (uint8_t)((g_recordingChannelOffset + g_channelOffset) %
                kMidiChannelCount);

  midiOutSendAfterTouch(
      pressure,
      (byte)(laneChannel + 1));
}

void onLaunchpadControlChange(
    byte channel,
    byte control,
    byte value)
{
  (void)channel;

  // ---------------------------------------------------------------------------
  // NOTE MODE: bottom-right function pad reports as control 0
  //   In Live/Note Mode that pad reports as pitch 0 / control 0 rather than
  //   as its Programmer-Mode pad address, and 0 is not a usable control
  //   number - so swallow it here instead of letting it fall through to the
  //   generic control handling. Note mode leaves through the held play/pause
  //   pad (98), handled in the top-row branch below.
  // ---------------------------------------------------------------------------

  if (g_launchpadNoteMode && control == kLaunchpadNoteModeIgnoredNote)
  {
    return;
  }

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

    if (value != 0 && internalChannel < kMidiChannelCount)
    {
      g_lastPressedChannel = internalChannel;
    }

    // ---------------------------------------------------------------------------
    // MODIFIER MODE: mute / delete / save / load
    // ---------------------------------------------------------------------------

    // Note mode keeps the right column inert: the recording lane is picked
    // before note mode is entered, and g_modifierMode stays 0 while note mode
    // runs, so without this guard the pads would fall through to the record-
    // channel path below and re-arm the transport mid-recording.
    if (g_launchpadNoteMode || g_modifierMode > 0)
    {
      // Act only on press so mute is a persistent toggle, not momentary.
      // (Right-column pads send a CC on press and another on release.)
      if (value != 0)
      {
        bool sdOk = false;

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
          else if (g_modifierMode == 3)
          {
            // Save the entire channel lane to the SD card.
            sdOk = sdStoreSaveChannel(internalChannel);
          }
          else if (g_modifierMode == 4)
          {
            // Load the entire channel lane from the SD card.
            sdOk = sdStoreLoadChannel(internalChannel);
          }
        }

        // Flash the built-in LED when an SD save/load succeeded.
        if (sdOk)
        {
          g_ledFlashTimer = 0;
          digitalWriteFast(LED_BUILTIN, HIGH);
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

      // Make sure sequencer is running after recording, but never over an
      // explicit stop: pausing with the top-row play/stop pad first lets
      // Launchpad note mode record pitches while the transport stays put.
      if (!g_running && !g_userPaused)
      {
        g_running = true;
        g_stepTimer = 0;
        g_substepIndex = 0;
        restartMidiClockTimer();

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
    // -----------------------------------------------------------------------
    // PLAY/PAUSE PAD (98): tap = play/pause, hold = enter / leave note mode
    //   A press only starts the hold clock; handleLaunchpadNoteModeHold()
    //   switches note mode once the pad has been held past the window for the
    //   mode we are in (kNoteModeEnterHoldMs outside note mode,
    //   kNoteModeExitHoldMs inside it). Releasing before that keeps the
    //   pad's normal meaning, so a quick tap still toggles the transport -
    //   resolved on release rather than on press, which is what lets the
    //   three meanings share one pad.
    // -----------------------------------------------------------------------

    if (control == kLaunchpadTopRowControlNoteMin + 7)
    {
      if (value != 0)
      {
        g_noteModeHoldStartMs = millis();
        g_noteModeHoldActive = true;
        g_noteModeHoldTriggered = false;

        refreshLaunchpadControlLedState();
        return;
      }

      // Release: a hold that already switched note mode must not also flip
      // the transport; a tap that never reached the window is the play/stop
      // pad.
      if (g_noteModeHoldActive && !g_noteModeHoldTriggered)
      {
        handleLaunchpadControl(control);
      }
      else
      {
        refreshLaunchpadControlLedState();
      }

      g_noteModeHoldActive = false;
      return;
    }

    if (g_chordEditing &&
        value != 0 &&
        (control == kLaunchpadTopRowControlNoteMin + 2 ||
         control == kLaunchpadTopRowControlNoteMin + 3))
    {
      // While editing a chord, the step-scroll pads act as a 4-page note
      // browser: left/right moves through the +1..+7 / +8..+15 / +16..+23 /
      // +24..+31 note groups above the base note.
      if (control == kLaunchpadTopRowControlNoteMin + 2)
      {
        if (g_chordEditPage > 0)
        {
          --g_chordEditPage;
        }
      }
      else if (g_chordEditPage < 3)
      {
        ++g_chordEditPage;
      }

      refreshLaunchpadGridLedState();
      return;
    }

    // Pad 97 (note 91+6) is the modifier: a 5-state toggle that cycles
    // on each press. 0 = none (off), 1 = green (mute), 2 = red (delete),
    // 3 = amber (save), 4 = blue (load). Note mode is not part of the cycle
    // - it is entered and left by holding the play/pause pad (98).
    // While set, the right column pads act on the current channel.
    if (control == kLaunchpadTopRowControlNoteMin + 6)
    {
      if (value != 0)
      {
        cycleLaunchpadModifierMode();
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
        // Green modifier mode: pad 91 increases swing for the last selected channel.
        adjustChannelSwing(g_lastPressedChannel, 1);
      }
      else if (control == kLaunchpadTopRowControlNoteMin + 1)
      {
        // Green modifier mode: pad 92 decreases swing for the last selected channel.
        adjustChannelSwing(g_lastPressedChannel, -1);
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