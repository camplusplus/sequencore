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
- MIDI record sequence (press right column control buttons to record/overdub)
- Set Tempo (pads Session+/Note-)
- Added Modifier key (Custom)
- Pressing Modifier key (Custom) should toggle modes (also colors change)
- Pausing/Delete playback per midi channel (Custom to toggle mode: green=mute, red=delete and select right row channel)
- Displaying steps with recorded notes
- Step Editor: Record from hardware MIDI — while holding a record button touching a note-grid pad records the last hardware keyboard note/velocity onto that step.
- Step Editor delete/mute note by pressing Modifier key and then sequence step. Hold longer for column mute.
- Shuffle: in Modifier green mode, pad 91 increases and pad 92 decreases the shuffle of the last pressed channel.

ToDo:
- sequence length
- glide, microsteps, ratchet
- Song mode
- Save to mircosd card

  <img width="1834" height="616" alt="17868286194283894539707099595966" src="https://github.com/user-attachments/assets/77bb5b08-e9ed-4d48-aa5c-3f16cf06b46b" />

Connect Launchpad to Teensy host USB, but provide powered USB hub (or connect +5v from edge connectors)

<img width="576" height="455" alt="image" src="https://github.com/user-attachments/assets/c4a9c182-ecb2-443c-afbf-27ad333ed44b" />

Build and upload from the project folder with:
~/.platformio/penv/bin/pio run -e teensy41 -t upload -t nobuild

Debug with:
pio device monitor

Used code for initialization of Launchpad from DrivenByMoss project.
Launchpad X is property of Novation
