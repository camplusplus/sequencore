#include <Arduino.h>

#include "app_state.h"
#include "launchpad.h"
#include "sequencer.h"

void setup()
{
  pinMode(LED_BUILTIN, OUTPUT);
  digitalWriteFast(LED_BUILTIN, LOW);

  // ---------------------------------------------------------------------------
  // Hardware DIN MIDI
  // ---------------------------------------------------------------------------

  midiPort.begin(MIDI_CHANNEL_OMNI);

  // ---------------------------------------------------------------------------
  // Launchpad callbacks
  // ---------------------------------------------------------------------------

  launchpad.setHandleNoteOn(onLaunchpadNoteOn);
  launchpad.setHandleNoteOff(onLaunchpadNoteOff);
  launchpad.setHandleControlChange(onLaunchpadControlChange);

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
  // Green-mode grid long-press: hold -> mute whole step
  // ---------------------------------------------------------------------------

  if (g_gridHoldActive)
  {
    if (!g_gridHoldTriggered &&
        (millis() - g_gridHoldStartMs) >= kGridHoldMs)
    {
      g_gridHoldTriggered = true;

      // Toggle mute for the whole step.
      g_stepMuteMask ^= (1U << g_gridHoldStep);

      refreshLaunchpadGridLedState();
    }
  }

  // ---------------------------------------------------------------------------
  // Hardware MIDI
  // ---------------------------------------------------------------------------

  if (midiPort.read())
  {
    handleHardwareMidiIn();
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

    refreshLaunchpadGridLedState();
  }

  // ---------------------------------------------------------------------------
  // Built-in LED heartbeat
  // ---------------------------------------------------------------------------

  if (g_ledTimer >= 250)
  {
    g_ledTimer = 0;

    digitalWriteFast(
        LED_BUILTIN,
        !digitalReadFast(LED_BUILTIN));
  }
}