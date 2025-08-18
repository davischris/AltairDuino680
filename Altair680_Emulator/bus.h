#pragma once
#include <Arduino.h>
#include <stdint.h>

// Cycle classification (FETCH is a READ that fetched an opcode)
typedef enum { BUS_FETCH, BUS_READ, BUS_WRITE, BUS_IO } bus_type_t;

typedef struct {
  uint16_t    addr;     // A0..A15 at the cycle
  uint8_t     data;     // D0..D7 at the cycle (value read or written)
  bus_type_t  type;     // fetch/read/write/io
  bool        rw;       // true = read/fetch, false = write
  uint32_t    t_us;     // micros() when captured
} bus_snapshot_t;

// Last observed bus cycle (written by CPU core, read by panel service)
extern volatile bus_snapshot_t g_bus;
extern volatile uint32_t       g_bus_seq;

// Call on EVERY bus cycle (all RAM/ROM/IO reads, writes, opcode fetches)
static inline void bus_capture(uint16_t addr, uint8_t data, bus_type_t type, uint32_t now_us) {
  // Single-writer (CPU thread) → multi-reader (panel thread) with a simple seq
  uint32_t seq = g_bus_seq + 1;
  g_bus.addr = addr;
  g_bus.data = data;
  g_bus.type = type;
  g_bus.rw   = (type != BUS_WRITE);      // IO reads count as "read"
  g_bus.t_us = now_us;
  g_bus_seq  = seq;                      // publish
}

// Helper to pull a race-free snapshot
static inline void bus_snapshot_read(bus_snapshot_t* out) {
  uint32_t s1, s2;
  do {
    s1 = g_bus_seq;
    out->addr = g_bus.addr;
    out->data = g_bus.data;
    out->type = g_bus.type;
    out->rw   = g_bus.rw;
    out->t_us = g_bus.t_us;
    s2 = g_bus_seq;
  } while (s1 != s2);
}