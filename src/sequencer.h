#pragma once

#include <Arduino.h>

// -----------------------------------------------------------------------------
// Timing
// -----------------------------------------------------------------------------

uint16_t calculateStepDurationMs();
uint32_t calculateClockPulseUs();

// -----------------------------------------------------------------------------
// MIDI clock hardware timer
// -----------------------------------------------------------------------------

// Start the hardware (PIT) timer that fires the 24ppqn MIDI clock (F8) ISR
// at the current tempo. Call once from setup().
void startMidiClockTimer();

// Reprogram the clock timer's period from the current tempo. The new period
// applies at the next pulse boundary, so the next pulse lands a full interval
// from now instead of inheriting a stale reference. Call after any tempo
// change or on play/stop.
void restartMidiClockTimer();

// -----------------------------------------------------------------------------
// MIDI output
// -----------------------------------------------------------------------------

void sendMidiMessage(
    byte channel,
    byte note,
    byte velocity,
    bool noteOn);

void sendStepClockPulse();

// -----------------------------------------------------------------------------
// Sequence editing
// -----------------------------------------------------------------------------

void clearCurrentStep(uint8_t step);

void deleteChannel(uint8_t channel);

void adjustChannelSwing(uint8_t channel, int8_t delta);

void adjustSequenceLength(int8_t delta);

void recordCurrentStep(
    uint8_t channel,
    byte note,
    byte velocity);

// -----------------------------------------------------------------------------
// Playback
// -----------------------------------------------------------------------------

void sendActiveStepNotes(uint8_t step);
void suppressLastStepNotes(uint8_t step);
void advanceSequencerStep();
void handleSubstepPlayback();

// Release everything the transport still has sounding: the one note each
// channel may be holding (main note or substep slot) plus any chord tones
// held from the last played step. A step's note-off is normally sent at the
// *next* step boundary, so this is what a stopped transport has to do
// explicitly - otherwise pausing leaves the last note ringing on the outputs
// until playback resumes.
void releaseSoundingNotes();

// Whether the incoming stream is still acting as the clock: a pulse has to
// have arrived within the stop-gap window (see stopForMissingClockPulses).
// A stream that went silent - or has not sent a pulse yet - cannot drive
// the transport, so resume paths hand the clock back to the PIT timer.
bool inputClockIsLive();

// Pause the transport when the clock arriving from MIDI IN stops ticking:
// the incoming stream is the source, so when its source pauses the
// sequencer pauses with it (see the clock branch in handleHardwareMidiIn).
void stopForMissingClockPulses();

// -----------------------------------------------------------------------------
// Hardware MIDI input
// -----------------------------------------------------------------------------

void handleHardwareMidiIn();

// -----------------------------------------------------------------------------
// Debug
// -----------------------------------------------------------------------------

void debugPrintState();