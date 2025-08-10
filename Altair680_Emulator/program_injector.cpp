#include "program_injector.h"
#include "Altair680.h"
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

static const ProgramEntry* currentEntry = nullptr;
static size_t inject_pos = 0;
static bool injecting = false;
unsigned long lastInjectTime = 0;
unsigned long injectCharDelay = 10 * 1000; // 10 ms default per char
unsigned long injectLineDelay = 50 * 1000; // 50 ms after each line

// =================== INTERFACE IMPLEMENTATION ===================

void programInjectorBegin() {
    currentEntry = nullptr;
    injecting = false;
    inject_pos = 0;
}

void programInjectorStart(ProgramType type, uint16_t address) {
    // Find matching program
    for (size_t i = 0; i < sizeof(programTable)/sizeof(programTable[0]); ++i) {
        if (programTable[i].type == type && programTable[i].address == address) {
            currentEntry = &programTable[i];
            inject_pos = 0;
            injecting = true;
            return;
        }
    }
    // No match
    currentEntry = nullptr;
    injecting = false;
}

void programInjectorFeed() {
    static bool inLineDelay = false;
    unsigned long now = micros();
    if (!injecting || !currentEntry || !currentEntry->text)
        return;

    if (inLineDelay) {
        if (now - lastInjectTime < injectLineDelay)
            return; // Still waiting after line
        inLineDelay = false;
    }

    // Only inject if MC6850 ready for a char
    if (!(getMc6850StatusReg() & 0x01)) {
        char c = currentEntry->text[inject_pos];
        if (c) {
            EmulateMC6850_InjectReceivedChar(c);
            inject_pos++;
            lastInjectTime = now;
            // If end of line, pause longer
            if (c == '\r' || c == '\n') {
                inLineDelay = true;
            }
        } else {
            injecting = false; // done!
            currentEntry = nullptr;
        }
    }
}

bool programInjectorIsInjecting() {
    return injecting;
}

const char* programInjectorCurrentName() {
    return currentEntry ? currentEntry->name : nullptr;
}

void programInjectorAbort() {
    injecting = false;
    currentEntry = nullptr;
    inject_pos = 0;
}

