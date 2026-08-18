#include "sequencer.h"

#include "app_state.h"
#include "launchpad.h"

// -----------------------------------------------------------------------------
// Timing
// -----------------------------------------------------------------------------

uint16_t calculateStepDurationMs()
{
  return static_cast<uint16_t>(
      60000.0f /
      static_cast<float>(g_tempoBpm) /
      4.0f /
      static_cast<float>(g_microstepDivisions));
}

uint16_t calculateClockPulseMs()
{
  return static_cast<uint16_t>(
      60000.0f /
      static_cast<float>(g_tempoBpm) /
      24.0f);
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
    midiPort.sendNoteOn(
        note,
        velocity,
        midiChannel);
  }
  else
  {
    midiPort.sendNoteOff(
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

  midiPort.sendClock();
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
    g_sequence[step][channel].active = false;
  }
}

void deleteChannel(uint8_t channel)
{
  if (channel >= kMidiChannelCount)
  {
    return;
  }

  for (uint8_t step = 0;
       step < kStepCount;
       ++step)
  {
    g_sequence[step][channel].active = false;
  }

  // A deleted channel is no longer muted.
  g_channelMuteMask &= ~(1U << channel);

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

  lane.active = true;
  lane.note = note;
  lane.velocity = velocity;

  refreshLaunchpadGridLedState();
}

// -----------------------------------------------------------------------------
// Playback
// -----------------------------------------------------------------------------

void sendActiveStepNotes(uint8_t step)
{
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

    if (!lane.active)
    {
      continue;
    }

    const uint16_t swingDelayUs =
        (step % 2 == 1 && g_swingPct > 0)
            ? (calculateStepDurationMs() * g_swingPct / 100U) *
                  1000U / 2U
            : 0;

    if (swingDelayUs > 0)
    {
      delayMicroseconds(swingDelayUs);
    }

    for (uint8_t ratchet = 0;
         ratchet < g_ratchetCount;
         ++ratchet)
    {
      sendMidiMessage(
          channel,
          lane.note,
          lane.velocity,
          true);

      sendMidiMessage(
          channel,
          lane.note,
          0,
          false);

      if (ratchet + 1 < g_ratchetCount)
      {
        delayMicroseconds(
            static_cast<uint32_t>(
                calculateStepDurationMs()) *
            1000U /
            g_ratchetCount);
      }
    }
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

    // Muted channels were never played back.
    if (g_channelMuteMask & (1U << channel))
    {
      continue;
    }

    if (lane.active)
    {
      sendMidiMessage(
          channel,
          lane.note,
          0,
          false);
    }
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
      (g_stepIndex + 1) % kStepCount;

  g_hasPlayedStep = true;
}

void playAllChannelSequence()
{
  // Temporarily stop the sequencer.
  const bool wasRunning = g_running;
  g_running = false;

  for (uint8_t step = 0;
       step < kStepCount;
       ++step)
  {
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

      if (lane.active)
      {
        sendMidiMessage(
            channel,
            lane.note,
            lane.velocity,
            true);

        sendMidiMessage(
            channel,
            lane.note,
            0,
            false);
      }
    }

    const uint16_t stepDuration =
        calculateStepDurationMs();

    delay(stepDuration);
  }

  // Restore sequencer state.
  g_running = wasRunning;

  if (g_running)
  {
    midiPort.sendStart();
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

  // We only record MIDI notes while holding
  // a Launchpad recording-channel button.
  if (!g_recordingHeldNote)
  {
    return;
  }

  // ---------------------------------------------------------------------------
  // NOTE ON
  // ---------------------------------------------------------------------------

  if (type == midi::NoteOn && data2 > 0)
  {
    const uint8_t recordChannel =
        g_recordingChannelOffset +
        g_channelOffset;

    recordCurrentStep(
        recordChannel,
        data1,
        data2);

    return;
  }

  // ---------------------------------------------------------------------------
  // NOTE OFF
  // ---------------------------------------------------------------------------

  if (type == midi::NoteOff ||
      (type == midi::NoteOn && data2 == 0))
  {
    // Do not modify the sequence on NoteOff.
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

  Serial.print(" micro=");
  Serial.print(g_microstepDivisions);

  Serial.print(" swing=");
  Serial.print(g_swingPct);

  Serial.print(" ratchet=");
  Serial.print(g_ratchetCount);

  Serial.print(" step=");
  Serial.println(g_stepIndex);
}