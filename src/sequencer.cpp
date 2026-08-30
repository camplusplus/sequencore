#include "sequencer.h"

#include "app_state.h"
#include "launchpad.h"

namespace
{
  // True if the lane has any substep programmed beyond slot 0 (the main
  // note). Such lanes are played on the divided 8-slot substep grid
  // (see handleSubstepPlayback) instead of as a single immediate note
  // at the step boundary.
  bool laneHasExtraSubsteps(const StepLaneState &lane)
  {
    for (uint8_t k = 1; k < kMicrostepMax; ++k)
    {
      if (lane.substep[k].active)
      {
        return true;
      }
    }
    return false;
  }

  // Release a channel's currently-held substep note, if any.
  void releaseHeldSubstepNote(uint8_t channel)
  {
    if (g_lastPlayedSubstepActive[channel])
    {
      sendMidiMessage(
          channel,
          g_lastPlayedSubstepNote[channel],
          0,
          false);
      g_lastPlayedSubstepActive[channel] = false;
    }
  }
}

// -----------------------------------------------------------------------------
// Timing
// -----------------------------------------------------------------------------

// Duration of one step (a 16th note at the current tempo).
// Microsteps only subdivide this step into equal slots; the
// step itself keeps this full length.
uint16_t calculateStepDurationMs()
{
  return static_cast<uint16_t>(
      60000.0f /
      static_cast<float>(g_tempoBpm) /
      4.0f);
}

// Duration of one MIDI clock pulse (µs) at the current tempo.
// MIDI clock uses 24 pulses per beat, so this is
// (60,000,000 µs per minute) / BPM / 24.
// Integer µs math avoids the millisecond truncation error.
uint32_t calculateClockPulseUs()
{
  return 60000000UL / g_tempoBpm / 24UL;
}

// -----------------------------------------------------------------------------
// MIDI output
// -----------------------------------------------------------------------------

void sendMidiMessage(
    byte channel,
    byte note,
    byte velocity,
    bool noteOn)
{
  if (channel >= 16)
  {
    return;
  }

  // Internal channel = 0..15
  // MIDI channel = 1..16
  const byte midiChannel = channel + 1;

  if (noteOn)
  {
    midiOutSendNoteOn(
        note,
        velocity,
        midiChannel);
  }
  else
  {
    midiOutSendNoteOff(
        note,
        velocity,
        midiChannel);
  }
}

void sendStepClockPulse()
{
  if (!g_running)
  {
    return;
  }

  midiOutSendClock();
}

// -----------------------------------------------------------------------------
// Sequence editing
// -----------------------------------------------------------------------------

void clearCurrentStep(uint8_t step)
{
  for (uint8_t channel = 0;
       channel < kMidiChannelCount;
       ++channel)
  {
    StepLaneState &lane = g_sequence[step][channel];

    for (uint8_t k = 0; k < kMicrostepMax; ++k)
    {
      lane.substep[k].active = false;
    }

    lane.muted = false;
  }
}

void deleteChannel(uint8_t channel)
{
  if (channel >= kMidiChannelCount)
  {
    return;
  }

  for (uint8_t step = 0;
       step < kMaxSequenceLength;
       ++step)
  {
    StepLaneState &lane = g_sequence[step][channel];

    for (uint8_t k = 0; k < kMicrostepMax; ++k)
    {
      lane.substep[k].active = false;
    }

    lane.muted = false;
  }

  // A deleted channel is no longer muted.
  g_channelMuteMask &= ~(1U << channel);

  // Don't leave a substep note stuck on if this channel was mid-way
  // through playing one when it got deleted.
  releaseHeldSubstepNote(channel);

  refreshLaunchpadGridLedState();
}

void recordCurrentStep(
    uint8_t channel,
    byte note,
    byte velocity)
{
  if (channel >= kMidiChannelCount)
  {
    return;
  }

  // Only record NoteOn.
  // NoteOff must not erase the recorded step.
  if (velocity == 0)
  {
    return;
  }

  // Writing to the lane replaces only this channel's step at the
  // current step index. Steps of other channels in the same column
  // must stay intact.
  StepLaneState &lane =
      g_sequence[g_stepIndex][channel];

  lane.substep[0].active = true;
  lane.substep[0].note = note;
  lane.substep[0].velocity = velocity;
  lane.muted = false;

  refreshLaunchpadGridLedState();
}

void adjustChannelShuffle(uint8_t channel, int8_t delta)
{
  if (channel >= kMidiChannelCount)
  {
    return;
  }

  const int16_t newShuffle =
      static_cast<int16_t>(g_channelShuffle[channel]) + delta;

  if (newShuffle < 0)
  {
    g_channelShuffle[channel] = 0;
  }
  else if (newShuffle > kShuffleMax)
  {
    g_channelShuffle[channel] = kShuffleMax;
  }
  else
  {
    g_channelShuffle[channel] = static_cast<uint8_t>(newShuffle);
  }
}

void adjustSequenceLength(int8_t delta)
{
  const int16_t newLength =
      static_cast<int16_t>(g_sequenceLength) + delta;

  if (newLength < kMinSequenceLength)
  {
    g_sequenceLength = kMinSequenceLength;
  }
  else if (newLength > kMaxSequenceLength)
  {
    g_sequenceLength = kMaxSequenceLength;
  }
  else
  {
    g_sequenceLength = static_cast<uint8_t>(newLength);
  }

  // Keep the step offset within the visible range.
  if (g_stepOffset + 8 > g_sequenceLength)
  {
    g_stepOffset = 0;
  }

  // Clear mute flags on steps that are no longer part of the sequence.
  for (uint8_t step = g_sequenceLength;
       step < kMaxSequenceLength;
       ++step)
  {
    g_stepMuted[step] = false;
  }
}

// -----------------------------------------------------------------------------
// Playback
// -----------------------------------------------------------------------------

void sendActiveStepNotes(uint8_t step)
{
  // Steps muted via the green modifier mode are not played.
  if (step < g_sequenceLength &&
      g_stepMuted[step])
  {
    return;
  }

  for (uint8_t channel = 0;
       channel < kMidiChannelCount;
       ++channel)
  {
    StepLaneState &lane =
        g_sequence[step][channel];

    // Muted channels are not played back.
    if (g_channelMuteMask & (1U << channel))
    {
      continue;
    }

    // Only slot 0 is recorded so far, so the lane is active
    // iff slot 0 is active.
    if (!lane.isSubstepActive() ||
        lane.muted)
    {
      continue;
    }

    if (laneHasExtraSubsteps(lane))
    {
      // This lane uses more than just the main note: the step is
      // divided into kMicrostepMax equal slots (a 16th note split
      // into 8ths). Fire slot 0 now and hold it; later slots are
      // fired by handleSubstepPlayback() as their scheduled time
      // within the step arrives.
      byte note, velocity;
      if (lane.getSubstep(0, &note, &velocity))
      {
        sendMidiMessage(channel, note, velocity, true);
        g_lastPlayedSubstepNote[channel] = note;
        g_lastPlayedSubstepVelocity[channel] = velocity;
        g_lastPlayedSubstepActive[channel] = true;
      }
      continue;
    }

    // Swing and per-channel shuffle both delay odd steps.
    // Shuffle is set per channel (green mode pads 91/92).
    const uint16_t swingDelayUs =
        (step % 2 == 1 && g_swingPct > 0)
            ? (calculateStepDurationMs() * g_swingPct / 100U) *
                  1000U / 2U
            : 0;

    const uint16_t shuffleDelayUs =
        (step % 2 == 1 && g_channelShuffle[channel] > 0)
            ? (calculateStepDurationMs() *
               g_channelShuffle[channel] / 100U) *
                  1000U / 2U
            : 0;

    const uint16_t oddStepDelayUs =
        swingDelayUs + shuffleDelayUs;

    if (oddStepDelayUs > 0)
    {
      delayMicroseconds(oddStepDelayUs);
    }

    sendMidiMessage(
        channel,
        lane.substep[0].note,
        lane.substep[0].velocity,
        true);

    sendMidiMessage(
        channel,
        lane.substep[0].note,
        0,
        false);
  }
}

void suppressLastStepNotes(uint8_t step)
{
  for (uint8_t channel = 0;
       channel < kMidiChannelCount;
       ++channel)
  {
    StepLaneState &lane =
        g_sequence[step][channel];

    // Muted channels/cells were never played back.
    if (g_channelMuteMask & (1U << channel))
    {
      continue;
    }

    // Substep-grid lanes are released via suppressPendingSubstepNotes()
    // in advanceSequencerStep() instead, since the note actually held
    // may not be slot 0's note.
    if (laneHasExtraSubsteps(lane))
    {
      continue;
    }

    // Substep slots (k > 0) are played as immediate note-on/note-off
    // pairs, so only the main note (slot 0) needs to be suppressed.
    if (lane.substep[0].active && !lane.muted)
    {
      sendMidiMessage(
          channel,
          lane.substep[0].note,
          0,
          false);
    }
  }
}

// Releases any substep note still held over from the previous step,
// for every channel. Called right before a new step starts so nothing
// is left stuck on if a lane's last active slot wasn't slot 7.
static void suppressPendingSubstepNotes()
{
  for (uint8_t channel = 0; channel < kMidiChannelCount; ++channel)
  {
    releaseHeldSubstepNote(channel);
  }
}

void advanceSequencerStep()
{
  if (g_hasPlayedStep)
  {
    suppressLastStepNotes(g_lastPlayedStep);
  }

  sendActiveStepNotes(g_stepIndex);

  g_lastPlayedStep = g_stepIndex;
  g_stepIndex =
      (g_stepIndex + 1) % g_sequenceLength;

  g_hasPlayedStep = true;

  // Reset the substep scheduler for the step that just started
  // (g_lastPlayedStep, since g_stepIndex has now moved to the next one).
  g_substepIndex = 0;
  g_substepStepStartMs = millis();
}

// Called every loop(). Advances g_substepIndex as time passes within the
// current step and fires slot 1..7 for any lane that uses the substep
// grid (laneHasExtraSubsteps). Lanes with only slot 0 are untouched here
// since they were already fired-and-released in sendActiveStepNotes().
void handleSubstepPlayback()
{
  if (!g_running || !g_hasPlayedStep)
  {
    return;
  }

  const uint16_t stepMs = calculateStepDurationMs();
  const uint16_t slotMs = stepMs / kMicrostepDivisionsDefault;

  if (slotMs == 0)
  {
    return;
  }

  const uint32_t elapsedMs = millis() - g_substepStepStartMs;
  uint8_t targetIndex = static_cast<uint8_t>(elapsedMs / slotMs);

  if (targetIndex >= kMicrostepDivisionsDefault)
  {
    targetIndex = kMicrostepDivisionsDefault - 1;
  }

  while (g_substepIndex < targetIndex)
  {
    ++g_substepIndex;

    if (g_stepMuted[g_lastPlayedStep])
    {
      continue;
    }

    for (uint8_t channel = 0; channel < kMidiChannelCount; ++channel)
    {
      if (g_channelMuteMask & (1U << channel))
      {
        continue;
      }

      StepLaneState &lane = g_sequence[g_lastPlayedStep][channel];

      if (lane.muted || !laneHasExtraSubsteps(lane))
      {
        continue;
      }

      // Release whatever this channel was holding from the previous slot.
      releaseHeldSubstepNote(channel);

      byte note, velocity;
      if (lane.getSubstep(g_substepIndex, &note, &velocity))
      {
        sendMidiMessage(channel, note, velocity, true);
        g_lastPlayedSubstepNote[channel] = note;
        g_lastPlayedSubstepVelocity[channel] = velocity;
        g_lastPlayedSubstepActive[channel] = true;
      }
    }
  }
}

// -----------------------------------------------------------------------------
// Hardware MIDI input / recording
// -----------------------------------------------------------------------------

void handleHardwareMidiIn()
{
  const byte type = midiPort.getType();
  const byte channel = midiPort.getChannel();
  const byte data1 = midiPort.getData1();
  const byte data2 = midiPort.getData2();

  (void)channel;

  // ---------------------------------------------------------------------------
  // NOTE ON
  // ---------------------------------------------------------------------------

  if (type == midi::NoteOn && data2 > 0)
  {
    // Remember the last hardware keyboard note so grid pads can
    // record it while holding the record button.
    g_lastHwNote = data1;
    g_lastHwVelocity = data2;

    if (g_hwNotesHeld < 32)
    {
      ++g_hwNotesHeld;
    }

    // We only record into the current step while holding
    // a Launchpad recording-channel button.
    if (g_recordingHeldNote)
    {
      const uint8_t recordChannel =
          g_recordingChannelOffset +
          g_channelOffset;

      recordCurrentStep(
          recordChannel,
          data1,
          data2);
    }

    return;
  }

  // ---------------------------------------------------------------------------
  // NOTE OFF
  // ---------------------------------------------------------------------------

  if (type == midi::NoteOff ||
      (type == midi::NoteOn && data2 == 0))
  {
    // Track held notes so grid pads know if the hardware
    // keyboard is playing. Do not modify the sequence.
    if (g_hwNotesHeld > 0)
    {
      --g_hwNotesHeld;
    }

    return;
  }
}

// -----------------------------------------------------------------------------
// Debug
// -----------------------------------------------------------------------------

void debugPrintState()
{
  Serial.print("tempo=");
  Serial.print(g_tempoBpm);

  Serial.print(" swing=");
  Serial.print(g_swingPct);

  Serial.print(" step=");
  Serial.println(g_stepIndex);
}