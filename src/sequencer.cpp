#include "sequencer.h"

#include "app_state.h"
#include "launchpad.h"
#include "sd_store.h"

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
// MIDI clock hardware timer
// -----------------------------------------------------------------------------

namespace
{
  // Runs at interrupt level, at 24ppqn, independent of loop() timing.
  // Safe to call unconditionally: sendStepClockPulse() no-ops when stopped.
  void onMidiClockTimerTick()
  {
    sendStepClockPulse();
  }
}

void startMidiClockTimer()
{
  g_midiClockTimer.begin(onMidiClockTimerTick, calculateClockPulseUs());
}

void restartMidiClockTimer()
{
  g_midiClockTimer.update(calculateClockPulseUs());
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

  // When a clock arrives from MIDI IN, that stream is the source: its pulses
  // are forwarded on arrival (see handleHardwareMidiIn), so do not emit a
  // second, competing pulse from the PIT timer.
  if (g_midiInClockActive)
  {
    return;
  }

  midiOutSendClock();
}

// Whether the incoming stream is still acting as the clock: a pulse has to
// have arrived within the stop-gap window. A stream that went silent (or
// has not sent a pulse yet) cannot drive the transport, so the resume paths
// hand the clock back to the PIT timer instead of waiting on pulses that
// will never arrive.
bool inputClockIsLive()
{
  if (!g_midiInClockActive || g_midiInClockPulseGapMs == 0)
  {
    return false;
  }

  return (millis() - g_midiInClockLastPulseMs) <=
      (uint32_t)g_midiInClockPulseGapMs * kMidiInClockStopGapFactor;
}

// -----------------------------------------------------------------------------
// Incoming clock: stop when its source stops
// -----------------------------------------------------------------------------

// The incoming stream is the clock source, so a source that pauses has to
// pause the sequencer too - otherwise the pattern keeps advancing against a
// clock that no longer ticks and the two drift apart once it resumes.
void stopForMissingClockPulses()
{
  if (!g_midiInClockActive || !g_running || g_midiInClockPulseGapMs == 0)
  {
    return;
  }

  // One whole step of silence (six pulse intervals at the tempo the pulses
  // are actually arriving at) reads as the source pausing rather than
  // hiccuping: a dropped pulse is only two intervals.
  if ((millis() - g_midiInClockLastPulseMs) <=
      (uint32_t)g_midiInClockPulseGapMs * kMidiInClockStopGapFactor)
  {
    return;
  }

  // The same stop the play/pause pad performs, so the outputs see a pause and
  // the pad goes dark rather than a silent, still-running transport.
  g_running = false;
  g_userPaused = true;

  releaseSoundingNotes();
  closeLaunchpadEchoSession();
  midiOutSendStop();
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

    lane.chordActiveMask = 0;
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

    lane.chordActiveMask = 0;
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
  lane.chordActiveMask = 0;
  lane.substep[0].note = note;
  lane.substep[0].velocity = velocity;
  lane.muted = false;

  refreshLaunchpadGridLedState();
}

void adjustChannelSwing(uint8_t channel, int8_t delta)
{
  if (channel >= kMidiChannelCount)
  {
    return;
  }

  const int16_t newSwing =
      static_cast<int16_t>(g_channelSwing[channel]) + delta;

  if (newSwing < 0)
  {
    g_channelSwing[channel] = 0;
  }
  else if (newSwing > kSwingMax)
  {
    g_channelSwing[channel] = kSwingMax;
  }
  else
  {
    g_channelSwing[channel] = static_cast<uint8_t>(newSwing);
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

    // Just live-recorded ahead of the playhead: the hit was already
    // heard via MIDI Thru, so skip this one playback.
    if (g_recordSkipStep[channel] == step)
    {
      g_recordSkipStep[channel] = 0xFF;
      continue;
    }

    // Only slot 0 is recorded so far, so the lane is active
    // iff slot 0 is active.
    if (!lane.isSubstepActive() ||
        lane.muted)
    {
      continue;
    }

    if (step % 2 == 1 && g_channelSwing[channel] > 0)
    {
      // At the maximum setting the offbeat shifts by half a step, giving
      // the classic 75% MPC ratio for this channel.
      const uint32_t swingDelayUs =
          (calculateStepDurationMs() * g_channelSwing[channel] / 100U) *
          1000U;
      delayMicroseconds(swingDelayUs);
    }

    if (lane.hasChordNotes())
    {
      sendMidiMessage(
          channel,
          lane.substep[0].note,
          lane.substep[0].velocity,
          true);

      for (uint8_t i = 0; i < kChordNoteMax; ++i)
      {
        if (lane.isChordNoteActive(i))
        {
          sendMidiMessage(
              channel,
              lane.chordNote[i],
              lane.chordVelocity[i],
              true);
        }
      }

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

    if (lane.hasChordNotes())
    {
      sendMidiMessage(
          channel,
          lane.substep[0].note,
          0,
          false);

      for (uint8_t i = 0; i < kChordNoteMax; ++i)
      {
        if (lane.isChordNoteActive(i))
        {
          sendMidiMessage(
              channel,
              lane.chordNote[i],
              0,
              false);
        }
      }

      continue;
    }

    // Hold the gate for a full step duration. The held note is tracked the
    // same way as substep notes and released by suppressPendingSubstepNotes()
    // before the next step starts, so the note-off always matches the note
    // that was actually played, even if the lane is edited, muted or
    // replaced (auto tracks) while it is sounding.
    if (lane.substep[0].active)
    {
      sendMidiMessage(
          channel,
          lane.substep[0].note,
          lane.substep[0].velocity,
          true);
      g_lastPlayedSubstepNote[channel] = lane.substep[0].note;
      g_lastPlayedSubstepVelocity[channel] = lane.substep[0].velocity;
      g_lastPlayedSubstepActive[channel] = true;
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

    // Muted channels/cells were never played back.
    if (g_channelMuteMask & (1U << channel))
    {
      continue;
    }

    // Chord lanes hold every chord tone until the step boundary.
    if (lane.hasChordNotes())
    {
      sendMidiMessage(
          channel,
          lane.substep[0].note,
          0,
          false);

      for (uint8_t i = 0; i < kChordNoteMax; ++i)
      {
        if (lane.isChordNoteActive(i))
        {
          sendMidiMessage(
              channel,
              lane.chordNote[i],
              0,
              false);
        }
      }

      continue;
    }

    // Single main notes and substep-grid lanes are released via
    // suppressPendingSubstepNotes() in advanceSequencerStep() instead,
    // which sends the note-off for the note actually held.
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

// Stop-and-clear: the same release advanceSequencerStep() does at a step
// boundary, applied when the transport is stopped instead. The note-off for
// a step is normally sent by the *next* step, which never arrives while
// paused, so without this the last played note (and any held chord tones or
// substep note) keeps ringing for as long as the transport is paused.
void releaseSoundingNotes()
{
  if (!g_hasPlayedStep)
  {
    return;
  }

  suppressLastStepNotes(g_lastPlayedStep);
  suppressPendingSubstepNotes();
}

void advanceSequencerStep()
{
  if (g_hasPlayedStep)
  {
    suppressLastStepNotes(g_lastPlayedStep);
    suppressPendingSubstepNotes();
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

    // Holding a Launchpad recording-channel button arms recording, but the
    // lane is the channel the note arrived on, not the button's lane: a
    // TR-8 on MIDI channel 8 records into lane 8, so its notes land where
    // they are played back, whatever the button (or the scroll offset) says.
    if (g_recordingHeldNote)
    {
      recordCurrentStep(
          channel,
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

  // ---------------------------------------------------------------------------
  // MIDI CLOCK (F8) arriving from the DIN input
  // ---------------------------------------------------------------------------

  if (type == midi::Clock)
  {
    // The incoming stream is the clock source: the Teensy stops emitting
    // its own PIT pulses (see sendStepClockPulse) and the sequencer
    // advances from the pulses that arrive here instead.
    g_midiInClockActive = true;

    // Forward and count only while the transport runs, mirroring the
    // PIT clock's behavior over a pause.
    if (!g_running)
    {
      return;
    }

    // Remember where the pulse fell: the loop watches the silence that
    // follows it (see stopForMissingClockPulses). A first pulse, or the long
    // gap since a pause or a start, has no usable predecessor to measure
    // against, so fall back to the tempo the Launchpad shows.
    const uint32_t pulseMs = millis();
    const uint32_t gapMs = pulseMs - g_midiInClockLastPulseMs;
    g_midiInClockLastPulseMs = pulseMs;

    g_midiInClockPulseGapMs =
        (gapMs > 0 && gapMs < 1000)
            ? (uint16_t)gapMs
            : (uint16_t)(calculateClockPulseUs() / 1000UL);

    // A start armed over a pause opens the pattern on this pulse instead of
    // at the moment the pad was pressed, so step 0 begins exactly on a pulse
    // of the clock's source and every step boundary lands on one after it.
    // START travels with it so the outputs begin together too.
    const bool startsStep = g_midiInClockAwaitingStart;
    if (startsStep)
    {
      g_midiInClockAwaitingStart = false;
      g_midiInClockPulseCount = 0;
      midiOutSendStart();
    }

    midiOutSendClock();

    // The pulse that opens a step is not counted towards the next one; the
    // six that follow it are (24ppqn: six pulses per 16th-note step).
    if (!startsStep)
    {
      if (++g_midiInClockPulseCount < kMidiInClockPulsesPerStep)
      {
        return;
      }
    }

    g_midiInClockPulseCount = 0;

    // The same step advance the main loop performs on its own timer.
    g_stepTimer = 0;
    advanceSequencerStep();
    sdStorePlayAutoTracks();
    refreshLaunchpadGridLedState();

    return;
  }

  // ---------------------------------------------------------------------------
  // MIDI START (FA) / STOP (FC) from the input source
  // ---------------------------------------------------------------------------

  // The source also announces its transport boundaries, and they are the only
  // exact anchors available: a start is where its step 0 is, a stop is where
  // its pattern ends. Without them the pulse phase is arbitrary and the two
  // play different steps at the same time.

  if (type == midi::Start)
  {
    // Forward it so the outputs see the same start.
    midiOutSendStart();

    // Restart the transport, but keep the mode that was in effect before the
    // pause: a stop (explicit or a silent source) never switches play-only vs
    // record, so a source that restarts while we were recording resumes
    // recording, with any lane-pad capture still armed. The pad's pause ->
    // play transition always lands in play only; the source's does not.
    g_running = true;
    g_userPaused = false;

    g_stepIndex = 0;
    g_stepTimer = 0;
    g_substepIndex = 0;
    memset(g_recordSkipStep, 0xFF, sizeof(g_recordSkipStep));
    g_hasPlayedStep = false;

    if (inputClockIsLive())
    {
      // A start is a step boundary, so count from here: the pulse that opens
      // step 0 is the next one, which is where the source's step 0 begins.
      g_midiInClockAwaitingStart = false;
      g_midiInClockPulseCount =
          static_cast<uint8_t>(kMidiInClockPulsesPerStep - 1);
      g_midiInClockLastPulseMs = millis();
    }
    else
    {
      // The stream is not ticking: START arrived before any pulse (or the
      // source sends no clock at all), or it went silent over the pause.
      // Waiting for a pulse that may never come would leave the transport
      // stopped, so the PIT clock drives it and the step opens on the START
      // itself, like the pad's pause -> play transition.
      g_midiInClockActive = false;
      g_midiInClockAwaitingStart = false;
      g_midiInClockPulseCount = 0;
      restartMidiClockTimer();
      advanceSequencerStep();
    }

    sdStorePlayAutoTracks();
    refreshLaunchpadGridLedState();

    return;
  }

  if (type == midi::Stop)
  {
    // Only meaningful once the input stream is the clock.
    if (!g_midiInClockActive)
    {
      return;
    }

    // The source ended its pattern: pause the transport, like a source that
    // goes silent does (see stopForMissingClockPulses).
    g_running = false;
    g_userPaused = true;

    releaseSoundingNotes();
    closeLaunchpadEchoSession();
    midiOutSendStop();

    return;
  }

  // ---------------------------------------------------------------------------
  // AFTERTOUCH (pass-through)
  // ---------------------------------------------------------------------------

  // A pressure-sensitive keyboard keeps sending while a key is held down.
  // Nothing in a sequence carries pressure, so these messages are not
  // recorded - they are forwarded straight to the six outputs on the channel
  // they arrived on, which is what a hardware THRU would do.
  if (type == midi::AfterTouchChannel)
  {
    midiOutSendAfterTouch(data1, channel);
    return;
  }

  if (type == midi::AfterTouchPoly)
  {
    midiOutSendPolyAfterTouch(data1, data2, channel);
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
  Serial.print(g_channelSwing[g_lastPressedChannel]);

  Serial.print(" step=");
  Serial.println(g_stepIndex);
}