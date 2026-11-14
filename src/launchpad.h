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

// Launchpad note mode (modifier mode 5): put the device back into its native
// Live/Note Mode so the grid pads supply the pitch themselves.
void sendLaunchpadLiveMode();
void sendLaunchpadNoteLayout();
void sendLaunchpadScaleMode();
void enterLaunchpadNoteMode();
void exitLaunchpadNoteMode();

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
// Note-mode exit gesture (hold the play/pause pad)
// -----------------------------------------------------------------------------

// Called every loop: while in note mode, a play/pause pad (98) that has been
// held for kNoteModeExitHoldMs leaves note mode. A shorter press/release is
// left to the pad's normal play/stop meaning.
void handleLaunchpadNoteModeExitHold();

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

// Live/Note Mode pressure: 0xA0 applies to one held pad, 0xD0 to the whole
// channel. USBHost_t36 routes them through the velocity-change and
// aftertouch slots, both of which default to NULL.
void onLaunchpadAfterTouchPoly(
    byte channel,
    byte note,
    byte pressure);

void onLaunchpadAfterTouch(
    byte channel,
    byte pressure);