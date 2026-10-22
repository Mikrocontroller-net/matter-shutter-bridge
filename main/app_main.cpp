#include <driver/gpio.h>
#include <esp_log.h>
#include <esp_matter.h>
#include <esp_matter_console.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <nvs_flash.h>

#include "cli.h"
#include "covers.h"
#include "rf_tx.h"
#include "settings.h"

static const char *TAG = "app";

// Grove yellow wire to the Unit RF433T. HIGH keys the 433 MHz carrier, so the
// pin must be LOW before anything else runs.
static constexpr gpio_num_t RF_DATA_PIN = GPIO_NUM_2;
// Front button, active LOW. Holding it factory-resets the Matter pairing.
static constexpr gpio_num_t BUTTON_PIN = GPIO_NUM_41;
static constexpr uint32_t FACTORY_RESET_HOLD_MS = 5000;

static void rfPinLow() {
  gpio_reset_pin(RF_DATA_PIN);
  gpio_set_direction(RF_DATA_PIN, GPIO_MODE_OUTPUT);
  gpio_set_level(RF_DATA_PIN, 0);
}

static void buttonTask(void *) {
  gpio_reset_pin(BUTTON_PIN);
  gpio_set_direction(BUTTON_PIN, GPIO_MODE_INPUT);
  gpio_set_pull_mode(BUTTON_PIN, GPIO_PULLUP_ONLY);
  uint32_t heldMs = 0;
  for (;;) {
    vTaskDelay(pdMS_TO_TICKS(100));
    heldMs = gpio_get_level(BUTTON_PIN) == 0 ? heldMs + 100 : 0;
    if (heldMs >= FACTORY_RESET_HOLD_MS) {
      ESP_LOGW(TAG, "button held: factory reset, the device restarts unpaired");
      esp_matter::factory_reset();
      heldMs = 0;
    }
  }
}

static void onEvent(const ChipDeviceEvent *event, intptr_t) {
  namespace DeviceEventType = chip::DeviceLayer::DeviceEventType;
  switch (event->Type) {
    case DeviceEventType::kCommissioningComplete: ESP_LOGI(TAG, "commissioning complete"); break;
    case DeviceEventType::kFailSafeTimerExpired: ESP_LOGW(TAG, "commissioning failed: fail-safe expired"); break;
    case DeviceEventType::kCommissioningWindowOpened: ESP_LOGI(TAG, "commissioning window open"); break;
    case DeviceEventType::kCommissioningWindowClosed: ESP_LOGI(TAG, "commissioning window closed"); break;
    default: break;
  }
}

static esp_err_t onAttributeUpdate(esp_matter::attribute::callback_type_t, uint16_t, uint32_t, uint32_t,
                                   esp_matter_attr_val_t *, void *) {
  return ESP_OK;
}

extern "C" void app_main() {
  rfPinLow();
  if (rf_tx::init() != ESP_OK) ESP_LOGE(TAG, "radio unavailable; commands will not be transmitted");
  nvs_flash_init();
  settings::load();

  esp_matter::node::config_t nodeCfg;
  esp_matter::node_t *node = esp_matter::node::create(&nodeCfg, onAttributeUpdate, nullptr);
  if (!node || covers::create(node) != ESP_OK) {
    ESP_LOGE(TAG, "failed to create the Matter data model");
    return;
  }

  if (esp_matter::start(onEvent) != ESP_OK) {
    ESP_LOGE(TAG, "failed to start Matter");
    return;
  }
  covers::start();
  xTaskCreate(buttonTask, "button", 2048, nullptr, 3, nullptr);

  esp_matter::console::diagnostics_register_commands();
  esp_matter::console::factoryreset_register_commands();
  cli::registerCommands();
  esp_matter::console::init();
}
