#pragma once

#include <Arduino.h>
#include <USBHost_t36.h>

// -----------------------------------------------------------------------------
// Launchpad LED handling
// -----------------------------------------------------------------------------

void setLaunchpadLedColor(
    byte note,
    byte color);

void sendLaunchpadProgramMode();

void refreshLaunchpadGridLedState();
void refreshLaunchpadControlLedState();

// -----------------------------------------------------------------------------
// Launchpad note classification
// -----------------------------------------------------------------------------

bool isLaunchpadTopRowControlNote(byte note);
bool isLaunchpadRightColumnControlNote(byte note);
bool isLaunchpadControlNote(byte note);
bool isLaunchpadGridPad(byte note);

// -----------------------------------------------------------------------------
// Launchpad input
// -----------------------------------------------------------------------------

void stageLaunchpadPad(
    byte channel,
    byte note,
    byte velocity,
    bool active);

void handleLaunchpadControl(byte note);

// -----------------------------------------------------------------------------
// Microstep edit (long-press a grid pad)
// -----------------------------------------------------------------------------

// Called every loop: tracks a long-press on a grid pad and opens
// microstep editing for the held pad's step/lane after kMicrostepHoldMs.
void handleMicrostepEditHold();

// -----------------------------------------------------------------------------
// Chord edit (blue mode + long-press a grid pad)
// -----------------------------------------------------------------------------

// Enters chord edit mode for the held step/lane (blue modifier mode,
// grid pad held). step = the sequence step of the held pad,
// channel = the grid row (MIDI channel) of the held pad.
void enterChordEditMode(
    uint8_t step,
    uint8_t channel);

// -----------------------------------------------------------------------------
// USB MIDI callbacks
// -----------------------------------------------------------------------------

void onLaunchpadNoteOn(
    byte channel,
    byte note,
    byte velocity);

void onLaunchpadNoteOff(
    byte channel,
    byte note,
    byte velocity);

void onLaunchpadControlChange(
    byte channel,
    byte control,
    byte value);