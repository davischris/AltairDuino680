#pragma once
#include <Arduino.h>
#include <SdFat.h> 

extern SdFat g_sd;

// ===== Build options =====
// Your current boards: CS is hard-wired LOW to GND.
#define SD_CS_TIED_LOW   1

// Next PCB rev: set SD_CS_TIED_LOW to 0 and wire CS to this pin:
#ifndef SD_CS_PIN
#define SD_CS_PIN        4     // you'll use D4 next rev
#endif

// SPI clock (Due is happy at 12 MHz; you can try 18–25 if stable)
#ifndef SD_SPI_MHZ
#define SD_SPI_MHZ       12
#endif

// When CS is tied low, SdFat still wants *some* pin number.
// Use an unconnected, harmless pin here (DO NOT wire this to the card).
#ifndef SD_DUMMY_CS_PIN
#define SD_DUMMY_CS_PIN  8
#endif

// Public API
bool   sd_begin();                        // mount card
bool   sd_present();                      // quick check
void   sd_list_root(Stream& out);         // list "/"

bool   sd_load_start(const char* path);   // begin non-blocking read
size_t sd_load_pump(uint8_t* dst, size_t max_bytes);
bool   sd_load_done();

bool   sd_save_start(const char* path);   // begin non-blocking write (overwrite)
size_t sd_save_pump(const uint8_t* src, size_t src_len);
bool   sd_save_done();

const char* sd_last_error();              // human-friendly error
void   sd_service();                      // call every 10 ms (or 1 ms; cheap)
bool sd_write_text(const char* path, const char* content);   // overwrite
bool sd_read_text (const char* path, String& out);           // read whole file