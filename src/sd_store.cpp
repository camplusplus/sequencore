#include "sd_store.h"

#include "app_state.h"

#include <SD.h>
#include <string.h>

// -----------------------------------------------------------------------------
// On-disk layout
//
//   /sequencore/ch<N>[K].seq          (N = 0..15, K = track number)
//   /sequencore/autoch<N>[K].seq      (N = 0..15, K = 1..kMaxAutoTracksPerChannel)
//
// ch files hold manually saved channel patterns (one per track index).
//
// autoch files are auto-track patterns. They are never written by the
// firmware. In setup, before the main loop, the firmware checks the last
// increment K present on the card for each channel and loads every existing
// autochN[1..K].seq pattern into RAM. While the sequencer runs, the loaded
// files are played in succession: each channel cycles through its existing
// files (ascending K, gaps skipped), each file plays for exactly
// kAutoTrackSteps (16) steps, then wraps back to the first file (a channel
// with a single existing file loops it forever).
//
// Every file is a small header followed by the lane data:
//
//   byte 0    kSequenceFileMagic
//   byte 1    kSequenceFileVersion
//   byte 2    channel number
//   byte 3    sequence length (steps written)
//   bytes 4.. the lane data for the steps
//
// StepLaneState / SubstepNote are flat POD structs with no padding, so the
// raw struct bytes can be copied straight to/from disk.
// -----------------------------------------------------------------------------

namespace
{
constexpr uint8_t kSequenceFileMagic = 0x53;     // 'S'
constexpr uint8_t kSequenceFileVersion = 1;
constexpr uint8_t kSequenceHeaderSize = 4;
constexpr const char *kRootDirPath = "/sequencore";

// Last increment K found on the card for each channel's auto tracks
// ("autochN[K].seq"). 0 means no auto tracks for that channel.
// Set by sdStoreScanTrackCounters().
uint8_t g_sdAutoLastK[kMidiChannelCount] = {0};

// Auto track patterns loaded from the SD card into RAM in setup:
// g_sdAutoPattern[channel][track - 1][step].
StepLaneState g_sdAutoPattern[kMidiChannelCount][kMaxAutoTracksPerChannel][kAutoTrackSteps];

// Ascending list of existing auto track increments (K) per channel, built
// in setup. g_sdAutoCount[c] is the length of this list (0 = none).
uint8_t g_sdAutoKList[kMidiChannelCount][kMaxAutoTracksPerChannel] = {0};

// Number of existing auto track files loaded per channel (0 = none).
uint8_t g_sdAutoCount[kMidiChannelCount] = {0};

// Position (1-based) into g_sdAutoKList[c] of the auto track file currently
// loaded in g_sequence for channel c.
uint8_t g_sdAutoIndex[kMidiChannelCount] = {1};

// Per-channel position (1-based) into the ascending list of existing
// tracks that the next sdStoreLoadChannel() call should load.
// 0 means "not started yet" -> the first load grabs the last track.
uint8_t g_sdLoadCursor[kMidiChannelCount] = {0};
} // namespace

uint8_t g_sdTrackCounter[kMidiChannelCount] = {1};

// -----------------------------------------------------------------------------
// Filename parsing: "chN[K].seq" / "autochN[K].seq"
// -----------------------------------------------------------------------------

// Parses the "N[K]" part of a track filename (already positioned at N).
// Writes N to *outChannel and K to *outIndex; returns false on malformed
// input.
static bool parseChannelAndIndex(
    const char *name,
    uint8_t *outChannel,
    uint8_t *outIndex)
{
  // "ch" prefix (or "autoch", caller already skipped the "auto").
  if (name[0] != 'c' || name[1] != 'h')
  {
    return false;
  }

  uint8_t channel = 0;
  const char *p = name + 2;

  while (*p >= '0' && *p <= '9')
  {
    channel = static_cast<uint8_t>(
        channel * 10 + static_cast<uint8_t>(*p - '0'));
    ++p;
  }

  if (channel >= kMidiChannelCount)
  {
    return false;
  }

  // "[K]"
  if (*p != '[')
  {
    return false;
  }
  ++p;

  uint8_t track = 0;
  while (*p >= '0' && *p <= '9')
  {
    track = static_cast<uint8_t>(
        track * 10 + static_cast<uint8_t>(*p - '0'));
    ++p;
  }

  if (track == 0 || *p != ']')
  {
    return false;
  }
  ++p;

  // ".seq"
  if (strcmp(p, ".seq") != 0)
  {
    return false;
  }

  *outChannel = channel;
  *outIndex = track;
  return true;
}

// Returns true if `name` is a track file ("chN[K].seq"), and on success
// writes the channel (N) and track index (K) to outChannel / outTrack.
static bool parseTrackFilename(
    const char *name,
    uint8_t *outChannel,
    uint8_t *outTrack)
{
  return parseChannelAndIndex(name, outChannel, outTrack);
}

// Returns true if `name` is an auto-track file ("autochN[K].seq"), and on
// success writes the channel (N) and track increment (K, 1..kMaxAutoTracksPerChannel)
// to outChannel / outTrack.
static bool parseAutoTrackFilename(
    const char *name,
    uint8_t *outChannel,
    uint8_t *outTrack)
{
  if (strncmp(name, "auto", 4) != 0)
  {
    return false;
  }

  uint8_t channel = 0;
  uint8_t track = 0;
  if (!parseChannelAndIndex(name + 4, &channel, &track))
  {
    return false;
  }

  if (track < 1 || track > kMaxAutoTracksPerChannel)
  {
    return false;
  }

  *outChannel = channel;
  *outTrack = track;
  return true;
}

// -----------------------------------------------------------------------------
// Internal helpers
// -----------------------------------------------------------------------------

static bool sdIsReady()
{
  return SD.mediaPresent();
}

static bool ensureRootDir()
{
  if (!SD.exists(kRootDirPath))
  {
    if (!SD.mkdir(kRootDirPath))
    {
      return false;
    }
  }
  return true;
}

static void buildChannelTrackPath(
    char *path,
    uint8_t channel,
    uint8_t track)
{
  snprintf(
      path,
      64,
      "%s/ch%u[%u].seq",
      kRootDirPath,
      static_cast<unsigned>(channel),
      static_cast<unsigned>(track));
}

static void buildAutoTrackPath(
    char *path,
    uint8_t channel,
    uint8_t track)
{
  snprintf(
      path,
      64,
      "%s/autoch%u[%u].seq",
      kRootDirPath,
      static_cast<unsigned>(channel),
      static_cast<unsigned>(track));
}

// Loads a single "chN[K].seq" style file into the given channel's lane
// (steps beyond the on-disk length are cleared). Returns true on a full,
// valid read.
static bool loadChannelFile(
    const char *path,
    uint8_t channel)
{
  File f = SD.open(path, FILE_READ);
  if (!f)
  {
    return false;
  }

  uint8_t header[kSequenceHeaderSize] = {0, 0, 0, 0};
  if (f.read(header, sizeof(header)) !=
      static_cast<int>(sizeof(header)))
  {
    f.close();
    return false;
  }

  if (header[0] != kSequenceFileMagic ||
      header[1] != kSequenceFileVersion ||
      header[2] != channel)
  {
    f.close();
    return false;
  }

  const uint8_t lenOnDisk = header[3];
  if (lenOnDisk < kMinSequenceLength ||
      lenOnDisk > kMaxSequenceLength)
  {
    f.close();
    return false;
  }

  bool ok = true;
  for (uint8_t s = 0; s < lenOnDisk && ok; ++s)
  {
    StepLaneState lane;
    if (f.read(&lane, sizeof(StepLaneState)) !=
        static_cast<int>(sizeof(StepLaneState)))
    {
      ok = false;
      break;
    }
    g_sequence[s][channel] = lane;
  }

  // Keep any steps beyond the on-disk length cleared.
  for (uint8_t s = lenOnDisk; s < g_sequenceLength; ++s)
  {
    g_sequence[s][channel] = StepLaneState{};
  }

  f.close();
  return ok;
}

// Loads a single "autochN[K].seq" file into outLanes (kAutoTrackSteps lanes;
// steps beyond the on-disk length are cleared). Returns true on a full,
// valid read.
static bool loadAutoTrackFile(
    const char *path,
    uint8_t channel,
    StepLaneState *outLanes)
{
  File f = SD.open(path, FILE_READ);
  if (!f)
  {
    return false;
  }

  uint8_t header[kSequenceHeaderSize] = {0, 0, 0, 0};
  if (f.read(header, sizeof(header)) !=
      static_cast<int>(sizeof(header)))
  {
    f.close();
    return false;
  }

  if (header[0] != kSequenceFileMagic ||
      header[1] != kSequenceFileVersion ||
      header[2] != channel)
  {
    f.close();
    return false;
  }

  const uint8_t lenOnDisk = header[3];
  if (lenOnDisk < kMinSequenceLength ||
      lenOnDisk > kAutoTrackSteps)
  {
    f.close();
    return false;
  }

  bool ok = true;
  for (uint8_t s = 0; s < lenOnDisk && ok; ++s)
  {
    StepLaneState lane;
    if (f.read(&lane, sizeof(StepLaneState)) !=
        static_cast<int>(sizeof(StepLaneState)))
    {
      ok = false;
      break;
    }
    outLanes[s] = lane;
  }

  for (uint8_t s = lenOnDisk; s < kAutoTrackSteps; ++s)
  {
    outLanes[s] = StepLaneState{};
  }

  f.close();
  return ok;
}

// -----------------------------------------------------------------------------
// Public API
// -----------------------------------------------------------------------------

bool sdStoreInit()
{
  // On Teensy 4.1 the built-in SDIO slot is addressed by BUILTIN_SDCARD.
  // SD.begin() wraps SdFat and picks the SDIO bus for that pseudo-CS pin.
  // Safe to call repeatedly: a no-op if the card is already present.
  if (!SD.begin(BUILTIN_SDCARD))
  {
    // Card not present yet (user may insert one later); nothing to do.
    return false;
  }

  return ensureRootDir();
}

void sdStoreScanTrackCounters()
{
  // Defaults: next free track is 1 for every channel and no auto tracks.
  for (uint8_t c = 0; c < kMidiChannelCount; ++c)
  {
    g_sdTrackCounter[c] = 1;
    g_sdAutoLastK[c] = 0;
  }

  if (!sdIsReady())
  {
    return;
  }

  File dir = SD.open(kRootDirPath, FILE_READ);
  if (!dir)
  {
    return;
  }

  // Walk every entry, remember the next free "chN[K].seq" track index K per
  // channel N (the next save for channel N goes to max K + 1) and the last
  // increment K present on the card for each "autochN[K].seq" per channel N.
  for (File entry = dir.openNextFile(); entry;
       entry = dir.openNextFile())
  {
    // name() returns a heap buffer owned by this File object; copy the
    // string before we close the entry.
    char name[64];
    strncpy(name, entry.name(), sizeof(name) - 1);
    name[sizeof(name) - 1] = '\0';
    entry.close();

    uint8_t channel = 0;
    uint8_t track = 0;

    // Auto tracks: remember the last increment on the card.
    if (parseAutoTrackFilename(name, &channel, &track))
    {
      if (track > g_sdAutoLastK[channel])
      {
        g_sdAutoLastK[channel] = track;
      }
      continue;
    }

    if (!parseTrackFilename(name, &channel, &track))
    {
      continue;
    }

    // The counter holds the *next* index to write, so track K implies
    // the counter must be at least K + 1.
    if (track + 1 > g_sdTrackCounter[channel])
    {
      g_sdTrackCounter[channel] = static_cast<uint8_t>(track + 1);
    }
  }

  dir.close();
}

bool sdStoreSaveChannel(uint8_t channel)
{
  if (channel >= kMidiChannelCount)
  {
    return false;
  }

  if (!sdIsReady() || !ensureRootDir())
  {
    return false;
  }

  const uint8_t track = g_sdTrackCounter[channel];

  char path[64];
  buildChannelTrackPath(path, channel, track);

  File f = SD.open(path, FILE_WRITE);
  if (!f)
  {
    return false;
  }

  const uint8_t header[kSequenceHeaderSize] = {
      kSequenceFileMagic,
      kSequenceFileVersion,
      channel,
      g_sequenceLength,
  };

  bool ok = f.write(header, sizeof(header));

  if (ok)
  {
    // g_sequence is [kMaxSequenceLength][kMidiChannelCount]; a channel's
    // lanes are strided across steps, so each lane is written separately.
    for (uint8_t s = 0; s < g_sequenceLength && ok; ++s)
    {
      ok = f.write(&g_sequence[s][channel], sizeof(StepLaneState));
    }
  }

  f.close();

  if (ok)
  {
    // Advance to the next free track index for this channel.
    g_sdTrackCounter[channel] = static_cast<uint8_t>(track + 1);
  }

  return ok;
}

bool sdStoreLoadChannel(uint8_t channel)
{
  if (channel >= kMidiChannelCount)
  {
    return false;
  }

  if (!sdIsReady())
  {
    return false;
  }

  // Collect this channel's existing tracks in ascending order so we can
  // cycle through them by position.
  uint8_t existingTracks[255];
  uint8_t trackCount = 0;
  for (uint8_t t = 1; t < g_sdTrackCounter[channel]; ++t)
  {
    char probe[64];
    buildChannelTrackPath(probe, channel, t);
    if (SD.exists(probe))
    {
      existingTracks[trackCount++] = t;
    }
  }

  if (trackCount == 0)
  {
    return false;
  }

  // Position (1-based) into existingTracks for this load.
  uint8_t cursor = g_sdLoadCursor[channel];
  if (cursor == 0)
  {
    // First load after boot: use the highest saved track.
    cursor = trackCount;
  }
  else if (cursor > trackCount)
  {
    // Track list changed; restart from the first track.
    cursor = 1;
  }

  const uint8_t track = existingTracks[cursor - 1];

  char path[64];
  buildChannelTrackPath(path, channel, track);

  if (!loadChannelFile(path, channel))
  {
    return false;
  }

  // Advance the cursor for the next load: after the highest track,
  // wrap back to the first track.
  g_sdLoadCursor[channel] = (cursor == trackCount)
                                ? 1
                                : static_cast<uint8_t>(cursor + 1);

  return true;
}

void sdStoreLoadAutoTracks()
{
  bool anyChannelHasAutoTracks = false;

  for (uint8_t c = 0; c < kMidiChannelCount; ++c)
  {
    // Files are auto-incremented, so probe every increment up to the last
    // one found on the card. Only the files that exist (and load validly)
    // are recorded; playback cycles through exactly these, so a channel
    // with a single existing file loops that file forever.
    const uint8_t cap = g_sdAutoLastK[c];
    g_sdAutoCount[c] = 0;
    g_sdAutoIndex[c] = 1;

    for (uint8_t k = 1; k <= cap; ++k)
    {
      char path[64];
      buildAutoTrackPath(path, c, k);

      // k is 1-based; g_sdAutoPattern is indexed 0-based by (k - 1).
      if (sdIsReady() &&
          SD.exists(path) &&
          loadAutoTrackFile(path, c, g_sdAutoPattern[c][k - 1]))
      {
        g_sdAutoKList[c][g_sdAutoCount[c]++] = k;
      }
    }

    if (g_sdAutoCount[c] == 0)
    {
      continue;
    }

    anyChannelHasAutoTracks = true;

    // Prime the first existing file so it is live in g_sequence (and
    // visible on the grid) as soon as the sequencer starts running.
    const uint8_t k0 = g_sdAutoKList[c][0];
    for (uint8_t s = 0; s < kAutoTrackSteps; ++s)
    {
      g_sequence[s][c] = g_sdAutoPattern[c][k0 - 1][s];
    }
  }

  if (anyChannelHasAutoTracks)
  {
    g_sequenceLength = kAutoTrackSteps;
  }
}

void sdStorePlayAutoTracks()
{
  // Advance the files only right after the last step (kAutoTrackSteps - 1)
  // of a 16-step bar has been played and the step index wrapped to 0.
  if (!g_running ||
      g_lastPlayedStep != kAutoTrackSteps - 1 ||
      g_stepIndex != 0)
  {
    return;
  }

  bool anyChannelHasAutoTracks = false;

  for (uint8_t c = 0; c < kMidiChannelCount; ++c)
  {
    if (g_sdAutoCount[c] == 0)
    {
      continue;
    }

    anyChannelHasAutoTracks = true;

    // Advance to the next existing file, wrapping back to the first after
    // the last. With a single existing file the same file is reloaded, so
    // it loops until more files appear on the card.
    const uint8_t next =
        static_cast<uint8_t>(g_sdAutoIndex[c] % g_sdAutoCount[c] + 1);
    g_sdAutoIndex[c] = next;

    const uint8_t k = g_sdAutoKList[c][next - 1];
    for (uint8_t s = 0; s < kAutoTrackSteps; ++s)
    {
      g_sequence[s][c] = g_sdAutoPattern[c][k - 1][s];
    }
  }

  if (anyChannelHasAutoTracks)
  {
    g_sequenceLength = kAutoTrackSteps;
  }
}