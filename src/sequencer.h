#pragma once

#include <Arduino.h>

// -----------------------------------------------------------------------------
// Timing
// -----------------------------------------------------------------------------

uint16_t calculateStepDurationMs();
uint32_t calculateClockPulseUs();

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