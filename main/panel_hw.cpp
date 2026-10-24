#include "panel_hw.h"

#include <atomic>

#include <app/server/Server.h>
#include <driver/gpio.h>
#include <esp_log.h>
#include <esp_matter.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <led_strip.h>
#include <platform/CHIPDeviceLayer.h>

#include <panel/panel.h>

#include "rf_tx.h"

namespace panel_hw {

static const char *TAG = "panel";

static constexpr gpio_num_t BUTTON_PIN = GPIO_NUM_41;  // active LOW
static constexpr gpio_num_t LED_PIN = GPIO_NUM_35;
static constexpr uint32_t TICK_MS = 20;
static constexpr uint32_t WINDOW_POLL_MS = 500;
// A window opened with the button stays open this long.
static constexpr uint32_t PAIRING_WINDOW_S = 900;

static led_strip_handle_t led;
static std::atomic<bool> wifiConnected{false};
static std::atomic<bool> pairingOpen{false};

void setWifiConnected(bool connected) { wifiConnected = connected; }

static uint32_t nowMs() { return (uint32_t)(esp_timer_get_time() / 1000); }

// The SDK posts no events when the window opens or closes, so the panel task
// asks the Matter task every WINDOW_POLL_MS.
static void pollPairingWindow(intptr_t) {
  pairingOpen = chip::Server::GetInstance().GetCommissioningWindowManager().IsCommissioningWindowOpen();
}

// Runs on the Matter task. Advertised over Wi-Fi only: Bluetooth is released
// after the first pairing, so the new controller must be on the same network.
static void openPairingWindow(intptr_t) {
  auto &window = chip::Server::GetInstance().GetCommissioningWindowManager();
  if (window.IsCommissioningWindowOpen()) return;
  const CHIP_ERROR err = window.OpenBasicCommissioningWindow(chip::System::Clock::Seconds32(PAIRING_WINDOW_S),
                                                             chip::CommissioningWindowAdvertisement::kDnssdOnly);
  if (err != CHIP_NO_ERROR) {
    ESP_LOGE(TAG, "cannot open the pairing window: %" CHIP_ERROR_FORMAT, err.Format());
  } else {
    ESP_LOGI(TAG, "pairing window open for %u s", (unsigned)PAIRING_WINDOW_S);
  }
}

static void show(panel::Color c) {
  if (!led) return;
  led_strip_set_pixel(led, 0, c.r, c.g, c.b);
  led_strip_refresh(led);
}

static void task(void *) {
  panel::Button button;
  panel::Color shown{};
  uint32_t lastPollMs = 0;
  for (;;) {
    vTaskDelay(pdMS_TO_TICKS(TICK_MS));
    const uint32_t now = nowMs();

    switch (button.update(gpio_get_level(BUTTON_PIN) == 0, now)) {
      case panel::Action::OpenPairing:
        ESP_LOGI(TAG, "button: opening the pairing window");
        if (chip::DeviceLayer::PlatformMgr().ScheduleWork(openPairingWindow) != CHIP_NO_ERROR) {
          ESP_LOGE(TAG, "cannot reach the Matter task to open the pairing window");
        }
        break;
      case panel::Action::FactoryReset:
        ESP_LOGW(TAG, "button: factory reset, the device restarts unpaired");
        esp_matter::factory_reset();
        break;
      case panel::Action::None: break;
    }

    if (now - lastPollMs >= WINDOW_POLL_MS) {
      // A poll that cannot be queued is simply repeated next time.
      (void)chip::DeviceLayer::PlatformMgr().ScheduleWork(pollPairingWindow);
      lastPollMs = now;
    }

    const rf_tx::Activity radio = rf_tx::activity();
    panel::Status status;
    status.pairingOpen = pairingOpen;
    status.wifiConnected = wifiConnected;
    status.anyFrame = radio.anyFrame;
    status.lastFrameMs = radio.lastFrameMs;
    status.airtimeUsedMs = radio.airtimeUsedMs;
    status.airtimeBudgetMs = radio.airtimeBudgetMs;
    const panel::Color c = panel::color(status, button.hold(), now);
    if (c != shown) {
      show(c);
      shown = c;
    }
  }
}

esp_err_t start() {
  gpio_reset_pin(BUTTON_PIN);
  gpio_set_direction(BUTTON_PIN, GPIO_MODE_INPUT);
  gpio_set_pull_mode(BUTTON_PIN, GPIO_PULLUP_ONLY);

  led_strip_config_t stripCfg = {};
  stripCfg.strip_gpio_num = LED_PIN;
  stripCfg.max_leds = 1;
  stripCfg.led_model = LED_MODEL_WS2812;
  stripCfg.color_component_format = LED_STRIP_COLOR_COMPONENT_FMT_GRB;
  led_strip_rmt_config_t rmtCfg = {};
  rmtCfg.clk_src = RMT_CLK_SRC_DEFAULT;
  rmtCfg.resolution_hz = 10 * 1000 * 1000;
  const esp_err_t ledErr = led_strip_new_rmt_device(&stripCfg, &rmtCfg, &led);
  if (ledErr != ESP_OK) {
    ESP_LOGE(TAG, "LED unavailable: %s", esp_err_to_name(ledErr));
    led = nullptr;
  }
  show({});

  if (xTaskCreate(task, "panel", 3072, nullptr, 3, nullptr) != pdPASS) return ESP_ERR_NO_MEM;
  return ledErr;
}

}  // namespace panel_hw
