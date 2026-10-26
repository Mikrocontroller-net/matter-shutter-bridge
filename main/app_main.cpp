#include <string.h>
#include <unistd.h>

#include <driver/gpio.h>
#include <driver/usb_serial_jtag.h>
#include <esp_log.h>
#include <esp_matter.h>
#include <esp_matter_console.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <linenoise/linenoise.h>
#include <nvs_flash.h>

#include "cli.h"
#include "covers.h"
#include "panel_hw.h"
#include "remote.h"
#include "rf_tx.h"
#include "settings.h"
#include "setup_code.h"

static const char *TAG = "app";

// Grove yellow wire to the Unit RF433T. HIGH keys the 433 MHz carrier, so the
// pin must be LOW before anything else runs.
static constexpr gpio_num_t RF_DATA_PIN = GPIO_NUM_2;
// The bridge's own name in Alexa (it is listed as a "Hub" next to the covers).
static constexpr const char *BRIDGE_NAME = "Rollladen-Bridge";

static void rfPinLow() {
  gpio_reset_pin(RF_DATA_PIN);
  gpio_set_direction(RF_DATA_PIN, GPIO_MODE_OUTPUT);
  gpio_set_level(RF_DATA_PIN, 0);
}

static void onEvent(const ChipDeviceEvent *event, intptr_t) {
  namespace DeviceEventType = chip::DeviceLayer::DeviceEventType;
  switch (event->Type) {
    case DeviceEventType::kCommissioningComplete: ESP_LOGI(TAG, "commissioning complete"); break;
    case DeviceEventType::kFailSafeTimerExpired: ESP_LOGW(TAG, "commissioning failed: fail-safe expired"); break;
    case DeviceEventType::kWiFiConnectivityChange: {
      const bool up = event->WiFiConnectivityChange.Result == chip::DeviceLayer::kConnectivity_Established;
      panel_hw::setWifiConnected(up);
      remote::setWifiConnected(up);
      break;
    }
    default: break;
  }
}

// The root node's NodeLabel is persisted from the first boot, so a firmware
// with a new default name would otherwise keep showing the old, empty one.
static void ensureBridgeName() {
  using namespace chip::app::Clusters;
  esp_matter::attribute_t *label =
      esp_matter::attribute::get(0, BasicInformation::Id, BasicInformation::Attributes::NodeLabel::Id);
  esp_matter_attr_val_t current = esp_matter_invalid(nullptr);
  if (!label || esp_matter::attribute::get_val(label, &current) != ESP_OK) return;
  if (current.val.a.s > 0) return;
  esp_matter_attr_val_t name = esp_matter_char_str(const_cast<char *>(BRIDGE_NAME), (uint16_t)strlen(BRIDGE_NAME));
  esp_matter::lock::ScopedChipStackLock lock(portMAX_DELAY);
  esp_matter::attribute::update(0, BasicInformation::Id, BasicInformation::Attributes::NodeLabel::Id, &name);
}

// ESP-IDF fails console reads at once while no USB host is connected
// (IDF-14303), so on a plain power supply the SDK's shell loop would spin,
// printing an empty line per pass. Waiting for a host keeps it idle.
static ssize_t readConsole(int fd, void *buf, size_t len) {
  while (!usb_serial_jtag_is_connected()) vTaskDelay(pdMS_TO_TICKS(500));
  return read(fd, buf, len);
}

static esp_err_t onAttributeUpdate(esp_matter::attribute::callback_type_t, uint16_t, uint32_t, uint32_t,
                                   esp_matter_attr_val_t *, void *) {
  return ESP_OK;
}

extern "C" void app_main() {
  rfPinLow();
  remote::captureLog();
  if (rf_tx::init() != ESP_OK) ESP_LOGE(TAG, "radio unavailable; commands will not be transmitted");
  nvs_flash_init();
  settings::load();
  setup_code::init();
  remote::init();

  esp_matter::node::config_t nodeCfg;
  strncpy(nodeCfg.root_node.basic_information.node_label, BRIDGE_NAME,
          sizeof(nodeCfg.root_node.basic_information.node_label) - 1);
  esp_matter::node_t *node = esp_matter::node::create(&nodeCfg, onAttributeUpdate, nullptr);
  if (!node || covers::create(node) != ESP_OK) {
    ESP_LOGE(TAG, "failed to create the Matter data model");
    return;
  }

  if (esp_matter::start(onEvent) != ESP_OK) {
    ESP_LOGE(TAG, "failed to start Matter");
    return;
  }
  ensureBridgeName();
  setup_code::print();
  if (remote::start() != ESP_OK) ESP_LOGE(TAG, "remote access unavailable");
  remote::print();
  covers::start();
  if (panel_hw::start() != ESP_OK) ESP_LOGE(TAG, "status LED unavailable; the button still works");

  esp_matter::console::diagnostics_register_commands();
  esp_matter::console::factoryreset_register_commands();
  cli::registerCommands();
  esp_matter::console::init();
  linenoiseSetReadFunction(readConsole);
}
