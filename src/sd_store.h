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
//
// Auto tracks are stored as /sequencore/autoch<N>[K].seq
// (N = 0..15, K = track number). At startup, before the main loop, the
// firmware checks the last increment K present on the card for each
// channel and records every existing autochN[1..K].seq (only files that
// actually exist); a channel can have an unlimited number of auto tracks.
// The patterns themselves stay on the card and are read when a track
// starts.
// While the sequencer is running the loaded files are played in
// succession: each channel cycles through its existing files (ascending
// K, gaps skipped) and each file plays for exactly kAutoTrackSteps (16)
// steps, then wraps back to the first file - a single existing file
// loops forever. While any auto track is loaded the sequence length is
// held at kAutoTrackSteps.
// -----------------------------------------------------------------------------

// Number of steps in one auto track pattern.
constexpr uint8_t kAutoTrackSteps = 16;

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

// Called in setup after sdStoreScanTrackCounters(): for each channel,
// check the last increment K on the card of "autochN[K].seq" and record
// every existing autochN[1..K].seq (ascending K; gaps are skipped). The
// patterns stay on the card and are read when a track starts.
// For every channel that has auto tracks, g_sequence is primed with the
// first existing file and the sequence length is set to kAutoTrackSteps.
void sdStoreLoadAutoTracks();

// Advances the auto-track playback one step. Call after
// advanceSequencerStep() while the sequencer is running: when the
// 16-step bar wraps (g_stepIndex back to 0), each channel's lane is
// replaced by the next existing auto track file (ascending K, gaps
// skipped), wrapping back to the first file after the last one, so the
// files play in succession, each for 16 steps. A channel with a single
// existing file loops that file forever.
void sdStorePlayAutoTracks();

// Next track index to save for each channel (1-based).
// Set by sdStoreScanTrackCounters(); bumped after each successful save.
extern uint8_t g_sdTrackCounter[kMidiChannelCount];