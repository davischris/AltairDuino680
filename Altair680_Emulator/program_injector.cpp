#include "program_injector.h"
#include "Altair680.h"
#include "acia_6850.h"
#include <Arduino.h>

// Add more entries as desired
static const ProgramEntry programTable[] = {
    { PROGRAM_BASIC, 0x0001, "23 Matches (BASIC)", basic_23_match },
    { PROGRAM_VTL2,  0x0001, "VTL-2 Mini-Trek", vtl2_minitrek },
    { PROGRAM_ASM,  0x0001, "Asm Hello World", asm_hello },
    { PROGRAM_BASIC, 0x0002, "Zilch (BASIC)", basic_zilch },
    { PROGRAM_VTL2,  0x0002, "VTL-2 Lunar Lander", vtl2_lunarlander },
    { PROGRAM_BASIC, 0x0003, "Sine (BASIC)", basic_sin },
    { PROGRAM_VTL2,  0x0003, "VTL-2 Lunar (1k)", vtl2_1klander },
    { PROGRAM_BASIC, 0x0004, "Sea War (BASIC)", basic_seawar },
    { PROGRAM_VTL2,  0x0004, "VTL-2 Tic Tac Toe", vtl2_tictactoe },
    { PROGRAM_BASIC, 0x0005, "Monopoly (BASIC)", basic_monopoly },
    { PROGRAM_VTL2,  0x0005, "VTL-2 Star Shot", vtl2_starshot },
    { PROGRAM_BASIC, 0x0006, "Orbit (BASIC)", basic_orbit },
    { PROGRAM_VTL2,  0x0006, "VTL-2 Life", vtl2_life },
    { PROGRAM_BASIC, 0x0007, "Pizza (BASIC)", basic_pizza },
    { PROGRAM_VTL2,  0x0007, "VTL-2 Craps", vtl2_craps },
    { PROGRAM_BASIC, 0x0008, "Poker (BASIC)", basic_poker },
    { PROGRAM_VTL2,  0x0008, "VTL-2 Hurkle", vtl2_hurkle },
    { PROGRAM_BASIC, 0x0009, "Wumpus (BASIC)", basic_wumpus },
    // ...add more here...
};

// At top (keep yours or replace)
static const ProgramEntry* currentEntry = nullptr;
static size_t inject_pos = 0;
static bool injecting = false;

// Timing knobs (you can tune to taste)
static unsigned long lastInjectTime = 0;
// No per-char delay needed when pacing by RX ring:
static unsigned long injectCharDelay = 0;            // µs
// Keep a tiny post-line dwell only if you see missed lines; start at 0
static unsigned long injectLineDelay = 0;            // µs (e.g., 2000 if needed)
static bool inLineDelay = false;

// =================== INTERFACE IMPLEMENTATION ===================

void programInjectorBegin() {
    currentEntry = nullptr;
    injecting = false;
    inject_pos = 0;
    lastInjectTime = 0;
}

void programInjectorStart(ProgramType type, uint16_t address) {
    for (size_t i = 0; i < sizeof(programTable)/sizeof(programTable[0]); ++i) {
        if (programTable[i].type == type && programTable[i].address == address) {
            currentEntry = &programTable[i];
            inject_pos = 0;
            injecting = true;
            lastInjectTime = micros();
            return;
        }
    }
    currentEntry = nullptr;
    injecting = false;
}

// Pace purely by ACIA RX space; optional tiny pause after CR
void programInjectorFeed() {
    unsigned long now = micros();
    if (!injecting || !currentEntry || !currentEntry->text) return;

    // Optional: respect a post-line dwell
    if (inLineDelay) {
        if ((now - lastInjectTime) < injectLineDelay) return;
        inLineDelay = false;
    }

    // Optional: send a small burst per call (prevents starving other tasks)
    const uint8_t maxBurst = 16;
    uint8_t sent = 0;

    while (sent < maxBurst) {
        char c = currentEntry->text[inject_pos];
        if (!c) {
            // End of text
            injecting = false;
            currentEntry = nullptr;
            return;
        }

        // If you want a per-char throttle (usually unnecessary), enforce it here
        if (injectCharDelay) {
            if ((now - lastInjectTime) < injectCharDelay) break;
        }

        // Try to queue the byte; if RX ring is full, stop and try next tick
        if (!acia_push_rx((uint8_t)c)) break;

        // Successfully queued: advance
        inject_pos++;
        lastInjectTime = now;
        sent++;

        // If end of line, set optional dwell and stop this tick
        if (c == '\r' || c == '\n') {
            if (injectLineDelay) inLineDelay = true;
            break;
        }

        // Refresh time if needed
        now = micros();
    }
}

void programInjectorAbort(bool flushLine) {
    injecting = false;
    currentEntry = nullptr;
    inject_pos = 0;
    inLineDelay = false;
    lastInjectTime = 0;
    if (flushLine) (void)acia_push_rx('\r');
}

