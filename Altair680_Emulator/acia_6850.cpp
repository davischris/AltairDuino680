#include "acia_6850.h"

// ===== Config =====
static constexpr uint8_t RX_BITS_RDRF = 0x01;  // Receive Data Register Full
static constexpr uint8_t TX_BITS_TDRE = 0x02;  // Transmit Data Register Empty
static constexpr uint8_t RX_BITS_OVRN = 0x10;  // Receive Overrun 

// Small ring buffers keep us non-blocking and ISR-safe
static constexpr uint8_t RX_BUF_MASK = 63;     // 64 bytes (power-of-2)
static constexpr uint8_t TX_BUF_MASK = 63;     // 64 bytes

// ===== Internal state =====
static uint8_t controlReg = 0x00;
static uint8_t statusReg  = TX_BITS_TDRE;      // start with TX empty
static uint8_t rxData     = 0x00;
static uint8_t txData     = 0x00;
static uint8_t rxDataLat  = 0x00;              // last read byte (for completeness)

// RX ring: host → ACIA → CPU reads at data port
static uint8_t rxbuf[RX_BUF_MASK + 1];
static uint8_t rx_i = 0, rx_j = 0;

// TX ring: CPU writes at data port → host reads via acia_try_pop_tx
static uint8_t txbuf[TX_BUF_MASK + 1];
static uint8_t tx_i = 0, tx_j = 0;

static inline bool rx_empty()  { return rx_i == rx_j; }
static inline bool rx_full()   { return uint8_t((rx_i + 1) & RX_BUF_MASK) == rx_j; }
static inline bool tx_empty()  { return tx_i == tx_j; }
static inline bool tx_full()   { return uint8_t((tx_i + 1) & TX_BUF_MASK) == tx_j; }

void acia_init() {
    controlReg = 0x00;
    statusReg  = TX_BITS_TDRE; // TX empty, RX empty
    rx_i = rx_j = 0;
    tx_i = tx_j = 0;
}

void acia_master_reset() {
    acia_init();
}

// Host → ACIA (enqueue a received char). Returns false if full.
bool acia_push_rx(uint8_t c) {
    uint8_t next = uint8_t((rx_i + 1) & RX_BUF_MASK);
    if (next == rx_j) {
        // ring full → signal overrun and drop newest
        statusReg |= RX_BITS_OVRN;      // optional: keep latched until read clears it (your choice)
        return false;
    }
    rxbuf[rx_i] = c & 0x7F;             // keep 7-bit policy (matches your TX path)
    rx_i = next;
    statusReg |= RX_BITS_RDRF;          // data available
    return true;
}

// Host reads what CPU transmitted (optional use)
bool acia_try_pop_tx(uint8_t* out) {
    if (tx_empty()) return false;
    *out = txbuf[tx_j];
    tx_j = uint8_t((tx_j + 1) & TX_BUF_MASK);
    if (tx_empty()) statusReg |= TX_BITS_TDRE; // buffer drained → empty
    return true;
}

uint8_t acia_status() {
    if (rx_empty()) statusReg &= ~RX_BITS_RDRF; else statusReg |= RX_BITS_RDRF;
    if (tx_empty()) statusReg |=  TX_BITS_TDRE; else statusReg &= ~TX_BITS_TDRE;
    uint8_t s = statusReg;
    statusReg &= ~RX_BITS_OVRN;       // clear overrun on status read
    return s;
}

bool acia_rx_ready() { return (acia_status() & RX_BITS_RDRF) != 0; }
bool acia_tx_empty() { return (acia_status() & TX_BITS_TDRE) != 0; }

// ===== Memory-mapped API (use from your mem map) =====
uint8_t acia_mmio_read(uint16_t addr) {
    switch (addr & 1) {
        case 0:  // Status
            return acia_status();
        case 1:  // Data (CPU reads)
        {
            if (rx_empty()) {
                // no data; on real 6850, reading when empty returns last value / undefined.
                // we'll return rxDataLat as a safe behavior.
                return rxDataLat;
            }
            uint8_t c = rxbuf[rx_j];
            rx_j = uint8_t((rx_j + 1) & RX_BUF_MASK);
            // Update RDRF
            if (rx_empty()) statusReg &= ~RX_BITS_RDRF;
            rxDataLat = c;
            return c;
        }
    }
    return 0xFF;
}

void acia_mmio_write(uint16_t addr, uint8_t val) {
    switch (addr & 1) {
        case 0:  // Control register
            controlReg = val;
            if (val == 0x03) { // Master reset
                acia_master_reset();
            }
            break;

        case 1:  // Data (CPU writes → host TX queue)
        {
            if (!tx_full()) {
                txbuf[tx_i] = val & 0x7F;   // keep 7-bit like your old code
                tx_i = uint8_t((tx_i + 1) & TX_BUF_MASK);
                statusReg &= ~TX_BITS_TDRE; // no longer empty
            }
            // If you want to mirror to a debug serial and your onSerialOutput():
            // (Optional; do this from your main loop by draining acia_try_pop_tx)
            break;
        }
    }
}

void acia_receive_byte(uint8_t c) {
    acia_push_rx(c);   // always queue; acia_status()/read will reflect RDRF correctly
}
