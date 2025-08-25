#include "sd_card.h"
#include <SdFat.h>

// Classic SdFat v1.x API types
SdFat  g_sd;
static SdFile g_file;

static bool g_mounted = false;
static char g_err[64] = {0};
static enum { IDLE, LOADING, SAVING, ERROR_STATE } g_state = IDLE;

// Small ring buffer so SD I/O never stalls your panel/CPU timing
static uint8_t rb[1024];
static volatile size_t rb_head = 0, rb_tail = 0;
static inline size_t rb_count() { return (rb_head - rb_tail) & (sizeof(rb)-1); }
static inline size_t rb_space() { return sizeof(rb) - 1 - rb_count(); }
static inline void   rb_push(uint8_t b){ rb[rb_head] = b; rb_head = (rb_head + 1) & (sizeof(rb)-1); }
static inline bool   rb_pop(uint8_t* b){
  if (rb_count()==0) return false;
  *b = rb[rb_tail]; rb_tail = (rb_tail + 1) & (sizeof(rb)-1); return true;
}

static void set_err(const char* msg){
  strncpy(g_err, msg, sizeof(g_err)-1);
  g_state = ERROR_STATE;
}

bool sd_begin() {
  if (g_mounted) return true;

  // When CS is tied low, give SdFat a harmless, unconnected pin number so it won’t fight GND.
  const uint8_t cs = SD_CS_TIED_LOW ? SD_DUMMY_CS_PIN : SD_CS_PIN;

  if (!g_sd.begin(cs, SD_SCK_MHZ(SD_SPI_MHZ))) {
    set_err("SdFat.begin failed");
    g_mounted = false;
    return false;
  }

  g_err[0] = 0;
  g_state = IDLE;
  g_mounted = true;
  return true;
}

bool sd_present() { return g_mounted; }

void sd_list_root(Stream& out) {
  if (!sd_begin()) { out.println(sd_last_error()); return; }

  SdFile dir, f;
  if (!dir.open("/", O_READ)) { out.println("open / failed"); return; }

  while (f.openNext(&dir, O_RDONLY)) {
    char name[64]; f.getName(name, sizeof(name));
    out.print(name);
    if (f.isDir()) out.println("/");
    else { out.print("  "); out.println((uint32_t)f.fileSize()); }
    f.close();
  }
  dir.close();
}

bool sd_load_start(const char* path) {
  if (!sd_begin()) return false;
  if (g_state != IDLE) { set_err("busy"); return false; }

  if (!g_file.open(path, O_RDONLY)) { set_err("open READ failed"); return false; }
  rb_head = rb_tail = 0;
  g_state = LOADING;
  return true;
}

size_t sd_load_pump(uint8_t* dst, size_t max_bytes) {
  size_t n=0; uint8_t b;
  while (n<max_bytes && rb_pop(&b)) { dst[n++] = b; }
  return n;
}

bool sd_load_done() {
  return (g_state == IDLE) || (g_state == ERROR_STATE);
}

bool sd_save_start(const char* path) {
  if (!sd_begin()) return false;
  if (g_state != IDLE) { set_err("busy"); return false; }

  g_sd.remove(path); // overwrite
  if (!g_file.open(path, O_CREAT | O_TRUNC | O_WRONLY)) {
    set_err("open WRITE failed"); return false;
  }
  rb_head = rb_tail = 0;
  g_state = SAVING;
  return true;
}

size_t sd_save_pump(const uint8_t* src, size_t src_len) {
  size_t n=0;
  while (n<src_len && rb_space()>0) { rb_push(src[n++]); }
  return n; // bytes accepted into ring
}

bool sd_save_done() {
  return (g_state == IDLE) || (g_state == ERROR_STATE);
}

const char* sd_last_error() {
  return (g_err[0] ? g_err : "OK");
}

void sd_service() {
  if (!g_mounted) return;

  switch (g_state) {
    case LOADING: {
      // Fill ring from SD without hogging CPU
      int budget = 256;
      while (rb_space()>0 && budget>0) {
        int16_t c = g_file.read();  // returns -1 on EOF/error
        if (c < 0) break;
        rb_push((uint8_t)c);
        budget--;
      }
      // SdFile has no .available(); check position vs size
      if ((uint32_t)g_file.curPosition() >= (uint32_t)g_file.fileSize()) {
        g_file.close();
        g_state = IDLE;
      }
    } break;

    case SAVING: {
      int budget = 256;
      while (rb_count()>0 && budget>0) {
        uint8_t b; rb_pop(&b);
        if (g_file.write(&b, 1) != 1) { set_err("write failed"); break; }
        budget--;
      }
      static uint32_t lastFlush = 0;
      uint32_t now = micros();
      if ((uint32_t)(now - lastFlush) > 20000) { g_file.sync(); lastFlush = now; } // v1.x uses sync()
    } break;

    default: break;
  }
}

bool sd_write_text(const char* path, const char* content) {
  if (!sd_begin()) return false;
  // Overwrite
  g_sd.remove(path);
  SdFile f;
  if (!f.open(path, O_CREAT | O_TRUNC | O_WRONLY)) return false;
  size_t len = strlen(content);
  size_t wrote = f.write(content, len);
  f.sync();
  f.close();
  return wrote == len;
}

bool sd_read_text(const char* path, String& out) {
  out = "";
  if (!sd_begin()) return false;
  SdFile f;
  if (!f.open(path, O_RDONLY)) return false;

  const size_t BUF = 128;
  char buf[BUF];
  int n;
  while ((n = f.read(buf, BUF)) > 0) {
    out.concat(String(buf).substring(0, n));
  }
  f.close();
  return true;
}

