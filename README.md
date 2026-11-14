Teensy 4.1 as headless sequencer, Launchpad X as the sequencer controller, and MIDI IN/OUT to your instruments 

<img width="961" height="1280" alt="17867357828422494826715409083845" src="https://github.com/user-attachments/assets/b23e96f6-c196-454c-96cf-b7f39c36b469" />

[![Watch Short](https://img.youtube.com/vi/Y_C1o1M-Pfo/maxresdefault.jpg)](https://www.youtube.com/shorts/Y_C1o1M-Pfo)   
https://youtube.com/shorts/Y_C1o1M-Pfo

Progress:
- All grid buttons mapped
- All Control Buttons mapped
- Lights and color of buttons mapped
- Scroll upper, lower midi channels mapped
- Scroll left, right note steps mapped
- MIDI clock as hardware interrupt timer
- MIDI record sequence (press right column control buttons to record/overdub)
- Set Tempo (pads Session+/Note-)
- Added Modifier key (Custom)
- Pressing Modifier key (Custom) should toggle modes (also colors change)
- Pausing/Delete playback per midi channel (Custom to toggle mode: green=mute, red=delete and select right row channel)
- Displaying steps with recorded notes
- Step Editor: Record from hardware MIDI — while holding a record button touching a note-grid pad records the last hardware keyboard note/velocity onto that step.
- Step Editor delete/mute note by pressing Modifier key and then sequence step. Hold longer for column mute.
- Shuffle: in Modifier green mode, pad up increases and pad down decreases the shuffle of the last pressed channel.
- Sequence length: in Modifier green mode, pad left decreases and pad right increases the sequence length (4..64 steps, default 16).
- Teensy 4.1 hardware UART TX pins: Serial1 TX = pin 1, Serial2 TX = pin 8, Serial3 TX = pin 14, Serial4 TX = pin 17, Serial5 TX = pin 20, Serial6 TX = pin 24. Use any or all.
- Microsteps: Editing step when longer pressed on step without modifier button. Add or delete note same as steps: (record + press note to add it; red mode to delete it). Exit by any of other row note.
- Save to microSD card: use two functions modes when pressed "Custom" function pad: 1. Save (amber), 2.Load (blue). Files are named ch1.seq - ch6.seq in folder sequencore. To save or load just press Channel pad on the right in appropriate function mode. 
- Multiple tracks can be saved and loaded and they will have auto increment in brackets: ch1[1].seq. Loading will load last saved sequence first then loop from begining.
- Autoload tracks named autochN[K].seq on start (N = channel, K = track number). Each existing file plays for 16 steps, then the next K is loaded and played, looping back to [1] after the last file (only existing files are cycled). For example, with autoch1[1].seq through autoch1[5].seq on the card, autoch1[5].seq plays after 4 loops of the first file.
- Chord edit mode (blue modifier + long-press a grid pad with note) uses four pages to add up to 31 higher notes; selected chord notes play together with the base note
- Launchpad-only note mode: the 6th Modifier state (white) puts the Launchpad X back into its native Live/Note Mode (SysEx 0E 00, 00 01, 15 01) so the grid pads supply the pitch themselves — you can record a sequence with no hardware MIDI keyboard. Select the channel first with the right-column record buttons (same channel the hardware keyboard records into, shifted by the scroll offset); once in note mode recording is automatic, no button has to be held. Stop the transport first with a quick tap of the top-row play/stop pad (98) and it stays paused through note mode — pads keep recording into the sequence and each pad touch also sounds live on the selected channel so you can hear what you record (the transport itself stays put; selecting a record channel no longer restarts a transport you stopped on purpose). Pads write into successive steps of that channel. Exit by **press-and-hold the top-row play/pause pad (98)** (~700 ms, `kNoteModeExitHoldMs`): the hold leaves note mode, returns the modifier to the off state and restores Programmer Mode (0E 01) — a quick tap of that same pad still just toggles play/pause (it is resolved on release, which is what lets one pad carry both meanings), and the bottom-right note pad (which reports as pitch 0 in Live Mode) is now ignored. The other top-row function pads keep their native Live-Mode behaviour (tempo, scroll) handled by the Launchpad itself.
- Aftertouch: a pressure-sensitive hardware keyboard is passed through untouched — 0xD0 (channel/monophonic) and 0xA0 (polyphonic, one held note) arriving on the DIN input are forwarded to all six DIN outputs on the channel they arrived on and are never recorded into a sequence. Aftertouch arriving from the Launchpad over USB is remapped to the recording lane while in note mode (Live Mode reports its pads on channel 2, which would put pressure one lane over) and passed through on its own channel otherwise. In paused note mode the pad's velocity is additionally reused as aftertouch on the lane just sounded — poly for the touched pitch plus one lane-wide value — and the release answers it with pressure 0, so nothing keeps a stale squeeze. The X's grid pads are not pressure-sensitive, so that last part is synthesised dynamics rather than real pressure.

  <img width="1834" height="616" alt="17868286194283894539707099595966" src="https://github.com/user-attachments/assets/77bb5b08-e9ed-4d48-aa5c-3f16cf06b46b" />

Connect Launchpad to Teensy host USB, but provide powered USB hub (or connect +5v from edge connectors)

<img width="576" height="455" alt="image" src="https://github.com/user-attachments/assets/c4a9c182-ecb2-443c-afbf-27ad333ed44b" />

Build and upload from the project folder with:
~/.platformio/penv/bin/pio run -e teensy41 -t upload -t nobuild

Debug with:
pio device monitor

Used code for initialization of Launchpad from DrivenByMoss project.
Launchpad X is property of Novation
