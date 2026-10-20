#include "diag.h"

#include <esp_log.h>
#include <freertos/FreeRTOS.h>

namespace diag {

static const char *TAG = "diag";
static constexpr size_t CAPACITY = 64;
static Entry entries[CAPACITY];
static size_t head = 0;  // next slot to write
static size_t count = 0;
static portMUX_TYPE mux = portMUX_INITIALIZER_UNLOCKED;

static const char *commandName(uint32_t cluster, uint32_t command) {
  if (cluster != 0x0102) return "?";  // Window Covering
  switch (command) {
    case 0x00: return "UpOrOpen";
    case 0x01: return "DownOrClose";
    case 0x02: return "StopMotion";
    case 0x05: return "GoToLiftPercentage";
    default: return "?";
  }
}

static void print(const Entry &e) {
  ESP_LOGI(TAG, "t=%lu ep=%u cluster=0x%04lx cmd=%s(0x%02lx) value=%ld fabric=%u", (unsigned long)e.ms, e.endpoint,
           (unsigned long)e.cluster, commandName(e.cluster, e.command), (unsigned long)e.command, (long)e.value,
           e.fabricIndex);
}

void record(const Entry &e) {
  portENTER_CRITICAL(&mux);
  entries[head] = e;
  head = (head + 1) % CAPACITY;
  if (count < CAPACITY) count++;
  portEXIT_CRITICAL(&mux);
  print(e);
}

void dump() {
  Entry copy[CAPACITY];
  size_t n;
  portENTER_CRITICAL(&mux);
  n = count;
  const size_t start = (head + CAPACITY - count) % CAPACITY;
  for (size_t i = 0; i < n; i++) copy[i] = entries[(start + i) % CAPACITY];
  portEXIT_CRITICAL(&mux);

  ESP_LOGI(TAG, "%u entries, oldest first", (unsigned)n);
  for (size_t i = 0; i < n; i++) print(copy[i]);
}

void clear() {
  portENTER_CRITICAL(&mux);
  head = 0;
  count = 0;
  portEXIT_CRITICAL(&mux);
}

}  // namespace diag
