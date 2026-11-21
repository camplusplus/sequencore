Teensy 4.1 as headless sequencer, Launchpad X as the sequencer controller, and MIDI IN/OUT to your instruments 

<img width="961" height="1280" alt="17867357828422494826715409083845" src="https://github.com/user-attachments/assets/b23e96f6-c196-454c-96cf-b7f39c36b469" />

[![Watch Short](https://img.youtube.com/vi/Y_C1o1M-Pfo/maxresdefault.jpg)](https://www.youtube.com/shorts/Y_C1o1M-Pfo)   
https://youtube.com/shorts/Y_C1o1M-Pfo

## Modes

### Transport (top-row play/pause pad 98)
Tapping pad 98 cycles three transport modes:
- **Record** (pad 98 red) — the running state: notes arriving from the hardware keyboard and grid-pad edits are captured into the sequence.
- **Play only** (pad 98 white) — the sequence sounds but nothing is recorded, so an external keyboard can play along without its notes being captured; right-column pads keep selecting lanes without arming recording.
- **Pause** (pad 98 off) — the transport is stopped; stopping releases anything still sounding, so a pause cuts the last step short instead of sustaining it.

Long-pressing a right-column record button enters note mode from any transport mode; a quick tap of pad 98 always keeps its transport meaning.

### Modifier cycle (top-row pad 97, "Custom")
Each press advances the modifier mode; pad 97 shows the current colour: off → **green** → **red** → **amber** → **blue**.
- **Green — mute.** Grid: short touch mutes/unmutes that cell, long hold mutes/unmutes the whole step column. Right column: toggle mute per channel. Pads 91/92 raise/lower the swing (shuffle) of the last selected channel; pads 93/94 shorten/lengthen the sequence by 8 steps (4..64, default 16).
- **Red — delete.** Grid: touch deletes the cell's note. Right column: delete the whole channel lane.
- **Amber — save.** Right column: save the channel lane to the SD card.
- **Blue — load.** Right column: load a saved channel lane from the SD card. Blue modifier + long-press a grid pad that has a note also enters chord edit (see below).

### Step editor (hardware keyboard)
While a right-column record button is held (shorter than the note-mode window), touching a grid pad records the last hardware MIDI keyboard note/velocity onto that step; touching a step that already has a note deletes it (toggle). The last note played in Launchpad note mode is remembered in the same place (see Note mode), so after leaving note mode this editor can re-place it on any step.

### Note mode (Launchpad-only recording, no keyboard needed)
- **Enter:** long-press a right-column record button (19, 29, …, 89) (~700 ms, `kNoteModeEnterHoldMs`). The Launchpad X returns to its native Live/Note Mode (SysEx 0E 00, 00 01, 15 01) so the grid pads supply the pitch themselves. The button the gesture is pressed on selects the recording channel (same lane the hardware keyboard records into, shifted by the scroll offset); the press already armed the lane, so notes played after the switch land in it, and once in note mode recording is automatic — no button has to be held.
- **Recording:** pads write into successive steps of that channel with a time-based cursor: it advances by whole step durations since the last touch, so pads touched a beat apart land a beat apart and the steps in between stay empty, while pads touched within one step duration stack as a chord on the base note already in that lane (a lower touch re-bases, so chords work in any touch order). The first touch anchors the sequence at step 0. When an external MIDI clock drives the transport (see MIDI clock source), the cursor follows the transport's playhead instead of wall time, so a touch lands on the step the transport is quantizing to — the same place a live hit from the hardware keyboard would be written — and the first touch anchors at the playhead instead of step 0. Each pad touch also sounds live on the selected channel in every transport mode, so you can hear what you record.
- **Pause first (optional):** stop the transport with a quick tap of pad 98 before entering and it stays paused through note mode — pads keep recording and still sound live, but the transport stays put.
- **Inside note mode:** right-column pads are inert, pad 97 stays white (note mode is not a modifier state), the other top-row pads keep their native Live-Mode behaviour (tempo, scroll) handled by the Launchpad itself, and the bottom-right pad reports as pitch 0 and is ignored.
- **Exit:** long-press the top-row play/pause pad 98 (~1.2 s, `kNoteModeExitHoldMs`): leaves note mode, returns the modifier to the off state and restores Programmer Mode (0E 01). A quick tap of a right-column button always keeps its normal channel-select meaning, and a quick tap of pad 98 keeps its transport meanings — the exit gesture fires only once the hold window is reached.
- **Last note remembered:** the last pad touched in note mode is remembered as the last played note, exactly like the last hardware MIDI IN note. After the exit, hold a right-column record button and touch a grid pad (the step editor) and that note is added to the touched step — so a note played on the Launchpad's own grid can be moved or duplicated anywhere in the sequence. Only touches that were actually recorded into the sequence update this: in play-only (white) mode the pads still sound live but the remembered note stays the last one that was recorded. Until a note has actually been recorded — from the hardware keyboard or a recorded note-mode touch — the step and microstep editors stay inert instead of writing an empty cell that would light a pad but play back as silence.

### Microstep edit
Long-press a grid pad (no modifier): the row shows the 8 substep slots of that step — slot 0 is the main note (white), higher slots are extra substeps (yellow), empty slots stay off. Add or delete notes as with ordinary steps (record + press note to add, red mode to delete); exit by touching a pad in another row.

### Chord edit
Blue modifier + long-press a grid pad with a recorded note: the row becomes an 8-note chord picker across four pages, adding up to 31 notes above the base note; selected chord notes play together with the base note and the lane's grid pad shows blue while chord notes are active.

### LED legends
- **Grid pads:** white = recorded note, blue = chord notes active, yellow = extra microstep substeps active, green = muted (cell or whole column), off = empty or beyond the sequence length.
- **Right column:** amber = held for the note-mode gesture, green = muted, yellow = has recorded steps, white = empty.
- **Top row:** pad 97 = modifier colour (white while note mode runs), pad 98 = transport (off/white/red, amber while the exit hold counts), scroll/tempo pads white when available and amber at the limit (green in green mode).

## Other features
- MIDI record sequence: press right-column control buttons to record/overdub; all grid and control buttons are mapped, upper/lower MIDI channels scroll and left/right note steps scroll. Holding one arms recording only: notes arriving on the DIN MIDI IN land in the lane of the channel they arrived on, not the button's lane — a TR‑8 on MIDI channel 8 records into lane 8 and is therefore played back on channel 8, whatever the button or the scroll offset says.
- Save/load on microSD: in amber (save) or blue (load) modifier mode press the channel pad on the right. Files are named ch1.seq … ch6.seq in folder `sequencore`; multiple tracks auto-increment as `ch1[1].seq`. Loading loads the last saved sequence first, then loops from the beginning.
- Autoload: tracks named `autochN[K].seq` (N = channel, K = track) are cycled from start-up — each existing file plays for 16 steps, then the next K is loaded and played, looping back to [1] after the last file.
- Aftertouch: a pressure-sensitive hardware keyboard is passed through untouched — 0xD0 (monophonic) and 0xA0 (polyphonic) arriving on the DIN input are forwarded to all six DIN outputs on the channel they arrived on and are never recorded. Aftertouch from the Launchpad over USB is remapped to the recording lane while in note mode (Live Mode reports its pads on channel 2) and passed through on its own channel otherwise; in note mode the pad's velocity is also reused as aftertouch on the lane just sounded (poly for the touched pitch plus one lane-wide value), answered with pressure 0 on release. The X's grid pads are not pressure-sensitive, so that last part is synthesised dynamics.
- MIDI clock source: by default the F8 pulses are the Teensy's own (PIT timer ISR, broadcast to every output). If a clock arrives on the DIN MIDI IN, that stream becomes the source instead: the first incoming F8 switches the source (one-way, until reboot), the Teensy stops emitting its own pulses, and each received pulse is forwarded and counted, advancing every `kMidiInClockPulsesPerStep` pulses (6 at 24 ppqn). Starting over an incoming clock is a sync start — play arms the pattern and the next F8 opens step 0, so every step boundary lands on a pulse of the source. A whole step of silence (six pulse intervals at the tempo the pulses actually arrive at) pauses the sequencer with the source, and an incoming Start (FA) restarts at step 0 while an incoming Stop (FC) pauses it, so step position follows the source exactly.
- Teensy 4.1 hardware UART TX pins: Serial1 = pin 1, Serial2 = pin 8, Serial3 = pin 14, Serial4 = pin 17, Serial5 = pin 20, Serial6 = pin 24. Use any or all.

  <img width="1834" height="616" alt="17868286194283894539707099595966" src="https://github.com/user-attachments/assets/77bb5b08-e9ed-4d48-aa5c-3f16cf06b46b" />

## Setup
Connect the Launchpad to the Teensy host USB, but provide a powered USB hub (or connect +5v from edge connectors).

<img width="576" height="455" alt="image" src="https://github.com/user-attachments/assets/c4a9c182-ecb2-443c-afbf-27ad333ed44b" />

Build and upload from the project folder with:
```
pio run -e teensy41 -t upload || pio run -e teensy41 -t upload
# no build
~/.platformio/penv/bin/pio run -e teensy41 -t upload -t nobuild
```

Debug with:
```
pio device monitor
```

Used code for initialization of Launchpad from DrivenByMoss project.
Launchpad X is property of Novation.
