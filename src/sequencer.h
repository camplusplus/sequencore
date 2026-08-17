#pragma once

#include <Arduino.h>

// -----------------------------------------------------------------------------
// Timing
// -----------------------------------------------------------------------------

uint16_t calculateStepDurationMs();
uint16_t calculateClockPulseMs();

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
void playAllChannelSequence();

// -----------------------------------------------------------------------------
// Hardware MIDI input
// -----------------------------------------------------------------------------

void handleHardwareMidiIn();

// -----------------------------------------------------------------------------
// Debug
// -----------------------------------------------------------------------------

void debugPrintState();