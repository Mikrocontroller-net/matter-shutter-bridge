#include "settings.h"

#include <string.h>

#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <nvs.h>

namespace settings {

static const char *TAG = "settings";
// Fixed: a different namespace would leave the stored configuration behind.
static const char *NVS_NAMESPACE = "fernomatter";
static const char *NVS_KEY = "house";
static const char *NVS_KEY_POSITIONS = "positions";
static const char *NVS_KEY_SETUP_CODE = "setup_code";
static const char *NVS_KEY_REMOTE_TOKEN = "remote_token";

static config::House house;
static SemaphoreHandle_t mutex;
static uint8_t blob[config::MAX_BLOB];  // NVS image; used under the mutex or before tasks start
static uint16_t positions[config::MAX_SHUTTERS];

// Caller holds the mutex.
static bool save() {
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

  size_t posLen = sizeof(positions);
  if (nvs_open(NVS_NAMESPACE, NVS_READONLY, &nvs) == ESP_OK) {
    if (nvs_get_blob(nvs, NVS_KEY_POSITIONS, positions, &posLen) != ESP_OK || posLen != sizeof(positions)) {
      memset(positions, 0, sizeof(positions));
    }
    nvs_close(nvs);
  }
  for (uint16_t &p : positions) {
    if (p > 10000) p = 0;
  }
}

bool loadSetupCode(pairing::Code &out) {
  uint8_t data[pairing::BLOB_BYTES];
  size_t len = sizeof(data);
  nvs_handle_t nvs;
  if (nvs_open(NVS_NAMESPACE, NVS_READONLY, &nvs) != ESP_OK) return false;
  const bool ok = nvs_get_blob(nvs, NVS_KEY_SETUP_CODE, data, &len) == ESP_OK && pairing::deserialize(data, len, out);
  nvs_close(nvs);
  return ok;
}

void saveSetupCode(const pairing::Code &code) {
  uint8_t data[pairing::BLOB_BYTES];
  const size_t len = pairing::serialize(code, data, sizeof(data));
  nvs_handle_t nvs;
  if (len == 0 || nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs) != ESP_OK) {
    ESP_LOGE(TAG, "could not write the setup code to NVS");
    return;
  }
  if (nvs_set_blob(nvs, NVS_KEY_SETUP_CODE, data, len) != ESP_OK || nvs_commit(nvs) != ESP_OK) {
    ESP_LOGE(TAG, "could not write the setup code to NVS");
  }
  nvs_close(nvs);
}

bool loadRemoteToken(remote::Token &out) {
  size_t len = out.size();
  nvs_handle_t nvs;
  if (nvs_open(NVS_NAMESPACE, NVS_READONLY, &nvs) != ESP_OK) return false;
  const bool ok = nvs_get_str(nvs, NVS_KEY_REMOTE_TOKEN, out.data(), &len) == ESP_OK && remote::isValidToken(out.data());
  nvs_close(nvs);
  return ok;
}

void saveRemoteToken(const remote::Token &token) {
  nvs_handle_t nvs;
  if (nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs) != ESP_OK) {
    ESP_LOGE(TAG, "could not write the remote token to NVS");
    return;
  }
  if (nvs_set_str(nvs, NVS_KEY_REMOTE_TOKEN, token.data()) != ESP_OK || nvs_commit(nvs) != ESP_OK) {
    ESP_LOGE(TAG, "could not write the remote token to NVS");
  }
  nvs_close(nvs);
}

uint16_t savedPosition(size_t index) {
  if (index >= config::MAX_SHUTTERS) return 0;
  xSemaphoreTake(mutex, portMAX_DELAY);
  const uint16_t p = positions[index];
  xSemaphoreGive(mutex);
  return p;
}

void savePosition(size_t index, uint16_t position) {
  if (index >= config::MAX_SHUTTERS) return;
  xSemaphoreTake(mutex, portMAX_DELAY);
  if (positions[index] != position) {
    positions[index] = position;
    nvs_handle_t nvs;
    if (nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs) == ESP_OK) {
      if (nvs_set_blob(nvs, NVS_KEY_POSITIONS, positions, sizeof(positions)) != ESP_OK || nvs_commit(nvs) != ESP_OK) {
        ESP_LOGE(TAG, "could not write positions to NVS");
      }
      nvs_close(nvs);
    }
  }
  xSemaphoreGive(mutex);
}

size_t count() {
  xSemaphoreTake(mutex, portMAX_DELAY);
  const size_t n = house.count;
  xSemaphoreGive(mutex);
  return n;
}

config::Shutter shutter(size_t index) {
  xSemaphoreTake(mutex, portMAX_DELAY);
  const config::Shutter s = house.shutters[index];
  xSemaphoreGive(mutex);
  return s;
}

Radio radio() {
  xSemaphoreTake(mutex, portMAX_DELAY);
  const Radio r{house.centralId, house.repeats};
  xSemaphoreGive(mutex);
  return r;
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
