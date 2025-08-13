#include "altair680_physical_ROM.h"
#include "vtl2_rom.h"
#include "flex_boot_rom.h"
#include "m6800.h"
#include "program_injector.h"
#include "acia_6850.h"
#include "platform_io.h"
#include "panel_state.h"
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
char StatusRegister = 0x02;
char ControlRegister = 0;
char ReceiveDataRegister = 0;
bool lastDepositState = HIGH;
bool lastResetState = HIGH;
uint32_t currentSelectedPort = 0;
unsigned long currentBaudRate = 9600;

// Address Switches (SW0 - SW15)
const int switchPins[16] = {
  62, 63, 64, 65, 66, 67, 68, 69,   // SW0-SW7
  31, 30, 23, 24,                   // SW8-SW11 - NOTE: change 17, 16 to 31, 30 when v1.0.1 boards arrive
  70, 71, 42, 43                    // SW12-SW15
};

// Address LEDs (A0 - A15)
const int ledPins[16] = {
  34, 35, 36, 37, 38, 39, 40, 41,   // A0-A7
  51, 50, 49, 48,                   // A8-A11
  47, 46, 45, 44                    // A12-A15
};

// Data LEDs (D0 - D7)
const int dataLedPins[8] = {25, 26, 27, 28, 14, 15, 29, 11};

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
static uint8_t controlReg = 0x00;
static uint8_t statusReg = 0x02;  // TDRE set (transmit buffer empty)
static uint8_t rxData = 0x00;
bool g_panel_override = false;

// ------- Debouncer for momentaries -------
struct DebouncedBtn {
  uint8_t pin;
  bool active_low;
  uint8_t stable;   // 0/1 logical
  uint8_t counter;
  bool fell;        // 1->0 edge (pressed for active_low)
  bool rose;        // 0->1 edge (released for active_low)
};

static DebouncedBtn btn_reset_down  { RESETDOWN,   true, 1, 0, false, false };
static DebouncedBtn btn_reset_up    { RESET,       true, 1, 0, false, false };
static DebouncedBtn btn_deposit_down{ DEPOSITDOWN, true, 1, 0, false, false };
static DebouncedBtn btn_deposit_up  { DEPOSIT,     true, 1, 0, false, false }; // for “store byte”

static inline uint8_t norm(uint8_t raw, bool al) { return al ? !raw : raw; }

static inline void deb_init_buttons() {
  pinMode(btn_reset_down.pin,   INPUT_PULLUP);
  pinMode(btn_reset_up.pin,     INPUT_PULLUP);
  pinMode(btn_deposit_down.pin, INPUT_PULLUP);
  pinMode(btn_deposit_up.pin,   INPUT_PULLUP);

  // Initialize stable to the real normalized level so we don't get a false edge at boot
  btn_reset_down.stable   = norm((uint8_t)digitalRead(btn_reset_down.pin),   btn_reset_down.active_low);
  btn_reset_up.stable     = norm((uint8_t)digitalRead(btn_reset_up.pin),     btn_reset_up.active_low);
  btn_deposit_down.stable = norm((uint8_t)digitalRead(btn_deposit_down.pin), btn_deposit_down.active_low);
  btn_deposit_up.stable   = norm((uint8_t)digitalRead(btn_deposit_up.pin),   btn_deposit_up.active_low);

  // clear counters/edges
  btn_reset_down.counter = btn_reset_up.counter = btn_deposit_down.counter = btn_deposit_up.counter = 0;
  btn_reset_down.fell = btn_reset_down.rose = false;
  btn_reset_up.fell = btn_reset_up.rose = false;
  btn_deposit_down.fell = btn_deposit_down.rose = false;
  btn_deposit_up.fell = btn_deposit_up.rose = false;
}

static inline void deb_update(DebouncedBtn& b) {
  uint8_t v = norm((uint8_t)digitalRead(b.pin), b.active_low);
  if (v == b.stable) {
    b.counter = 0;
    // DO NOT clear b.fell/b.rose here — leave them latched until read
    return;
  }
  if (b.counter < 0xFF) b.counter++;
  if (b.counter >= 4) {
    bool old = b.stable;
    b.stable = v;
    b.counter = 0;
    b.fell = (old == 1 && v == 0);
    b.rose = (old == 0 && v == 1);
  }
}

// RESETDOWN (pressed = toggle DOWN)
static inline bool reset_down_pressed()  { bool e = btn_reset_down.rose; btn_reset_down.rose = false; return e; }
static inline bool reset_down_released() { bool e = btn_reset_down.fell; btn_reset_down.fell = false; return e; }
static inline bool reset_down_held()     { return btn_reset_down.stable; }

// RESET (UP contact) — “held up”
static inline bool reset_up_pressed()    { bool e = btn_reset_up.rose;   btn_reset_up.rose   = false; return e; }
static inline bool reset_up_released()   { bool e = btn_reset_up.fell;   btn_reset_up.fell   = false; return e; }
static inline bool reset_up_held()       { return btn_reset_up.stable; }

// DEPOSITDOWN (toggle DOWN)
static inline bool deposit_down_pressed(){ bool e = btn_deposit_down.rose; btn_deposit_down.rose = false; return e; }
static inline bool deposit_down_released(){bool e = btn_deposit_down.fell; btn_deposit_down.fell = false; return e; }
static inline bool deposit_down_held()   { return btn_deposit_down.stable; }

// DEPOSIT (UP contact) — your “store byte” trigger
static inline bool deposit_up_pressed()  { bool e = btn_deposit_up.rose; btn_deposit_up.rose = false; return e; }
static inline bool deposit_up_released() { bool e = btn_deposit_up.fell; btn_deposit_up.fell = false; return e; }
static inline bool deposit_up_held()     { return btn_deposit_up.stable; }

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

int32_t CPU_BD_get_mbyte(int32_t addr) {
    if (addr >= 0x0000 && addr <= 0xBFFF) return RAM_0000_BFFF[addr];

    if (addr == 0xF000 || addr == 0xF001) {
        return acia_mmio_read((uint16_t)addr);
    }

    if (addr == 0xF002) return 0x40;

    if (activeROM == VTL2_ROM && addr >= 0xFC00 && addr <= 0xFEFF) return vtl2_rom[addr - 0xFC00];
    else if (activeROM == FLEX_ROM && addr >= 0xFC00 && addr <= 0xFCFF) return flex_boot_rom[addr - 0xFC00];

    if (addr >= 0xFF00 && addr <= 0xFFFF) return altair680b_rom[addr - 0xFF00];

    return 0xFF;
}

void CPU_BD_put_mbyte(int32_t addr, int32_t val) {
    if (addr >= 0x0000 && addr <= 0xBFFF) {
        RAM_0000_BFFF[addr] = val & 0xFF;
    }
    else if (addr == 0xF000 || addr == 0xF001) {
        acia_mmio_write((uint16_t)addr, (uint8_t)val);
    }
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

    deb_init_buttons();

    // Initialize switches as INPUT_PULLUP
    for (int i = 0; i < 16; i++) {
        pinMode(switchPins[i], INPUT_PULLUP);
    }

    for (int i = 0; i < 8; i++) {
        pinMode(dataSwitchPins[i], INPUT_PULLUP);
    }

    // Initialize LEDs as OUTPUT, turn them off initially
    for (int i = 0; i < 16; i++) {
        pinMode(ledPins[i], OUTPUT);
        digitalWrite(ledPins[i], LOW);
    }

    // Initialize data LED pins as outputs, and turn them off initially
    for (int i = 0; i < 8; i++) {
        pinMode(dataLedPins[i], OUTPUT);
        digitalWrite(dataLedPins[i], LOW);
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


    programInjectorBegin();

    //activePort->begin(currentBaudRate);
    activePort->setTimeout(2);  // Short timeout to flush lingering LF

    activePort->println(msg);

    showMemoryAtSwitches(0);    // show data at memory location 0
    updateStatusLeds();
}

void loop() {
    int32_t reason;

    uint16_t address = readAddressSwitches();
    updateFrontPanelLEDs(address);

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
        if (!digitalRead(switchPins[i])) {
            value |= (1 << i);
        }
    }
    return value;
}

void updateFrontPanelLEDs(uint16_t addressValue) {
    for (int i = 0; i < 16; i++) {
        digitalWrite(ledPins[i], (addressValue & (1 << i)) ? HIGH : LOW);
    }
    showMemoryAtSwitches(addressValue);
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

    // debounced levels from our helpers
    const bool bothHeld = reset_down_held() && deposit_down_held();

    static bool lastBoth = false;  // remember prior chord state
    if (bothHeld && !lastBoth) {
        // fire once on transition to "both held"
        saveConfig(currentSelectedPort, activeROM, currentBaudRate);
        if (activePort) activePort->println("Config saved.");
        ret = true;
    }
    lastBoth = bothHeld;

    return ret;
}


void showMemoryAtSwitches(uint16_t address) {
    uint8_t value = CPU_BD_get_mbyte(address);
    displayDataOnLEDs(value);
}

bool isHaltMode() {
    return digitalRead(HALT) == LOW; // HALT is active LOW
}

bool isRunMode() {
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
    for (int i = 0; i < 16; ++i) digitalWrite(ledPins[i],     on ? HIGH : LOW);
    for (int i = 0; i < 8;  ++i) digitalWrite(dataLedPins[i], on ? HIGH : LOW);
    digitalWrite(RUNLED,  on ? HIGH : LOW);
    digitalWrite(HALTLED, on ? HIGH : LOW);
    digitalWrite(ACLED, HIGH);
} 

void saveConfig(uint8_t port, uint8_t rom, unsigned long baudRate) {
    ConfigData config = { port, rom, baudRate, uint8_t(port ^ rom ^ baudRate ^ 0x55) }; // simple XOR checksum

    byte b2[sizeof(ConfigData)]; // create byte array to store the struct
    memcpy(b2, &config, sizeof(ConfigData)); // copy the struct to the byte array
    dueFlashStorage.write(0, b2, sizeof(ConfigData)); // address 0
}

ConfigData loadConfig() {
    ConfigData config;
    byte* b = dueFlashStorage.readAddress(0); // byte array which is read from flash at adress 4
    memcpy(&config, b, sizeof(ConfigData)); // copy byte array to temporary struct
    
    // Check for erased flash (0xFF)
    if (config.selectedPort == 0xFF && config.selectedROM == 0xFF && config.checksum == 0xFF) {
        // All erased: treat as first boot
        config.selectedPort = 0; // Default USB
        config.selectedROM = 0;  // Default monitor ROM
        config.baudRate = 9600;
    }

    return config;
}

void onSerialOutput(char c) {
    output_buffer += c;

    if (output_buffer.length() > 12)
        output_buffer = output_buffer.substring(output_buffer.length() - 12);

    if (output_buffer.endsWith("MEMORY SIZE?")) {
        check_basic = true;
    }

    if (output_buffer.endsWith(" 680 EDITOR ")) {
        assembler_ready_for_input = true;
    }

    if (output_buffer.endsWith("OK\r") || output_buffer.endsWith("OK\n")) {
        if (check_basic == true) {
            basic_ready_for_input = true;
        } else {
            vtl_ready_for_input = true;
        }
    }
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

    // Edge detection: only load on transition HIGH -> LOW
    if (lastDepositDown == HIGH && currentDepositDown == LOW) {
        uint16_t address = readAddressSwitches();
        switch (address) {
            case 0x0001:
                loadAltairBasicImage();
                break;
            case 0x0002:
                loadAltairAssemblerImage();
                break;
        }
    }
    lastDepositDown = currentDepositDown;
}

void pollFrontPanelButtons() {
    deb_update(btn_reset_down);
    deb_update(btn_reset_up);
    deb_update(btn_deposit_down);
    deb_update(btn_deposit_up);
}
