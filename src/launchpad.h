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