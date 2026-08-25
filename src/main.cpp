#include <Arduino.h>

#include "app_state.h"
#include "launchpad.h"
#include "sd_store.h"
#include "sequencer.h"

void setup()
{
  pinMode(LED_BUILTIN, OUTPUT);
  digitalWriteFast(LED_BUILTIN, LOW);

  // ---------------------------------------------------------------------------
  // Hardware DIN MIDI
  // ---------------------------------------------------------------------------

  midiPort.begin(MIDI_CHANNEL_OMNI);

  // Begin all six DIN MIDI output ports so the same data is
  // broadcast to every output pin.
  midiOutBeginAll(MIDI_CHANNEL_OMNI);

  // ---------------------------------------------------------------------------
  // Launchpad callbacks
  // ---------------------------------------------------------------------------

  launchpad.setHandleNoteOn(onLaunchpadNoteOn);
  launchpad.setHandleNoteOff(onLaunchpadNoteOff);
  launchpad.setHandleControlChange(onLaunchpadControlChange);

  // ---------------------------------------------------------------------------
  // SD card (built-in Teensy 4.1 SDIO slot)
  // ---------------------------------------------------------------------------

  sdStoreInit();
  sdStoreScanTrackCounters();
  sdStoreLoadAutoTracks();

  // ---------------------------------------------------------------------------
  // USB host
  // ---------------------------------------------------------------------------

  myusb.begin();

  delay(500);

  sendLaunchpadProgramMode();

  delay(250);

  refreshLaunchpadControlLedState();
  refreshLaunchpadGridLedState();
}

void loop()
{
  // ---------------------------------------------------------------------------
  // USB
  // ---------------------------------------------------------------------------

  myusb.Task();
  launchpad.read();

  // ---------------------------------------------------------------------------
  // Microstep edit: long-press a grid pad -> open substep editing
  // ---------------------------------------------------------------------------

  handleMicrostepEditHold();

  // ---------------------------------------------------------------------------
  // Substep playback: fire slots 1..7 for lanes using the substep grid
  // ---------------------------------------------------------------------------

  handleSubstepPlayback();

  // ---------------------------------------------------------------------------
  // Green-mode grid long-press: hold -> mute whole step
  // ---------------------------------------------------------------------------

  if (g_gridHoldActive)
  {
    if (!g_gridHoldTriggered &&
        (millis() - g_gridHoldStartMs) >= kGridHoldMs)
    {
      g_gridHoldTriggered = true;

      // Toggle mute for the whole step.
      if (g_gridHoldStep < g_sequenceLength)
      {
        g_stepMuted[g_gridHoldStep] = !g_stepMuted[g_gridHoldStep];
      }

      refreshLaunchpadGridLedState();
    }
  }

  // ---------------------------------------------------------------------------
  // Hardware MIDI
  // ---------------------------------------------------------------------------

  if (midiPort.read())
  {
    handleHardwareMidiIn();

    // Flash the built-in LED on any incoming MIDI message.
    g_ledFlashTimer = 0;
    digitalWriteFast(LED_BUILTIN, HIGH);
  }

  // ---------------------------------------------------------------------------
  // Launchpad LEDs
  // ---------------------------------------------------------------------------

  refreshLaunchpadControlLedState();

  // ---------------------------------------------------------------------------
  // MIDI clock
  // ---------------------------------------------------------------------------

  if (g_running &&
      g_clockPulseTimer >= calculateClockPulseMs())
  {
    g_clockPulseTimer = 0;

    sendStepClockPulse();
  }

  // ---------------------------------------------------------------------------
  // Sequencer
  // ---------------------------------------------------------------------------

  if (g_running &&
      g_stepTimer >= calculateStepDurationMs())
  {
    g_stepTimer = 0;

    advanceSequencerStep();

    // Auto tracks (autochN[K].seq): the files loaded in setup are played in
    // succession; when a 16-step bar wraps, each channel's lane is replaced
    // by the next existing file (ascending K, gaps skipped, then back to
    // the first file - a single existing file loops forever).
    sdStorePlayAutoTracks();

    refreshLaunchpadGridLedState();
  }

  // ---------------------------------------------------------------------------
  // Built-in LED: auto-clear the MIDI input flash
  // ---------------------------------------------------------------------------

  if (g_ledFlashTimer >= kLedFlashMs)
  {
    g_ledFlashTimer = 0;

    digitalWriteFast(LED_BUILTIN, LOW);
  }
}