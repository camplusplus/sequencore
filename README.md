Launchpad X as the sequencer controller, and MIDI DIN/DOUT to your instruments 

Flashing the Teensy
Use the Teensy’s programming USB connector only to upload firmware.
Build and upload from the project folder with:
/home/cam/.platformio/penv/bin/platformio run -e teensy41 -t upload
Then disconnect the PC and run the sequencer standalone.
Launchpad X connection
The Launchpad X should be connected to the Teensy’s USB Host path, not to the MIDI UART pins.
In your code in src/main.cpp, the Launchpad is handled through USBHost_t36.
So the Launchpad is a USB device that the Teensy reads as a host.
Hardware MIDI connection
Your sketch is already set up for:
Serial1 TX = Teensy pin 1 = MIDI OUT
Serial1 RX = Teensy pin 0 = MIDI IN
Recommended wiring:

MIDI OUT from Teensy to instruments

Teensy pin 1 (TX) -> 220 Ω resistor -> MIDI DIN pin 4
Teensy GND -> MIDI DIN pin 5
Note: this should be through the standard MIDI output interface circuit, not direct wire-only TX.
MIDI IN from external device

MIDI DIN pin 4 -> standard MIDI IN optocoupler circuit -> Teensy pin 0 (RX)
MIDI DIN pin 5 -> Teensy GND
Use a proper 6N138-style MIDI input network.
Ready-to-wire connection summary
Launchpad X:
plug into the Teensy USB host port/host path
MIDI OUT:
Teensy pin 1 -> MIDI OUT DIN connector
MIDI IN:
Teensy pin 0 <- MIDI IN DIN connector with optocoupler circuit
Power/GND:
common GND to the MIDI DIN circuit and Teensy GND
Important “one USB only” limitation
Because the Teensy 4.1 only has one normal USB programming connector, you normally use that connector for:
flashing only
Then, for normal standalone operation:

plug the Launchpad X into the host-side USB path
use the MIDI DIN/DOUT pins to talk to your instruments
So the intended standalone workflow is:

program board over USB
disconnect from the PC
connect Launchpad X to the host USB path
use the MIDI OUT pins to send note/clock to instruments


Read [](file:///home/cam/Documents/PlatformIO/Projects/sequencore), lines 1 to 200

Read [](file:///home/cam/.config/Code/agentSessionData/c1b11a90-fd6d-43db-a644-e43803436d34/attachments/19a86bce-058d-4567-abaf-53cb283be113/file_README), lines 1 to 50
Completed with input: {
  "path": "/home/cam/.config/Code/agentSessionData/c1b11a90-fd6d-43db-a644-e43803436d34/attachments/19a86bce-058d-4567-abaf-53cb283be113/file_README",
  "view_range": [
    1,
    50
  ]
}

Searched for `Launchpad|MIDI|USBHost|Serial1|pin 0|pin 1`

Here is a clean copy/paste wiring diagram you can use for the Teensy 4.1 + Launchpad X + hardware MIDI setup.

1) USB side
- Launchpad X: connect to the Teensy USB Host port / USB Host path.
- Teensy programming port: use this only for flashing via PlatformIO.
- If you only have one physical USB connector on the board, you will need to unplug the Launchpad before upload, or use a USB hub/host adapter path that lets the Launchpad live on the host side while the PC still programs via the device port.

2) MIDI hardware wiring
The sketch in [src/main.cpp](/home/cam/Documents/PlatformIO/Projects/sequencore/src/main.cpp) uses:
- Serial1 TX = pin 1 = MIDI OUT
- Serial1 RX = pin 0 = MIDI IN

Use a real MIDI IN/OUT circuit, not a direct UART wire.

Copy/paste diagram:

```text
Teensy 4.1
----------
USB programming port  -> PC for flashing/uploading
Launchpad X USB      -> Teensy USB Host path (Launchpad control surface)

MIDI OUT (to external instruments)
----------------------------------
Teensy 4.1 pin 1 / TX1  ---- 220 ohm ----> DIN-5 pin 4
Teensy 4.1 GND         -------------------> DIN-5 pin 2

MIDI IN (from external instrument)
----------------------------------
DIN-5 pin 5  ---> 220 ohm ---> 6N138/6N137 LED anode
DIN-5 pin 4  ---> 6N138/6N137 LED cathode
6N138 pin 5  -> Teensy GND
6N138 pin 8  -> +3.3V (or +5V on breakout board, depending board design)
6N138 pin 6  -> Teensy RX1 / pin 0  (through the opto output stage / resistor network)

Note:
- Do not connect Serial1 RX directly to a MIDI DIN socket.
- Use a proper opto-isolated MIDI IN circuit.
- Use a proper MIDI OUT resistor network.
```

3) Practical build recommendation
For a reliable build, use:
- 1x 5-pin DIN female jack for MIDI IN
- 1x 5-pin DIN female jack for MIDI OUT
- 1x 6N138 or 6N137 optocoupler module or a proven MIDI IN breakout
- 220Ω resistor in the MIDI OUT path
- 220Ω resistor in the MIDI IN LED path
- proper pull-up / output resistor network on the optocoupler side

4) In plain “ready to solder” form
If you want the shortest version:

```text
Teensy 4.1:
  pin 1  -> MIDI OUT jack through 220R
  pin 0  -> MIDI IN opto-isolated receiver input
  GND    -> MIDI OUT jack ground and opto ground

Launchpad X:
  USB -> Teensy USB Host path

External MIDI:
  OUT jack -> instrument MIDI IN
  IN jack  <- instrument MIDI OUT
```

5) Important note about recording
Yes — with this architecture, the sequencer can:
- receive Launchpad X commands over USB Host
- send MIDI notes out on the hardware MIDI OUT
- receive incoming hardware MIDI notes on MIDI IN
- record notes into the step sequencer and play them back

If you want, I can now give you:
- a one-page “solder this on perfboard” version, or
- a fully labeled schematic for the 6N138 MIDI IN + TX1 MIDI OUT circuit.


Yes — MIDI is a 5 V current-loop standard, but that does not mean “connect 5 V straight to the Teensy.”

The safe rule is:

- MIDI OUT socket: 5 V current loop is expected on the cable side.
- MIDI IN socket: electrically isolated from the microcontroller side.
- Teensy 4.1 pins: 3.3 V logic only.

So the answer is:

- The 5 V on the MIDI DIN/DOUT side is fine.
- The Teensy must not be connected to that 5 V line directly.
- You must use:
  - a proper MIDI OUT driver circuit for the TX pin
  - a proper opto-isolated MIDI IN circuit for the RX pin

What is safe:

```text
Teensy 4.1 pin 1 (TX1)  -> MIDI OUT interface -> 5-pin DIN OUT jack
Teensy 4.1 pin 0 (RX1)  -> opto-isolated MIDI IN receiver -> 5-pin DIN IN jack
```

Do not do this:

```text
Teensy pin 0/1 directly to MIDI DIN socket
```

That is not standards-compliant and can damage the Teensy or create a bad signal.

Recommended ready-to-wire version:

```text
MIDI OUT (to instruments)
-------------------------
Teensy 4.1 pin 1 (TX1) ---- 220Ω ----> DIN pin 4
Teensy 4.1 GND ----------------------> DIN pin 2

MIDI IN (from instruments)
-------------------------
DIN pin 5 ----> optocoupler LED side (through 220Ω)
DIN pin 4 ----> optocoupler LED side
optocoupler output ----> Teensy 4.1 pin 0 (RX1) through proper logic stage
optocoupler power/GND ----> 5V/GND on the MIDI breakout board or isolated board
```

Important detail:
- The MIDI OUT is not “TTL level out”; it is a current loop that uses a resistor and a 5 V source.
- The MIDI IN side uses an optocoupler, so the 5 V on the cable side is isolated from the Teensy.

Practical recommendation:
- Buy a tiny ready-made 5-pin DIN MIDI IN/OUT breakout board, or
- use a known MIDI shield/module that already implements the proper resistor/opto-isolation circuit.

If you use a ready-made MIDI breakout, then:
- yes, the 5 V side is okay
- no, you do not feed that 5 V into the Teensy directly
- yes, the Teensy’s 3.3 V UART is fine as long as the MIDI I/O board handles level translation and isolation

So the short answer is:

- MIDI signals are 5 V on the cable side
- that is normal
- but only through a proper MIDI interface circuit
- do not wire the MIDI jack directly to Teensy pins

If you want, I can give you the exact “perfboard-ready” schematic for:
- one MIDI OUT jack
- one MIDI IN jack
- Teensy 4.1 pin 0/1
- 6N138 optocoupler
- all resistor values in one pasteable diagram.
