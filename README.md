# AltairDuino680 - an Altair 680 Emulator

This project emulates the MITS Altair 680 microcomputer using an Arduino Due to simulate a 6800-based CPU and peripherals, including serial communication via a Motorola MC6850 (ACIA) interface.

## Overview

This emulator combines a 6800 CPU core and peripheral emulation with an Altair 680 memory map. It is capable of running MITS 680 BASIC, faithfully reproducing the behavior of the original hardware.

## Code Origins

- The **6800 CPU emulation** is adapted from the SIMH 6800 emulator originally written by **William Beech**, with corrections and enhancements contributed by **James Nichols**.
- The version of BASIC and all manuals included were retreived from **Mike Douglas's** archive of vintage computer files.
- The **system and I/O emulation** is based on the SWTPC emulator by **Béla Török**, rewritten and modified to work with the **Altair 680** memory layout and hardware characteristics.

## Features

- Emulates Motorola 6800 CPU
- Runs original Altair 680 ROM and 6800-compatible BASIC
- Serial I/O emulation for ACIA at memory-mapped addresses
- Compatible with real S-record images used on the physical Altair 680
- Debugging output on simulation halt

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

## Notes

- The version of BASIC included does not like lowercase in commands (PRINT, NEW, FOR, etc).  While you can use lowercase within quotes in print statements (PRINT "Hey there lonely girl"), everything else should be uppercase.
- Backspace does not work in version 1.1 of Altair 680 BASIC.  Back in 1976 they assumed you would use the strikeout character ("_").
- The same Monitor ROM and S-records are used with my physical Altair 680 to ensure compatibility

## License

This project builds on open source work and is distributed for educational and historical preservation purposes. Refer to the licenses of the original authors where applicable.

