#include "altair680_physical_ROM.h"

extern void m6800_reset();
extern void dump_regs(void);
extern int32_t sim_instr(long);
extern int32_t PC;
extern int32_t SP;

char RAM_0000_BFFF[0xC000];
char StatusRegister = 0x02;
char ControlRegister = 0;
char ReceiveDataRegister = 0;

#define MC6850_RTS 13

/*
 * TRACE_PC flag can be set anywhere in simmulator, to cause m6800 "fetch_byte" to dump registers on opcode fetch,
 * using the trace() function found in this file.
 */
int32_t TRACE_PC;
  
// Emulates the Motorola MC6850 ACIA for the Altair 680
int32_t EmulateMC6850(char rw, int addr, int val = 0) {
    static uint8_t controlReg = 0x00;
    static uint8_t statusReg = 0x02;  // Bit 1 = TDRE set initially (transmit ready)
    static uint8_t rxData = 0x00;

    // --- Read Access ---
    if (rw == 'r') {
        switch (addr & 1) {
            case 0:  // Read Status Register
                if (Serial.available() > 0) {
                    if (!(statusReg & 0x01)) {
                        rxData = Serial.read();
                        if (rxData == 0x0A) return statusReg;
                        statusReg |= 0x01;  // Set RDRF
                    } else {
                        Serial.read(); // Drop extra
                    }
                }
                // Always set TDRE (bit 1 = transmit ready)
                statusReg |= 0x02;
                return statusReg;

            case 1:  // Read Receive Data Register
                statusReg &= ~0x01;  // Clear RDRF
                return rxData;
        }
    }

    // --- Write Access ---
    else if (rw == 'w') {
        switch (addr & 1) {
            case 0:  // Control register
                controlReg = val;
                if (val == 0x03) {
                    // Master Reset
                    statusReg = 0x02;  // TDRE = 1, RDRF = 0
                }
                break;

            case 1:  // Transmit data register
                Serial.write(val & 0x7F);
                statusReg |= 0x02;  // Set TDRE
                break;
        }
    }

    return 0;
}

int32_t CPU_BD_get_mbyte(int32_t addr) {
    if (addr >= 0x0000 && addr <= 0xBFFF) {
        return RAM_0000_BFFF[addr];
    }

    // Handle ACIA (MC6850) at F000 (Control/Status) and F001 (Data)
    if (addr == 0xF000 || addr == 0xF001) {
        return EmulateMC6850('r', addr);
    }

    // Handle STRAPS input at F002 (monitor config switches)
    if (addr == 0xF002) {
        return 0x40;  // Bit 6 set disables echo; Bit 7 clear keeps terminal enabled
    }

    // ROM space
    if (addr >= 0xFF00 && addr <= 0xFFFF) {
        return altair680b_rom[addr - 0xFF00];
    }

    return 0xFF;  // default filler
}

void CPU_BD_put_mbyte(int32_t addr, int32_t val) {
    if (addr >= 0x0000 && addr <= 0xBFFF) {
        RAM_0000_BFFF[addr] = val & 0xFF;
    }
    else if (addr == 0xF000 || addr == 0xF001) {
        EmulateMC6850('w', addr, val);
    }
    // Writes to STRAPS or ROM range ignored
}

int32_t CPU_BD_get_mword(int32_t addr) {
    uint8_t hi = CPU_BD_get_mbyte(addr);
    uint8_t lo = CPU_BD_get_mbyte(addr + 1);
    return (hi << 8) | lo;
}

void CPU_BD_put_mword(int32_t addr, int32_t val) {
    CPU_BD_put_mbyte(addr, (val >> 8) & 0xFF);  // High byte
    CPU_BD_put_mbyte(addr + 1, val & 0xFF);     // Low byte
}

void trace(int32_t PC, char *opcode, int32_t CCR, int32_t B, int32_t A, int32_t IX, int32_t SP)
{
  char text[80];
  
  sprintf(text, "%04X %-5s %02X %02X %02X %04X %04X\n", PC, opcode, CCR, B, A, IX, SP);
  Serial.print(text);
}

void WriteToConsole(char* Text)
{
  Serial.println(Text);
}

void setup() {
    TRACE_PC = 0;

    pinMode(MC6850_RTS, OUTPUT);
    digitalWrite(MC6850_RTS, 0);
    Serial.begin(9600);
    Serial.setTimeout(2);  // Short timeout to flush lingering LF

    // Clear RAM
    memset(RAM_0000_BFFF, 0x00, sizeof(RAM_0000_BFFF));

    for (int sp = 0x01FF; sp >= 0x01C0; sp -= 2) {
        RAM_0000_BFFF[sp]     = 0x00;
        RAM_0000_BFFF[sp - 1] = 0xFF;
    }

     m6800_reset();

    Serial.println("Starting Altair 680 Emulator ");

}

void loop() {
    int32_t reason;

    EmulateMC6850('r', 0x8000, 0);  // Check for serial input (RDRF)
    reason = sim_instr(1);         // Execute one instruction per loop

    if (reason != 0) {
        dump_regs();               // Print CPU state if halted
        Serial.println("Simulation halted!");
        while (true);              // Stop the loop
    }
}
