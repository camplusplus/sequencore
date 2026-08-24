#include "sd_store.h"

#include "app_state.h"

#include <SD.h>
#include <string.h>

// -----------------------------------------------------------------------------
// On-disk layout
//
//   /sequencore/ch<N>.seq   (N = 0..15, one file per MIDI channel lane)
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

static void buildChannelPath(char *path, uint8_t channel)
{
  snprintf(
      path,
      64,
      "%s/ch%u.seq",
      kRootDirPath,
      static_cast<unsigned>(channel));
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

  char path[64];
  buildChannelPath(path, channel);

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
    // g_sequence is [kMaxSequenceLength][kMidiChannelCount]; we want
    // g_sequence[step][channel] for step = 0..g_sequenceLength-1, which is
    // contiguous in memory because the second index (channel) is the
    // fastest-varying one inside the innermost step.
    // We must copy per step since the channel is the outer dimension here.
    for (uint8_t s = 0; s < g_sequenceLength && ok; ++s)
    {
      ok = f.write(&g_sequence[s][channel], sizeof(StepLaneState));
    }
  }

  f.close();

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

  char path[64];
  buildChannelPath(path, channel);

  File f = SD.open(path, FILE_READ);
  if (!f)
  {
    return false;
  }

  uint8_t header[kSequenceHeaderSize] = {0, 0, 0, 0};
  if (f.read(header, sizeof(header)) != sizeof(header))
  {
    f.close();
    return false;
  }

  f.close();

  if (header[0] != kSequenceFileMagic ||
      header[1] != kSequenceFileVersion ||
      header[2] != channel)
  {
    return false;
  }

  const uint8_t lenOnDisk = header[3];
  if (lenOnDisk < kMinSequenceLength ||
      lenOnDisk > kMaxSequenceLength)
  {
    return false;
  }

  // Reopen for the lane payload.
  File f2 = SD.open(path, FILE_READ);
  if (!f2)
  {
    return false;
  }

  if (f2.read(header, sizeof(header)) != sizeof(header))
  {
    f2.close();
    return false;
  }

  bool ok = true;
  for (uint8_t s = 0; s < lenOnDisk && ok; ++s)
  {
    StepLaneState lane;
    if (f2.read(&lane, sizeof(StepLaneState)) !=
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

  f2.close();
  return ok;
}