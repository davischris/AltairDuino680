# AltairDuino680
# Altair 680 Emulator with 6800 BASIC

This project is an emulator for the Altair 680 computer, modified to run 6800 BASIC.

## Overview

This emulator combines a 6800 CPU core and peripheral emulation with an Altair 680 memory map. It is capable of running MITS 680 BASIC, faithfully reproducing the behavior of the original hardware.

## Code Origins

- The **6800 CPU emulation** is adapted from the SIMH SWTPC emulator originally written by **William Beech**, with corrections and enhancements contributed by **James Nichols**.
- The **system and I/O emulation** is based on the SWTPC emulator by **Béla Török**, rewritten and modified to work with the **Altair 680** memory layout and hardware characteristics.

## Features

- Runs original Altair 680 ROMs and 6800-compatible BASIC
- Serial I/O emulation for ACIA at memory-mapped addresses
- Compatible with real S-record images used on the physical Altair 680
- Debugging output on simulation halt

## Usage

1. Load the 6800 BASIC S-record (`.S19`) file into memory
2. Start the emulator and jump to the BASIC entry point (e.g., `J 0000`)
3. Use a serial terminal (e.g., Tera Term or PuTTY) to interact with BASIC

## Notes

- This version avoids double echo problems seen in some serial configurations by emulating the ACIA more faithfully
- The same BASIC ROM and S-records are used as with a physical Altair 680 to ensure compatibility

## License

This project builds on open source work and is distributed for educational and historical preservation purposes. Refer to the licenses of the original authors where applicable.

