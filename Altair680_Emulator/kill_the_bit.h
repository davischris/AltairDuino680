// kill_the_bit.h
#pragma once
#include <stdint.h>

// Load address for this demo:
#define KTB_LOAD_ADDR     0x0000

// Scratch locations used by the routine:
#define KTB_PAT_ADDR      0x0300  // pattern byte (A15..A8 walker)
#define KTB_XHI_ADDR      0x0301  // high byte for $pp00
#define KTB_XLO_ADDR      0x0302  // low byte for $pp00 (always 0x00)
#define KTB_DELAY_HI_ADDR 0x0303  // 16-bit delay high
#define KTB_DELAY_LO_ADDR 0x0304  // 16-bit delay low

// Entry points (if you need symbols)
#define KTB_ENTRY_ADDR    0x0000  // starts with JMP $0003
#define KTB_LOOP_ADDR     0x0012
#define KTB_DELAY_ADDR    0x0040

// Adjustable delay: the program initializes $0303:$0304 to 0x4000.
// To change speed at runtime, in HALT just deposit new values to those bytes.

static const uint8_t kill_the_bit_bin[] = {
  // 0000: JMP $0003
  0x7E, 0x00, 0x03,
  // 0003: LDAA #$80; STAA $0300
  0x86, 0x80,
  0xB7, 0x03, 0x00,
  // 0008: LDAA #$C0; STAA $0303
  0x86, 0xC0,
  0xB7, 0x03, 0x03,
  // 000D: LDAA #$00; STAA $0304
  0x86, 0x00,
  0xB7, 0x03, 0x04,
  // 0012: LDAA $0300; STAA $0301; CLRB; STAB $0302; LDX $0301
  0xB6, 0x03, 0x00,
  0xB7, 0x03, 0x01,
  0x5F,
  0xF7, 0x03, 0x02,
  0xFE, 0x03, 0x01,
  // 001F: LDAA 0,X x4
  0xA6, 0x00,
  0xA6, 0x00,
  0xA6, 0x00,
  0xA6, 0x00,
  // 0027: JSR $0040
  0xBD, 0x00, 0x40,
  // 002A: LDAA $0300; LSRA; STAA $0300; BNE $0012
  0xB6, 0x03, 0x00,
  0x44,
  0xB7, 0x03, 0x00,
  0x26, 0xDF,
  // 0033: LDAA #$80; STAA $0300; JMP $0012
  0x86, 0x80,
  0xB7, 0x03, 0x00,
  0x7E, 0x00, 0x12,
  // 0040: DELAY: LDX $0303; DEX; BNE $0043; RTS
  0xFE, 0x03, 0x03,
  0x09,
  0x26, 0xFD,
  0x39
};

static const uint16_t kill_the_bit_size = sizeof(kill_the_bit_bin);  // 0x47 bytes
