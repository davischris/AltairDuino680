#include "bus.h"

volatile bus_snapshot_t g_bus = {0, 0, BUS_READ, true, 0};
volatile uint32_t       g_bus_seq = 0;
