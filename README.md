# AltairDuino680 - an Altair 680 Emulator

This project is an emulator for the Altair 680 computer.

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

## Notes

- This version currently has a double echo problem when running Altair 680 BASIC.  This can be solved by turing off the local echo in your terminal emulator.
- The same Monitor ROM and S-records are used with my physical Altair 680 to ensure compatibility

## License

This project builds on open source work and is distributed for educational and historical preservation purposes. Refer to the licenses of the original authors where applicable.

