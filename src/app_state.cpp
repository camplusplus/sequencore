#include "app_state.h"

// -----------------------------------------------------------------------------
// MIDI / USB hardware
// -----------------------------------------------------------------------------

// Teensy 4.1 hardware UART TX pins:
//   Serial1 TX = pin 1,  Serial2 TX = pin 8,
//   Serial3 TX = pin 14, Serial4 TX = pin 17,
//   Serial5 TX = pin 20, Serial6 TX = pin 24.
// MIDI input uses Serial1 RX (pin 0) via midiPort.
// All MIDI output is broadcast on all six TX pins above.
MIDI_CREATE_INSTANCE(HardwareSerial, Serial1, midiPort);
MIDI_CREATE_INSTANCE(HardwareSerial, Serial1, midiOut1);
MIDI_CREATE_INSTANCE(HardwareSerial, Serial2, midiOut2);
MIDI_CREATE_INSTANCE(HardwareSerial, Serial3, midiOut3);
MIDI_CREATE_INSTANCE(HardwareSerial, Serial4, midiOut4);
MIDI_CREATE_INSTANCE(HardwareSerial, Serial5, midiOut5);
MIDI_CREATE_INSTANCE(HardwareSerial, Serial6, midiOut6);

midi::MidiInterface<midi::SerialMIDI<HardwareSerial>> *kMidiOutPorts[
    kMidiOutPortCount] = {
    &midiOut1,
    &midiOut2,
    &midiOut3,
    &midiOut4,
    &midiOut5,
    &midiOut6};

// -----------------------------------------------------------------------------
// Broadcast helpers: send the same MIDI message on all six DIN output pins
// -----------------------------------------------------------------------------

void midiOutBeginAll(byte channel)
{
  for (uint8_t i = 0; i < kMidiOutPortCount; ++i)
  {
    kMidiOutPorts[i]->begin(channel);
  }
}

void midiOutSendNoteOn(byte note, byte velocity, byte channel)
{
  for (uint8_t i = 0; i < kMidiOutPortCount; ++i)
  {
    kMidiOutPorts[i]->sendNoteOn(note, velocity, channel);
  }
}

void midiOutSendNoteOff(byte note, byte velocity, byte channel)
{
  for (uint8_t i = 0; i < kMidiOutPortCount; ++i)
  {
    kMidiOutPorts[i]->sendNoteOff(note, velocity, channel);
  }
}

void midiOutSendClock()
{
  for (uint8_t i = 0; i < kMidiOutPortCount; ++i)
  {
    kMidiOutPorts[i]->sendClock();
  }
}

void midiOutSendStart()
{
  for (uint8_t i = 0; i < kMidiOutPortCount; ++i)
  {
    kMidiOutPorts[i]->sendStart();
  }
}

void midiOutSendStop()
{
  for (uint8_t i = 0; i < kMidiOutPortCount; ++i)
  {
    kMidiOutPorts[i]->sendStop();
  }
}

USBHost myusb;
USBHub hub1(myusb);
MIDIDevice_BigBuffer launchpad(myusb);

// -----------------------------------------------------------------------------
// Sequence state
// -----------------------------------------------------------------------------

StepLaneState g_sequence[kMaxSequenceLength][kMidiChannelCount];

uint8_t g_sequenceLength = kDefaultSequenceLength;

uint8_t g_channelShuffle[kMidiChannelCount] = {0};

// -----------------------------------------------------------------------------
// Sequencer state
// -----------------------------------------------------------------------------

uint8_t g_stepOffset = 0;
uint8_t g_channelOffset = 0;

uint8_t g_stepIndex = 0;
uint8_t g_substepIndex = 0;
uint8_t g_lastPlayedStep = 0;
bool g_hasPlayedStep = false;

uint16_t g_tempoBpm = 120;
uint8_t g_swingPct = 0;

uint8_t g_lastPressedChannel = 0;

bool g_running = true;
bool g_recording = false;
bool g_overdub = false;

bool g_recordingHeldNote = false;
uint8_t g_recordingChannelOffset = 0;

byte g_lastHwNote = 0;
byte g_lastHwVelocity = 0;
uint8_t g_hwNotesHeld = 0;

// -----------------------------------------------------------------------------
// Timers
// -----------------------------------------------------------------------------

elapsedMillis g_stepTimer;
elapsedMillis g_clockPulseTimer;
elapsedMillis g_ledFlashTimer;
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
bool g_stepMuted[kMaxSequenceLength] = {false};

// -----------------------------------------------------------------------------
// Green-mode long-press
// -----------------------------------------------------------------------------

uint32_t g_gridHoldStartMs = 0;
bool g_gridHoldActive = false;
bool g_gridHoldTriggered = false;
uint8_t g_gridHoldStep = 0;
uint8_t g_gridHoldChannel = 0;

// -----------------------------------------------------------------------------
// Substep editing mode (pads 95/96 green mode)
// -----------------------------------------------------------------------------

// When true, each grid pad records into the currently-cycling substep slot
// of that channel/step (Novation Circuit-style). Slot 0 is the main note.
bool g_substepEditing = false;

// Tracks a long-press on a single grid pad to cycle the substep slot
// (green-mode + substep editing). Used for the note-off handler.
uint32_t g_substepHoldStartMs = 0;
bool g_substepHoldActive = false;
bool g_substepHoldTriggered = false;
uint8_t g_substepHoldStep = 0;
uint8_t g_substepHoldChannel = 0;

bool g_microstepEditing = false;
uint8_t g_microstepEditStep = 0;
uint8_t g_microstepEditChannel = 0;

// -----------------------------------------------------------------------------
// Playback: last note-on time per hardware keyboard note (for velocity)
// -----------------------------------------------------------------------------

uint32_t g_lastHwNoteOnTime = 0;

// -----------------------------------------------------------------------------
// Playback: substep scheduling (queue of slots to fire within a step)
// -----------------------------------------------------------------------------

SubstepNote g_substepQueue[kMicrostepMax];
uint8_t g_substepQueueCount = 0;
uint8_t g_substepQueueNextIndex = 0;
uint32_t g_substepStepStartMs = 0;

// Last note held per channel, used to send note-off at the next step
// boundary (so substep notes are released cleanly instead of overlapping).
byte g_lastPlayedSubstepNote[kMidiChannelCount] = {0};
bool g_lastPlayedSubstepActive[kMidiChannelCount] = {false};
byte g_lastPlayedSubstepVelocity[kMidiChannelCount] = {0};
