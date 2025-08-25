#pragma once
#include <stdint.h>
#include <stdbool.h>

// Public ACIA interface (MC6850)
void    acia_init();

// Memory-mapped access (use these from your memmap)
uint8_t acia_mmio_read(uint16_t addr);         // addr & 1: 0=status, 1=data
void    acia_mmio_write(uint16_t addr, uint8_t val);
void    acia_receive_byte(uint8_t c);

// Host-side helpers (terminal ↔ emulator)
bool    acia_push_rx(uint8_t c);               // queue char from “terminal” to ACIA (returns false if full)
bool    acia_try_pop_tx(uint8_t* out);         // optional: get next TX byte CPU wrote
uint8_t acia_status();                         // mirror of status register (bit0=RDRF, bit1=TDRE)

// Convenience
bool    acia_rx_ready();                       // (status & 0x01) != 0
bool    acia_tx_empty();                       // (status & 0x02) != 0
void    acia_master_reset();                   // write 0x03 to control always ends up here
