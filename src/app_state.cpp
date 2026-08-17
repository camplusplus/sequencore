#include "app_state.h"

// -----------------------------------------------------------------------------
// MIDI / USB hardware
// -----------------------------------------------------------------------------

MIDI_CREATE_INSTANCE(HardwareSerial, Serial1, midiPort);

USBHost myusb;
USBHub hub1(myusb);
MIDIDevice_BigBuffer launchpad(myusb);

// -----------------------------------------------------------------------------
// Sequence state
// -----------------------------------------------------------------------------

StepLaneState g_sequence[kStepCount][kMidiChannelCount];

// -----------------------------------------------------------------------------
// Sequencer state
// -----------------------------------------------------------------------------

uint8_t g_stepOffset = 0;
uint8_t g_channelOffset = 0;

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

bool g_recordingHeldNote = false;
uint8_t g_recordingChannelOffset = 0;

// -----------------------------------------------------------------------------
// Timers
// -----------------------------------------------------------------------------

elapsedMillis g_stepTimer;
elapsedMillis g_clockPulseTimer;
elapsedMillis g_ledTimer;
elapsedMillis g_statusTimer;
elapsedMillis g_launchpadInitTimer;

// -----------------------------------------------------------------------------
// Launchpad state
// -----------------------------------------------------------------------------

bool g_seenLaunchpadInput = false;
bool g_launchpadProgramModeSent = false;
bool g_launchpadTestPatternSent = false;
bool g_launchpadControlLedsInitialized = false;

byte g_lastPressedControlNote = 0xFF;
byte g_controlFlashNote = 0xFF;
uint8_t g_modifierMode = 0;
uint32_t g_channelMuteMask = 0;
