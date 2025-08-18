#include "panel.h"

// Cache last LED states to minimize digitalWrite traffic
static uint16_t last_addr = 0xFFFF;
static uint8_t  last_data = 0xFF;
static int8_t   last_rw   = -1;
static int8_t   last_fetch= -1;
static int8_t   last_halt = -1;

static inline void set16(const uint8_t pins[16], uint16_t v) {
  if (v == last_addr) return;
  last_addr = v;
  for (int i = 0; i < 16; ++i)
    digitalWrite(pins[i], (v & (1u << i)) ? HIGH : LOW);
}
static inline void set8(const uint8_t pins[8], uint8_t v) {
  if (v == last_data) return;
  last_data = v;
  for (int i = 0; i < 8; ++i)
    digitalWrite(pins[i], (v & (1u << i)) ? HIGH : LOW);
}
static inline void setPin(uint8_t pin, int value, int8_t* last) {
  if (pin == 0xFF) return;
  if (*last == value) return;
  *last = value;
  digitalWrite(pin, value ? HIGH : LOW);
}

void panel_begin() {
  for (int i = 0; i < 16; ++i) pinMode(addrLedPins[i], OUTPUT);
  for (int i = 0; i < 8;  ++i) pinMode(dataLedPins[i], OUTPUT);
  if (PANEL_LED_RW    != 0xFF) pinMode(PANEL_LED_RW,    OUTPUT);
  if (PANEL_LED_FETCH != 0xFF) pinMode(PANEL_LED_FETCH, OUTPUT);
  if (PANEL_LED_HALT  != 0xFF) pinMode(PANEL_LED_HALT,  OUTPUT);

  // Force a first update to clear all LEDs
  last_addr = 0xEEEE;
  last_data = 0xEE;
  last_rw   = -1;
  last_fetch= -1;
  last_halt = -1;
  set16(addrLedPins, 0);
  set8 (dataLedPins, 0);
  setPin(PANEL_LED_RW,    0, &last_rw);
  setPin(PANEL_LED_FETCH, 0, &last_fetch);
  setPin(PANEL_LED_HALT,  0, &last_halt);
}

void panel_poll_and_update(uint32_t now_us, uint16_t pc, bool halted, bool have_halt_override, uint16_t halt_override_addr) {
  // Pull a race-free copy of the last bus cycle
  bus_snapshot_t snap;
  bus_snapshot_read(&snap);

  // Prefer live bus for a short window; else fall back to PC
  bool fresh = (snap.t_us && (uint32_t)(now_us - snap.t_us) < PANEL_DATA_DECAY_US);

  // If we are HALTed and the caller gave us a switch address, show that
  uint16_t addr_to_show =
    (halted && have_halt_override)
      ? halt_override_addr
      : (fresh ? snap.addr : pc);

  // For data: on stale panel, keep showing the last bus data (don’t cause reads)
  uint8_t  data_to_show = snap.data;

  set16(addrLedPins, addr_to_show);
  set8 (dataLedPins, data_to_show);

  setPin(PANEL_LED_RW,    fresh ? (snap.rw ? 1 : 0) : 1, &last_rw);         // default READ
  setPin(PANEL_LED_FETCH, fresh ? (snap.type == BUS_FETCH) : 0, &last_fetch);
  setPin(PANEL_LED_HALT,  halted ? 1 : 0, &last_halt);
}
