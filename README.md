# Altair-Duino 680 - an Altair 680 Emulator

This project emulates the MITS Altair 680 microcomputer with 48k of RAM using an Arduino Due to simulate a 6800-based CPU and peripherals, including serial communication via a Motorola MC6850 (ACIA) interface.

## Overview

This emulator combines a 6800 CPU core and peripheral emulation with an Altair 680 memory map. It is capable of running MITS 680 BASIC, faithfully reproducing the behavior of the original hardware.

## Code Origins

- The **6800 CPU emulation** is adapted from the SIMH 6800 emulator originally written by **William Beech**, with corrections and enhancements contributed by **James Nichols**.
- The version of BASIC and all manuals included were retreived from **Mike Douglas's** archive of vintage computer files.
- The **system and I/O emulation** is based on the SWTPC emulator by **Béla Török**, completely rewritten and modified to work with the **Altair 680** memory layout and hardware characteristics.

## Features

- Emulates Motorola 6800 CPU
- Runs original Altair 680 ROM and 6800-compatible BASIC
- Serial I/O emulation for ACIA at memory-mapped addresses
- Compatible with real S-record images used on the physical Altair 680
- Debugging output on simulation halt
- Ability to "instantly" load Altair 680 BASIC v3.2 or Editor/Assembler v1.0

## Front Panel Emulation
- Address LEDs (A0–A15) now reflect the actual 6800 address bus.
- Data LEDs (D0–D7) reflect the current data bus value.
- In HALT mode, the address switches let you directly inspect memory contents at the selected address, just like the real Altair 680.
- In RUN mode, the LEDs show live CPU activity (addresses and data changing as instructions execute).
- This provides a much more historically accurate “blinkenlights” experience compared to earlier versions.

## Headless Mode
- If the Arduino Due is run without any front panel hardware connected, the emulator will automatically detect this and force RUN mode.
- This allows BASIC or other programs to run on a bare Due with nothing but a USB serial connection.
- With headless mode enabled, panel-related features (LEDs, switches, DEPOSIT/RESET actions) are simply ignored.

## Installation and Setup

### Hardware
- Arduino Due or compatible
- USB cable

### Software
- Arduino IDE
- Terminal Emulator (CoolTerm Tera Term, minicom, PuTTY, Serial, etc.)

### Serial Settings
- Baud Rate: 9600
- Data/Parity/Stop Bits: 8N1
- Flow Control: None
- **Local Echo**: Off
- **Line Ending**: Carriage Return (CR or \r only)

## Loading Programs
Use L from  the monitor prompt (".") to load Motorola S-records.

At the prompt, press L and *do not* press return.  Transmit the .s19 text file from your terminal's send feature (no line ending conversion).

Ensure all lines end with a single CR character,  Avoid CRLF.

## "Instant Load" Programs
Set to HALT mode.  Set address switches to the desired application and lower DEPOSIT toggle (press down).

- Address 0x0001 = Altair 680 BASIC v3.2
- Address 0x0002 = Altair 680 Editor/Assembler v1.0

When you toggle RUN mode, the loaded application will start automatically.

## Loading ROM Images
When powering up (or connecting via USB) hold down RESET toggle to load ROM based on address.

- First nibble address 0x0000 = Monitor ROM
- First nibble address 0x0001 = VTL-2 ROM (launch with J FC00)
- First nibble address 0x0002 = FLEX ROM (untested)

## Other Power-Up Settings Available
When powering up (or connecting via USB) hold down RESET toggle to load the following settings:

### Serial Port
- Second nibble address 0x0000 = Programming USB port on Arduino
- Second nibble address 0x0001 = Serial1 (Arduino pins 18/19)
- Second nibble address 0x0002 = Serial2 (Arduino pins 16/17)

### Baud Rate
- Third nibble address 0x0000 = 9600 baud (default)
- Third nibble address 0x0001 = 110 baud
- Third nibble address 0x0002 = 300 baud
- Third nibble address 0x0003 = 2400 baud
- Third nibble address 0x0004 = 4800 baud
- Third nibble address 0x0005 = 19200 baud
- Third nibble address 0x0006 = 38400 baud
- Third nibble address 0x0007 = 57600 baud
- Third nibble address 0x0008 = 115200 baud

## Saving Settings
- Switch all address switches down, then lower RESET and DEPOSIT toggles at the same time.
- Settings (ROM, Serial Port, and Baud Rate) will be saved to Arduino flash memory.
- To revert to defaults, leave address switches off, hold RESET down, and power up.  Then save settings with RESET + DEPOSIT down.
- Note: uploading new code to Arduino will clear any saved settings and revert to defaults.

## Notes

- The version of BASIC included does not like lowercase in commands (PRINT, NEW, FOR, etc).  While you can use lowercase within quotes in print statements (PRINT "Hey there lonely girl"), everything else should be uppercase.
- Backspace does not work in version 1.1 of Altair 680 BASIC.  Back in 1976 they assumed you would use the strikeout character ("_").
- The same Monitor ROM and S-records are used with my physical Altair 680 to ensure compatibility

## License

This project builds on open source work and is distributed for educational and historical preservation purposes. Refer to the licenses of the original authors where applicable.

