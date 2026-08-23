#pragma once

#include <Arduino.h>
#include <MIDI.h>
#include <USBHost_t36.h>

// -----------------------------------------------------------------------------
// MIDI / USB hardware
// -----------------------------------------------------------------------------

// Hardware DIN MIDI (Teensy 4.1).
// MIDI input: Serial1 RX (pin 0), wrapped by midiPort for parsing.
// MIDI output: the same data is broadcast to all six hardware serial TX
// pins - Serial1 (pin 1), Serial2 (pin 8), Serial3 (pin 14), Serial4
// (pin 17), Serial5 (pin 20) and Serial6 (pin 24).
extern midi::MidiInterface<midi::SerialMIDI<HardwareSerial>> midiPort;

// Number of DIN MIDI output ports the sequencer broadcasts to.
constexpr uint8_t kMidiOutPortCount = 6;

// Substep divisions per step (1..kMicrostepMax). When > 1, each step is
// split into equal substep slots that can each hold a note.
constexpr uint8_t kMicrostepDivisionsDefault = 1;

// Broadcast the same MIDI message to all six DIN output pins.
void midiOutBeginAll(byte channel);
void midiOutSendNoteOn(byte note, byte velocity, byte channel);
void midiOutSendNoteOff(byte note, byte velocity, byte channel);
void midiOutSendClock();
void midiOutSendStart();
void midiOutSendStop();

// Duration (ms) the built-in LED stays on after a MIDI input flash.
constexpr uint16_t kLedFlashMs = 30;

extern USBHost myusb;
extern USBHub hub1;
extern MIDIDevice_BigBuffer launchpad;

// -----------------------------------------------------------------------------
// Constants
// -----------------------------------------------------------------------------

constexpr uint8_t kMinSequenceLength = 4;
constexpr uint8_t kMaxSequenceLength = 64;
constexpr uint8_t kDefaultSequenceLength = 16;
constexpr uint8_t kMidiChannelCount = 16;

constexpr uint8_t kLaunchpadGridNoteMin = 11;
constexpr uint8_t kLaunchpadGridNoteMax = 88;

constexpr uint8_t kGridNotes[8][8] = {
        {81, 82, 83, 84, 85, 86, 87, 88}, // Row 0 (80s)
        {71, 72, 73, 74, 75, 76, 77, 78}, // Row 1 (70s)
        {61, 62, 63, 64, 65, 66, 67, 68}, // Row 2 (60s)
        {51, 52, 53, 54, 55, 56, 57, 58}, // Row 3 (50s)
        {41, 42, 43, 44, 45, 46, 47, 48}, // Row 4 (40s)
        {31, 32, 33, 34, 35, 36, 37, 38}, // Row 5 (30s)
        {21, 22, 23, 24, 25, 26, 27, 28}, // Row 6 (20s)
        {11, 12, 13, 14, 15, 16, 17, 18}  // Row 7 (10s)
    };


constexpr uint8_t kLaunchpadTopRowControlNoteMin = 91;
constexpr uint8_t kLaunchpadTopRowControlNoteMax = 98;

constexpr uint8_t kLaunchpadRightColumnControlNoteMin = 19;
constexpr uint8_t kLaunchpadRightColumnControlNoteMax = 89;

constexpr uint16_t kMinTempoBpm = 60;
constexpr uint16_t kMaxTempoBpm = 200;

constexpr uint8_t kRatchetMax = 4;
constexpr uint8_t kMicrostepMax = 8;
constexpr uint8_t kSwingMax = 50;
constexpr uint8_t kShuffleMax = 50;

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

// -----------------------------------------------------------------------------
// Sequence state
// -----------------------------------------------------------------------------

// One substep slot. Slot 0 is the main note played at the step start.
// Higher slots play at (k-1) * (step / g_microstepDivisions).
struct SubstepNote
{
  bool active = false;
  byte note = 60;
  byte velocity = 100;
};

struct StepLaneState
{
  SubstepNote substep[kMicrostepMax];
  bool muted = false;

  bool isSubstepActive() const
  {
    for (uint8_t i = 0; i < kMicrostepMax; ++i)
    {
      if (substep[i].active)
      {
        return true;
      }
    }
    return false;
  }

  // Returns true if slot k is active; otherwise false.
  bool getSubstep(uint8_t k, byte *outNote, byte *outVelocity) const
  {
    if (k >= kMicrostepMax || !substep[k].active)
    {
      return false;
    }
    *outNote = substep[k].note;
    *outVelocity = substep[k].velocity;
    return true;
  }
};

extern StepLaneState g_sequence[kMaxSequenceLength][kMidiChannelCount];

// Current sequence length in steps (kMinSequenceLength..kMaxSequenceLength).
extern uint8_t g_sequenceLength;

// Per-channel shuffle (0..kShuffleMax). Applied to odd steps, in sync
// with swing (odd-step microsecond delay), but independent of it.
extern uint8_t g_channelShuffle[kMidiChannelCount];

// -----------------------------------------------------------------------------
// Sequencer state
// -----------------------------------------------------------------------------

extern uint8_t g_stepOffset;
extern uint8_t g_channelOffset;

extern uint8_t g_stepIndex;
// Substep (slot) counter within the current step (0..g_microstepDivisions-1).
// Advances every sequencer tick; the step advances when it wraps.
extern uint8_t g_substepIndex;
extern uint8_t g_lastPlayedStep;
extern bool g_hasPlayedStep;

extern uint16_t g_tempoBpm;
extern uint8_t g_microstepDivisions;
extern uint8_t g_ratchetCount;
extern uint8_t g_swingPct;

// Channel corresponding to the most recently pressed right-column
// channel pad. Shuffle up/down pads (91/92) act on this channel while
// the green modifier is active.
extern uint8_t g_lastPressedChannel;

extern bool g_running;
extern bool g_recording;
extern bool g_overdub;

extern bool g_recordingHeldNote;
extern uint8_t g_recordingChannelOffset;

// Last note/velocity received from hardware MIDI (NoteOn).
// Used so grid pads can record the hardware keyboard note.
extern byte g_lastHwNote;
extern byte g_lastHwVelocity;

// Count of notes currently held on the hardware MIDI keyboard.
extern uint8_t g_hwNotesHeld;

// -----------------------------------------------------------------------------
// Timers
// -----------------------------------------------------------------------------

extern elapsedMillis g_stepTimer;
extern elapsedMillis g_clockPulseTimer;
extern elapsedMillis g_ledFlashTimer;
extern elapsedMillis g_statusTimer;
extern elapsedMillis g_launchpadInitTimer;

// -----------------------------------------------------------------------------
// Launchpad state
// -----------------------------------------------------------------------------

extern bool g_seenLaunchpadInput;
extern bool g_launchpadProgramModeSent;
extern bool g_launchpadTestPatternSent;
extern bool g_launchpadControlLedsInitialized;

extern byte g_lastPressedControlNote;
extern byte g_controlFlashNote;

// Pad 97 modifier mode (3-state toggle, cycles on press).
// 0 = none (LED off)
// 1 = green (right column pads = mute channel; grid pads = mute cell/step;
//             top row 91 = shuffle up, 92 = shuffle down,
//                       93 = sequence length down, 94 = sequence length up,
//                       95 = microstep division down, 96 = microstep division up)
// 2 = red (right column pads = delete channel)
extern uint8_t g_modifierMode;

// Bit N set = channel N is muted (green modifier mode).
extern uint32_t g_channelMuteMask;

// Step N is muted (green modifier mode, long-press on a grid pad).
extern bool g_stepMuted[kMaxSequenceLength];

// -----------------------------------------------------------------------------
// Green-mode long-press (hold a grid pad to mute the whole step)
// -----------------------------------------------------------------------------

// Hold this long (ms) to mute the whole step instead of just the cell.
constexpr uint16_t kGridHoldMs = 400;

extern uint32_t g_gridHoldStartMs;
extern bool g_gridHoldActive;
extern bool g_gridHoldTriggered;
extern uint8_t g_gridHoldStep;
extern uint8_t g_gridHoldChannel;

// -----------------------------------------------------------------------------
// Substep editing mode (pads 95/96 green mode)
// -----------------------------------------------------------------------------

// When true, each grid pad records into the currently-cycling substep slot
// of that channel/step (Novation Circuit-style). Slot 0 is the main note.
extern bool g_substepEditing;

// Tracks a long-press on a single grid pad to cycle the substep slot
// (green-mode + substep editing).
extern uint32_t g_substepHoldStartMs;
extern bool g_substepHoldActive;
extern bool g_substepHoldTriggered;
extern uint8_t g_substepHoldStep;
extern uint8_t g_substepHoldChannel;
