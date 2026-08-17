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

ToDo:
- Quantization, Functions for rest control buttons
- Pausing/mute playback per midi channel (pad Custom + Right control pad)
- Record new pattern (delete old, not overdub)
- simple display on note pads, internal led bpm sync
- Save patterns to mircosd card
- Step Editor, microsteps, ratchet, sequence length
- song mode

  <img width="1834" height="616" alt="17868286194283894539707099595966" src="https://github.com/user-attachments/assets/77bb5b08-e9ed-4d48-aa5c-3f16cf06b46b" />

Connect Launchpad to Teensy host USB, but provide powered USB hub (or connect +5v from edge connectors)

<img width="576" height="455" alt="image" src="https://github.com/user-attachments/assets/c4a9c182-ecb2-443c-afbf-27ad333ed44b" />

Build and upload from the project folder with:
~/.platformio/penv/bin/platformio run -e teensy41 -t upload

Debug with:
pio device monitor

Used code for initialization of Launchpad from DrivenByMoss project.
Launchpad X is property of Novation
