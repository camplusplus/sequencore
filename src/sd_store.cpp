#include "sd_store.h"

#include "app_state.h"

#include <SD.h>
#include <string.h>

// -----------------------------------------------------------------------------
// On-disk layout
//
//   /sequencore/ch<N>[K].seq   (N = 0..15, K = track number)
//
// Every file is exactly kSequenceBinarySize bytes:
//
//   byte 0    kSequenceFileMagic
//   byte 1    kSequenceFileVersion
//   byte 2    channel number
//   byte 3    sequence length (steps written)
//   bytes 4.. the lane data for g_sequence[0..g_sequenceLength-1][channel]
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
} // namespace

uint8_t g_sdTrackCounter[kMidiChannelCount] = {1};

// -----------------------------------------------------------------------------
// Filename parsing: "chN[K].seq"
// -----------------------------------------------------------------------------

// Returns true if `name` is a track file, and on success writes the channel
// (N) and track index (K) to outChannel / outTrack.
static bool parseTrackFilename(
    const char *name,
    uint8_t *outChannel,
    uint8_t *outTrack)
{
  // "ch" prefix.
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
  // Defaults: next free track is 1 for every channel.
  for (uint8_t c = 0; c < kMidiChannelCount; ++c)
  {
    g_sdTrackCounter[c] = 1;
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

  // Walk every entry and remember the highest "chN[K].seq" track index K
  // per channel N. The next save for channel N goes to K + 1.
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

  // Load the highest saved track for this channel.
  uint8_t track = 0;
  for (uint8_t t = 1; t < g_sdTrackCounter[channel]; ++t)
  {
    char probe[64];
    buildChannelTrackPath(probe, channel, t);
    if (SD.exists(probe))
    {
      track = t;
    }
  }

  if (track == 0)
  {
    return false;
  }

  char path[64];
  buildChannelTrackPath(path, channel, track);

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