#pragma once
#include <Arduino.h>
#include "acia_6850.h"
#include "Altair680.h"

extern Stream* activePort;

static inline void pump_host_serial()
{
    // Drain CPU TX → host serial (and keep your prompt detector)
    uint8_t tx;
    while (acia_try_pop_tx(&tx)) {
        activePort->write(tx);
        onSerialOutput((char)tx);  // your existing prompt sniffing
    }

    // Host serial → ACIA RX (non-blocking; back off if full)
    while (activePort->available()) {
        uint8_t b = (uint8_t)(activePort->read() & 0x7F);
        if (!acia_push_rx(b)) break;  // RX ring full, stop; we’ll resume next tick
    }
}
