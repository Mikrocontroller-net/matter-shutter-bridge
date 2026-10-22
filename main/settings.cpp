#include "settings.h"

#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <nvs.h>

namespace settings {

static const char *TAG = "settings";
static const char *NVS_NAMESPACE = "fernomatter";
static const char *NVS_KEY = "house";

static config::House house;
static SemaphoreHandle_t mutex;

// Caller holds the mutex.
static bool save() {
  uint8_t blob[config::MAX_BLOB];
  const size_t len = config::serialize(house, blob, sizeof(blob));
  nvs_handle_t nvs;
  if (len == 0 || nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs) != ESP_OK) return false;
  const bool ok = nvs_set_blob(nvs, NVS_KEY, blob, len) == ESP_OK && nvs_commit(nvs) == ESP_OK;
  nvs_close(nvs);
  if (!ok) ESP_LOGE(TAG, "could not write configuration to NVS");
  return ok;
}

void load() {
  mutex = xSemaphoreCreateMutex();
  uint8_t blob[config::MAX_BLOB];
  size_t len = sizeof(blob);
  nvs_handle_t nvs;
  config::Error err = config::Error::BadBlob;
  if (nvs_open(NVS_NAMESPACE, NVS_READONLY, &nvs) == ESP_OK) {
    if (nvs_get_blob(nvs, NVS_KEY, blob, &len) == ESP_OK) err = config::deserialize(blob, len, house);
    nvs_close(nvs);
  }
  if (err != config::Error::None) {
    ESP_LOGW(TAG, "no valid configuration stored (%s); using defaults", config::describe(err));
    house = config::defaults();
    save();
  }
}

config::House get() {
  xSemaphoreTake(mutex, portMAX_DELAY);
  const config::House copy = house;
  xSemaphoreGive(mutex);
  return copy;
}

template <typename Edit>
static config::Error change(Edit edit) {
  xSemaphoreTake(mutex, portMAX_DELAY);
  const config::Error err = edit(house);
  if (err == config::Error::None) save();
  xSemaphoreGive(mutex);
  return err;
}

config::Error apply(const config::Command &c) {
  return change([&c](config::House &h) { return config::apply(h, c); });
}

config::Error setTimes(size_t index, uint32_t upMs, uint32_t downMs) {
  return change([=](config::House &h) { return config::setTimes(h, index, upMs, downMs); });
}

}  // namespace settings
