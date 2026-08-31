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

void adjustChannelShuffle(uint8_t channel, int8_t delta);

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

// -----------------------------------------------------------------------------
// Hardware MIDI input
// -----------------------------------------------------------------------------

void handleHardwareMidiIn();

// -----------------------------------------------------------------------------
// Debug
// -----------------------------------------------------------------------------

void debugPrintState();