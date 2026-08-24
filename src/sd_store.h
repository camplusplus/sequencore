#pragma once

#include <Arduino.h>

// -----------------------------------------------------------------------------
// SD card storage (Teensy 4.1 built-in SD controller)
//
// All files live in the /sequencore folder on the SD card root.
// Channel patterns are stored as /sequencore/ch<N>.seq (N = 0..15),
// one file per MIDI channel lane (all steps, substep slots included).
// -----------------------------------------------------------------------------

// Initialize the SD card and create the /sequencore folder if needed.
// Safe to call repeatedly: the Teensy SD library auto-initializes on the
// first successful begin and supports the card being inserted later.
bool sdStoreInit();

// Save the given channel's lane pattern to /sequencore/ch<N>.seq.
// Returns true on success.
bool sdStoreSaveChannel(uint8_t channel);

// Load the given channel's lane pattern from /sequencore/ch<N>.seq.
// Returns true on success (file exists, valid header, full read).
bool sdStoreLoadChannel(uint8_t channel);