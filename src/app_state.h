#pragma once

#include <Arduino.h>
#include <MIDI.h>
#include <USBHost_t36.h>

// -----------------------------------------------------------------------------
// MIDI / USB hardware
// -----------------------------------------------------------------------------

extern midi::MidiInterface<midi::SerialMIDI<HardwareSerial>> midiPort;

extern USBHost myusb;
extern USBHub hub1;
extern MIDIDevice_BigBuffer launchpad;

// -----------------------------------------------------------------------------
// Constants
// -----------------------------------------------------------------------------

constexpr uint8_t kStepCount = 16;
constexpr uint8_t kMidiChannelCount = 16;

constexpr uint8_t kLaunchpadGridNoteMin = 0;
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

// -----------------------------------------------------------------------------
// Sequence state
// -----------------------------------------------------------------------------

struct StepLaneState
{
  bool active = false;
  byte note = 60;
  byte velocity = 100;
};

extern StepLaneState g_sequence[kStepCount][kMidiChannelCount];

// -----------------------------------------------------------------------------
// Sequencer state
// -----------------------------------------------------------------------------

extern uint8_t g_stepOffset;
extern uint8_t g_channelOffset;

extern uint8_t g_stepIndex;
extern uint8_t g_lastPlayedStep;
extern bool g_hasPlayedStep;

extern uint16_t g_tempoBpm;
extern uint8_t g_microstepDivisions;
extern uint8_t g_ratchetCount;
extern uint8_t g_swingPct;

extern bool g_running;
extern bool g_recording;
extern bool g_overdub;

extern bool g_recordingHeldNote;
extern uint8_t g_recordingChannelOffset;

// -----------------------------------------------------------------------------
// Timers
// -----------------------------------------------------------------------------

extern elapsedMillis g_stepTimer;
extern elapsedMillis g_clockPulseTimer;
extern elapsedMillis g_ledTimer;
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
extern bool g_ModifierHeld;
