#pragma once
#include <Arduino.h>
#include <stdint.h>
#include "bus.h"

// === Your wiring (from your message) ===
// Address LEDs (A0 - A15)
static const uint8_t addrLedPins[16] = {
  34, 35, 36, 37, 38, 39, 40, 41,   // A0-A7
  51, 50, 49, 48,                   // A8-A11
  47, 46, 45, 44                    // A12-A15
};
// Data LEDs (D0 - D7)
static const uint8_t dataLedPins[8] = { 25, 26, 27, 28, 14, 15, 29, 11 };

// Optional status lamps (set to 0xFF if not wired yet)
#ifndef PANEL_LED_RW
#define PANEL_LED_RW     0xFF    // on = READ, off = WRITE
#endif
#ifndef PANEL_LED_FETCH
#define PANEL_LED_FETCH  0xFF    // on during opcode fetch cycles
#endif
#ifndef PANEL_LED_HALT
#define PANEL_LED_HALT   0xFF    // on when CPU halted
#endif

// How long to prefer "live bus" over PC on the panel
#ifndef PANEL_DATA_DECAY_US
#define PANEL_DATA_DECAY_US  10000u  // 10 ms; tune 20–80 ms to taste
#endif

void panel_begin();

// Call this from your 10 ms service:
//   - now_us  = micros()
//   - pc      = current PC
//   - halted  = cpu_is_halted()
// If have_halt_override is true, panel will show halt_override_addr when HALTed
void panel_poll_and_update(uint32_t now_us, uint16_t pc, bool halted,
                           bool have_halt_override = false,
                           uint16_t halt_override_addr = 0);                          