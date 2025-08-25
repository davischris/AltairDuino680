#include "altair680_physical_ROM.h"
#include "vtl2_rom.h"
#include "flex_boot_rom.h"
#include "m6800.h"
#include "program_injector.h"
#include "acia_6850.h"
#include "platform_io.h"
#include "bus.h"
#include "panel.h"
#include "sd_card.h"
#include "altair_cassette_basic.h"
#include <DueFlashStorage.h>

struct ConfigData {
    uint8_t selectedPort;
    uint8_t selectedROM;
    unsigned long baudRate;
    uint8_t checksum; // simple integrity check
};

DueFlashStorage dueFlashStorage;

enum RomType { MONITOR_ROM, VTL2_ROM, FLEX_ROM };
RomType activeROM = MONITOR_ROM; // Default at startup
Stream* activePort = &Serial; // Default to USB serial

extern void m6800_reset();
extern void dump_regs(void);
extern int32_t sim_instr(long);
extern int32_t PC;
extern int32_t SP;

char RAM_0000_BFFF[0xC000];
bool lastDepositState = HIGH;
bool lastResetState = HIGH;
uint32_t currentSelectedPort = 0;
unsigned long currentBaudRate = 9600;
static String host_line;

// Address Switches (SW0 - SW15)
const int addressSwitchPins[16] = {
  62, 63, 64, 65, 66, 67, 68, 69,   // SW0-SW7
  31, 30, 23, 24,                   // SW8-SW11 - NOTE: change 17, 16 to 31, 30 when v1.0.1 boards arrive
  70, 71, 42, 43                    // SW12-SW15
};

// Data Switches
const int dataSwitchPins[8] = {53, 54, 55, 56, 57, 59, 60, 61};

#define RUN 21
#define HALT 20
#define DEPOSIT 58
#define RESET 52
#define RESETDOWN 33
#define DEPOSITDOWN 32
#define RUNLED 9
#define HALTLED 13
#define ACLED 12

/*
 * TRACE_PC flag can be set anywhere in simmulator, to cause m6800 "fetch_byte" to dump registers on opcode fetch,
 * using the trace() function found in this file.
 */
int32_t TRACE_PC;
bool aciaReadIn = false;
bool lastDepositDown = HIGH;
String output_buffer = "";
bool check_basic = false;
bool basic_ready_for_input = false;
bool vtl_ready_for_input = false;
bool assembler_ready_for_input = false;
// Track whether we're running with no panel hardware
static bool g_headless = false;
static const char* CONFIG_PATH = "/Altair680.ini";

void EmulateMC6850_InjectReceivedChar(char c) {
    // Use the same static variables as in EmulateMC6850
    extern uint8_t rxData;
    extern uint8_t statusReg;

    // Only accept if RDRF not already set (buffer empty)
    if (!(statusReg & 0x01)) {
        rxData = c & 0x7F;   // 7-bit clean (like real serial)
        statusReg |= 0x01;   // Set RDRF (data ready)
    }
}

int32_t CPU_BD_get_mbyte(int32_t addr, bool is_fetch) {
    uint32_t now = micros();
    uint8_t v;
    bus_type_t t = is_fetch ? BUS_FETCH : BUS_READ;

    if (addr >= 0x0000 && addr <= 0xBFFF) {
        v = RAM_0000_BFFF[addr];
        bus_capture((uint16_t)addr, v, t, now);
        return v;
    }
    if (addr == 0xF000 || addr == 0xF001) {
        v = acia_mmio_read((uint16_t)addr);
        bus_capture((uint16_t)addr, v, BUS_IO, now);
        return v;
    }
    if (addr == 0xF002) {
        v = 0x40; // straps
        bus_capture((uint16_t)addr, v, BUS_IO, now);
        return v;
    }
    if (addr >= 0xFC00 && addr <= 0xFEFF) {
        if (activeROM == VTL2_ROM) {
            v = vtl2_rom[addr - 0xFC00];
            bus_capture((uint16_t)addr, v, t, now);
            return v;
        } else if (activeROM == FLEX_ROM && addr <= 0xFCFF) {
            v = flex_boot_rom[addr - 0xFC00];
            bus_capture((uint16_t)addr, v, t, now);
            return v;
        }
        // fall through if unmapped under current ROM
    }
    if (addr >= 0xFF00 && addr <= 0xFFFF) {
        v = altair680b_rom[addr - 0xFF00];
        bus_capture((uint16_t)addr, v, t, now);
        return v;
    }

    v = 0xFF; // unmapped pulls high
    bus_capture((uint16_t)addr, v, t, now);
    return v;
}

void CPU_BD_put_mbyte(int32_t addr, int32_t val) {
    uint32_t now = micros();
    uint8_t v = (uint8_t)val;

    if (addr >= 0x0000 && addr <= 0xBFFF) {
        RAM_0000_BFFF[addr] = v;
        bus_capture((uint16_t)addr, v, BUS_WRITE, now);
        return;
    }
    if (addr == 0xF000 || addr == 0xF001) {
        acia_mmio_write((uint16_t)addr, v);
        bus_capture((uint16_t)addr, v, BUS_IO, now);
        return;
    }
    // ROM/unmapped writes still show address/data on bus
    bus_capture((uint16_t)addr, v, BUS_WRITE, now);
}

// int32_t CPU_BD_get_mbyte(int32_t addr) {
//     if (addr >= 0x0000 && addr <= 0xBFFF) return RAM_0000_BFFF[addr];

//     if (addr == 0xF000 || addr == 0xF001) {
//         return acia_mmio_read((uint16_t)addr);
//     }

//     if (addr == 0xF002) return 0x40;

//     if (activeROM == VTL2_ROM && addr >= 0xFC00 && addr <= 0xFEFF) return vtl2_rom[addr - 0xFC00];
//     else if (activeROM == FLEX_ROM && addr >= 0xFC00 && addr <= 0xFCFF) return flex_boot_rom[addr - 0xFC00];

//     if (addr >= 0xFF00 && addr <= 0xFFFF) return altair680b_rom[addr - 0xFF00];

//     return 0xFF;
// }

// void CPU_BD_put_mbyte(int32_t addr, int32_t val) {
//     if (addr >= 0x0000 && addr <= 0xBFFF) {
//         RAM_0000_BFFF[addr] = val & 0xFF;
//     }
//     else if (addr == 0xF000 || addr == 0xF001) {
//         acia_mmio_write((uint16_t)addr, (uint8_t)val);
//     }
// }

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
  activePort->print(text);
}

void WriteToConsole(char* Text)
{
  activePort->println(Text);
}

void randomizeRAM() {
    for (size_t i = 0; i < sizeof(RAM_0000_BFFF); i++) {
        RAM_0000_BFFF[i] = random(0, 256);
    }
}

void setup() {
    TRACE_PC = 0;
    String msg ="Starting Altair 680 Emulator";

    // Initialize switches as INPUT_PULLUP
    for (int i = 0; i < 16; i++) {
        pinMode(addressSwitchPins[i], INPUT_PULLUP);
    }

    for (int i = 0; i < 8; i++) {
        pinMode(dataSwitchPins[i], INPUT_PULLUP);
    }

    pinMode(HALT, INPUT_PULLUP); // RUN switch (D20)
    pinMode(RUN, INPUT_PULLUP); // HALT switch (D21)
    pinMode(DEPOSIT, INPUT_PULLUP);
    pinMode(RESET, INPUT_PULLUP);
    pinMode(RESETDOWN, INPUT_PULLUP);
    pinMode(DEPOSITDOWN, INPUT_PULLUP);
    pinMode(HALTLED, OUTPUT);
    pinMode(RUNLED, OUTPUT);
    pinMode(ACLED, OUTPUT);
    digitalWrite(ACLED, HIGH);

    // Clear RAM
    randomSeed(analogRead(0));
    randomizeRAM();

    for (int sp = 0x01FF; sp >= 0x01C0; sp -= 2) {
        RAM_0000_BFFF[sp]     = 0x00;
        RAM_0000_BFFF[sp - 1] = 0xFF;
    }

    sd_begin();
    panel_begin();
    acia_init();
    m6800_reset();

    // --- ROM/Port selection logic ---
    bool resetDown = (digitalRead(RESETDOWN) == LOW);
    uint16_t addressSwitchValue = readAddressSwitches();

    // Group 1: SW0–SW3  (rightmost four switches)
    uint8_t nibble1 = (addressSwitchValue >> 0) & 0x0F;

    // Group 2: SW4–SW7
    uint8_t nibble2 = (addressSwitchValue >> 4) & 0x0F;

    // Group 2: SW8–SW11
    uint8_t nibble3 = (addressSwitchValue >> 8) & 0x0F;

    if (resetDown) {
        if (nibble1 == 0x0001) {
            activeROM = VTL2_ROM;
            msg.concat(": VTL-2 ROM loaded.");
        } else if (nibble1 == 0x0002)
        {
            activeROM = FLEX_ROM;
            msg.concat(": Flex ROM loaded.");
        } else if (nibble1 == 0x0000) {
            activeROM = MONITOR_ROM;
            msg.concat(": Monitor ROM loaded.");
        }

        switch (nibble3) {
            case 0x00:
                currentBaudRate = 9600;
                break;
            case 0x01:
                currentBaudRate = 110;
                break;
            case 0x02:
                currentBaudRate = 300;
                break;
            case 0x03:
                currentBaudRate = 2400;
                break;
            case 0x04:
                currentBaudRate = 4800;
                break;
            case 0x05:
                currentBaudRate = 19200;
                break;
            case 0x06:
                currentBaudRate = 38400;
                break;
            case 0x07:
                currentBaudRate = 57600;
                break;
            case 0x08:
                currentBaudRate = 115200;
                break;
        }

        if (nibble2 == 0x01) {
            activePort = &Serial1;
            currentSelectedPort = 1;
            Serial1.begin(currentBaudRate);
        } else if (nibble2 == 0x02) {
            activePort = &Serial2;
            currentSelectedPort = 2;
            Serial2.begin(currentBaudRate);
        } else {
            activePort = &Serial; // USB serial by default
            currentSelectedPort = 0;
            Serial.begin(currentBaudRate);
        }
    } else  {
        //load saved values (if exists)
        ConfigData config = loadConfig();

        switch (config.selectedROM) {
            case 0:
                activeROM = MONITOR_ROM;
                msg.concat(": Monitor ROM loaded.");
                break;
            case 1:
                activeROM = VTL2_ROM;
                msg.concat(": VTL-2 ROM loaded.");
                break;
            case 2:
                activeROM = FLEX_ROM;
                msg.concat(": Flex ROM loaded.");
                break;
            default:
                activeROM = MONITOR_ROM;
                msg.concat(": Monitor ROM loaded.");
                break;
        }

        switch (config.selectedPort) {
            case 0:
                activePort = &Serial;
                Serial.begin(config.baudRate);
                break;
            case 1:
                activePort = &Serial1;
                Serial1.begin(config.baudRate);
                break;
            case 2:
                activePort = &Serial2;
                Serial2.begin(config.baudRate);
                break;
            default:
                activePort = &Serial;
                Serial.begin(config.baudRate);
                break;
        }
        currentSelectedPort = config.selectedPort;

        currentBaudRate = config.baudRate;
    }

    detect_headless_at_boot();   // determine if we have a panel or not

    programInjectorBegin();

    activePort->setTimeout(2);  // Short timeout to flush lingering LF

    activePort->println(msg);

    updateStatusLeds();
}

void loop() {
    int32_t reason;

    reason = sim_instr(1);         // Execute one instruction per loop

    if (reason != 0) {
        //dump_regs();               // Print CPU state if halted
        //Serial.println("Simulation halted!");
        while (true);              // Stop the loop
    }
}

uint16_t readAddressSwitches() {
    uint16_t value = 0;
    for (int i = 0; i < 16; i++) {
        if (!digitalRead(addressSwitchPins[i])) {
            value |= (1 << i);
        }
    }
    return value;
}

uint8_t readDataSwitches() {
    uint8_t value = 0;
    for (int i = 0; i < 8; i++) {
        if (!digitalRead(dataSwitchPins[i])) {
            value |= (1 << i);
        }
    }
    return value;
}

void displayDataOnLEDs(uint8_t value) {
    for (int i = 0; i < 8; i++) {
        digitalWrite(dataLedPins[i], (value & (1 << i)) ? HIGH : LOW);
    }
}

void checkDepositAction(uint16_t address) {
    bool current = digitalRead(DEPOSIT);
    if (lastDepositState == HIGH && current == LOW) {
        // DEPOSIT pressed: store value
        uint8_t data = readDataSwitches();
        CPU_BD_put_mbyte(address, data);
        //RAM_0000_BFFF[address] = data;
        displayDataOnLEDs(data);
        pinMode(dataSwitchPins[1], INPUT_PULLUP);
    }
    lastDepositState = current;
}

void checkResetAction() {
    bool current = digitalRead(RESET);
    if (current == LOW) {
        // While RESET is held, light all panel LEDs
        lightAllPanelLeds(true);
    }
    if (lastResetState == HIGH && current == LOW) {
        // RESET pressed (edge detection)
        m6800_reset(); // or whatever your reset routine is
        check_basic = false;
        basic_ready_for_input = false;
        assembler_ready_for_input = false;
        vtl_ready_for_input = false;
        programInjectorAbort(true);
    }
    if (lastResetState == LOW && current == HIGH) {
        lightAllPanelLeds(false);
        updateStatusLeds();
    }
    lastResetState = current;
}

bool checkSaveConfig() {
    bool ret = false;

    // Check for chord: both toggles held down (LOW = active)
    bool resetDown  = (digitalRead(RESETDOWN) == LOW);
    bool depositDown = (digitalRead(DEPOSITDOWN) == LOW);

    static bool lastChord = false; // remember previous state

    if (resetDown && depositDown && !lastChord) {
        // Only trigger on the transition (to avoid multiple saves)
        saveConfig(currentSelectedPort, activeROM, currentBaudRate);

        // Optional: user feedback
        activePort->println("Config saved.");
        ret = true;
    }
    lastChord = resetDown && depositDown;

    return ret;
}


void showMemoryAtSwitches(uint16_t address) {
    if (g_headless) return; // no panel → nothing to show
    uint8_t value = CPU_BD_get_mbyte(address);
    displayDataOnLEDs(value);
}

bool isHaltMode() {
    if (g_headless) return false;      // never HALT in headless mode
    return digitalRead(HALT) == LOW; // HALT is active LOW
}

bool isRunMode() {
    if (g_headless) return true;       // always RUN in headless mode
    return digitalRead(RUN) == LOW; // RUN is active LOW
}

void flushSerialInput() {
    while (activePort->available()) {
        activePort->read(); // discard incoming bytes
    }
}

void updateStatusLeds() {
    if (isHaltMode()) {
        digitalWrite(HALTLED, HIGH);  // HALT LED ON
        digitalWrite(RUNLED, LOW);    // RUN LED OFF
    } else if (isRunMode()) {
        digitalWrite(HALTLED, LOW);   // HALT LED OFF
        digitalWrite(RUNLED, HIGH);   // RUN LED ON
    }
}

void lightAllPanelLeds(bool on) {
    for (int i = 0; i < 16; ++i) digitalWrite(addrLedPins[i], on ? HIGH : LOW);
    for (int i = 0; i < 8;  ++i) digitalWrite(dataLedPins[i], on ? HIGH : LOW);
    digitalWrite(RUNLED,  on ? HIGH : LOW);
    digitalWrite(HALTLED, on ? HIGH : LOW);
    digitalWrite(ACLED, HIGH);
} 

static inline void trimInPlace(String& s) {
  s.trim();                 // removes leading/trailing spaces, CR, LF, tabs
  // also strip possible Windows BOM or weird chars if needed later
}

void saveConfig(uint8_t port, uint8_t rom, unsigned long baudRate) {
    // write a human-readable INI to SD (if card present)
    char ini[160];
    // Use \r\n so it’s friendly to Windows editors too
    snprintf(ini, sizeof(ini),
             "CurrentROM=%u\r\n"
             "SerialPort=%u\r\n"
             "BaudRate=%lu\r\n",
             (unsigned)rom, (unsigned)port, baudRate);

    if (sd_present()) {
        if (!sd_write_text(CONFIG_PATH, ini)) {
            activePort->println("Warning: failed to write SD config.");
        } else {
            activePort->print("Config saved to SD: ");
            activePort->println(CONFIG_PATH);
        }
    } else {
        ConfigData config = { port, rom, baudRate, uint8_t(port ^ rom ^ baudRate ^ 0x55) };
        byte b2[sizeof(ConfigData)];
        memcpy(b2, &config, sizeof(ConfigData));
        dueFlashStorage.write(0, b2, sizeof(ConfigData));
        activePort->print("Config saved to flash memory. ");
    }
}

ConfigData loadConfig() {
    ConfigData config;
    // Start with flash values (backward-compatible)
    byte* b = dueFlashStorage.readAddress(0);
    memcpy(&config, b, sizeof(ConfigData));

    // If flash looks erased, seed defaults
    if (config.selectedPort == 0xFF && config.selectedROM == 0xFF && config.checksum == 0xFF) {
        config.selectedPort = 0;   // USB
        config.selectedROM  = 0;   // Monitor
        config.baudRate     = 9600;
        config.checksum     = uint8_t(config.selectedPort ^ config.selectedROM ^ config.baudRate ^ 0x55);
    }

    // If SD has an ini, let it override (friendly to edit on a PC)
    String text;
    if (sd_present() && sd_read_text(CONFIG_PATH, text)) {
        // Very simple INI parser: key=value per line, case-insensitive keys, spaces allowed
        uint8_t rom = config.selectedROM;
        uint8_t port = config.selectedPort;
        unsigned long baud = config.baudRate;

        int lineStart = 0;
        while (true) {
            int lineEnd = text.indexOf('\n', lineStart);
            String line = (lineEnd >= 0) ? text.substring(lineStart, lineEnd)
                                         : text.substring(lineStart);
            trimInPlace(line);
            // Skip blanks and comments
            if (line.length() && line.charAt(0) != ';' && line.charAt(0) != '#') {
                int eq = line.indexOf('=');
                if (eq > 0) {
                    String k = line.substring(0, eq);
                    String v = line.substring(eq + 1);
                    trimInPlace(k);
                    trimInPlace(v);
                    k.toLowerCase();

                    if (k == "currentrom") {
                        rom = (uint8_t) v.toInt();
                    } else if (k == "serialport") {
                        port = (uint8_t) v.toInt();
                    } else if (k == "baudrate") {
                        // allow underscores or spaces in numbers, e.g., "115_200"
                        v.replace("_", ""); v.replace(" ", "");
                        baud = (unsigned long) v.toInt();
                    }
                }
            }
            if (lineEnd < 0) break;
            lineStart = lineEnd + 1;
        }

        // Basic validation
        if (baud == 0) baud = 9600; // guard
        config.selectedROM  = rom;
        config.selectedPort = port;
        config.baudRate     = baud;
        config.checksum     = uint8_t(port ^ rom ^ baud ^ 0x55);

        // activePort->print("Config loaded from SD: ");
        // activePort->println(CONFIG_PATH);
    } else {
        // No SD or no INI; we’re using flash defaults
        // Write the defaults out so the card gets a file next time
        if (sd_present()) {
            saveConfig(config.selectedPort, config.selectedROM, config.baudRate);
        }
    }

    return config;
}

void onSerialOutput(char c) {
    output_buffer += c;

    if (output_buffer.length() > 12)
        output_buffer = output_buffer.substring(output_buffer.length() - 12);

    if (output_buffer.endsWith("J 0000")) {
        if (is_basic_loaded()) {
            resetSoftwareLoadedFlags();
            basic_ready_for_input = true;
        }
    }

    if (output_buffer.endsWith("J FC00")) {
        if (activeROM == VTL2_ROM) {
            resetSoftwareLoadedFlags();
            vtl_ready_for_input = true;
        }
    }

    if (output_buffer.endsWith(" 680 EDITOR ")) {
        resetSoftwareLoadedFlags();
        assembler_ready_for_input = true;
    }

    if (output_buffer.endsWith("J 0107") || output_buffer.endsWith("J 010A")) {
        if (is_assembler_loaded()) {
            resetSoftwareLoadedFlags();
            assembler_ready_for_input = true;
        }
    }
}

void resetSoftwareLoadedFlags() {
    assembler_ready_for_input = false;
    check_basic = false;
    basic_ready_for_input = false;
    vtl_ready_for_input = false;
}

void checkDepositDown() {
    bool currentDepositDown = digitalRead(DEPOSITDOWN);

    // Edge detection: only load on transition HIGH -> LOW
    if (lastDepositDown == HIGH && currentDepositDown == LOW) {
        // Check memory locations for indication of Altair 680 BASIC loaded
        if (is_basic_loaded() && basic_ready_for_input) {
            uint16_t address = readAddressSwitches();
            programInjectorStart(PROGRAM_BASIC, address);
        } else if (vtl_ready_for_input) {
            uint16_t address = readAddressSwitches();
            programInjectorStart(PROGRAM_VTL2, address);
        } else if (assembler_ready_for_input) {
            uint16_t address = readAddressSwitches();
            programInjectorStart(PROGRAM_ASM, address);
        }
    }
    lastDepositDown = currentDepositDown;
}

void checkLoadSoftware() {
    bool currentDepositDown = digitalRead(DEPOSITDOWN);
    bool resetDown  = (digitalRead(RESETDOWN) == LOW);

    // Edge detection: only load on transition HIGH -> LOW
    if (lastDepositDown == HIGH && currentDepositDown == LOW && !resetDown) {
        uint16_t address = readAddressSwitches();
        switch (address) {
            case 0x0001:
                loadAltairBasicImage();
                break;
            case 0x0002:
                loadAltairAssemblerImage();
                break;
            case 0x0003:
                loadKillTheBit();
                break;
        }
    }
    lastDepositDown = currentDepositDown;
}

void detect_headless_at_boot() {
    // Sample RUN and HALT inputs for ~50ms
    uint32_t t0 = millis();
    bool lowSeenRun  = false;
    bool lowSeenHalt = false;

    while (millis() - t0 < 50) {
        if (digitalRead(RUN)  == LOW) lowSeenRun  = true;
        if (digitalRead(HALT) == LOW) lowSeenHalt = true;
    }

    // If neither switch ever went LOW → assume nothing wired → headless
    g_headless = (!lowSeenRun && !lowSeenHalt);
}

void intercept_and_forward_host_input() {
    while (activePort->available()) {
        acia_receive_byte((uint8_t)activePort->read());
    }
}
