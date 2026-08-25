#pragma once

#include <Arduino.h>

#include "app_state.h"

// -----------------------------------------------------------------------------
// SD card storage (Teensy 4.1 built-in SD controller)
//
// All files live in the /sequencore folder on the SD card root.
// Channel patterns are stored as /sequencore/ch<N>[K].seq
// (N = 0..15, K = track number), one file per MIDI channel lane
// (all steps, substep slots included).
//
// Each channel can have multiple saved tracks: ch1[1].seq, ch1[2].seq, ...
// The bracket number is auto-incremented: saving a channel always writes
// the next free index. sdStoreScanTrackCounters() (called in setup before
// the main loop) inspects the card and finds the highest existing
// "chN[K].seq" index per channel so saves keep incrementing after reboot.
// Loading a channel cycles through its saved tracks: the first load
// loads the highest saved track, subsequent loads load track 1, 2, ...
// up to the highest track, then loop back to track 1 again.
// -----------------------------------------------------------------------------

// Initialize the SD card and create the /sequencore folder if needed.
// Safe to call repeatedly: the Teensy SD library auto-initializes on the
// first successful begin and supports the card being inserted later.
bool sdStoreInit();

// Scan the /sequencore folder and set g_sdTrackCounter[N] to the next
// free track index for each channel (1 if the card has no tracks for
// that channel). Call once in setup, after sdStoreInit, before the
// main loop.
void sdStoreScanTrackCounters();

// Save the given channel's lane pattern to the next free
// /sequencore/ch<N>[K].seq track.
// Returns true on success.
bool sdStoreSaveChannel(uint8_t channel);

// Load the given channel's highest saved track from the SD card
// into the running sequence.
// Returns true on success (file exists, valid header, full read).
bool sdStoreLoadChannel(uint8_t channel);

// Next track index to save for each channel (1-based).
// Set by sdStoreScanTrackCounters(); bumped after each successful save.
extern uint8_t g_sdTrackCounter[kMidiChannelCount];