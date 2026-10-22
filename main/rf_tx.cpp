#include "rf_tx.h"

#include <driver/rmt_tx.h>
#include <esp_check.h>
#include <esp_log.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>

#include <fernotron/airtime.h>
#include <fernotron/encoder.h>

namespace rf_tx {

static const char *TAG = "rf";

static constexpr gpio_num_t DATA_PIN = GPIO_NUM_2;
static constexpr uint32_t RESOLUTION_HZ = 400000;  // 2.5 us per tick
static constexpr uint8_t MAX_REPEATS = 8;
static constexpr int FRAME_TIMEOUT_MS = 1000;

struct Request {
  fernotron::Target target;
  fernotron::Command cmd;
  uint8_t repeats;
};

static rmt_channel_handle_t channel;
static rmt_encoder_handle_t encoder;
static QueueHandle_t requests;
static fernotron::Airtime airtime;
static rmt_symbol_word_t symbols[fernotron::MAX_MARKS];

static uint16_t ticks(uint32_t us) { return (uint16_t)(us * 2 / 5); }

static const char *name(fernotron::Command cmd) {
  switch (cmd) {
    case fernotron::Command::Up: return "UP";
    case fernotron::Command::Down: return "DOWN";
    case fernotron::Command::Stop: return "STOP";
    default: return "?";
  }
}

// Sends one frame and waits until it, including its trailing gap, is out.
static bool transmitFrame(const fernotron::Frame &frame) {
  for (size_t i = 0; i < frame.count; i++) {
    symbols[i].level0 = 1;
    symbols[i].duration0 = ticks(frame.marks[i].highUs);
    symbols[i].level1 = 0;
    symbols[i].duration1 = ticks(frame.marks[i].lowUs);
  }
  rmt_transmit_config_t cfg = {};
  cfg.loop_count = 0;
  cfg.flags.eot_level = 0;
  if (rmt_transmit(channel, encoder, symbols, frame.count * sizeof(rmt_symbol_word_t), &cfg) != ESP_OK) return false;
  return rmt_tx_wait_all_done(channel, FRAME_TIMEOUT_MS) == ESP_OK;
}

static void task(void *) {
  uint8_t counter = 1;
  for (;;) {
    Request r;
    xQueueReceive(requests, &r, portMAX_DELAY);

    fernotron::Message msg{};
    if (fernotron::buildMessage(r.target, r.cmd, counter, msg) != fernotron::Error::None) continue;
    counter = fernotron::nextCounter(counter);

    const fernotron::Frame frame = fernotron::encode(msg);
    const uint32_t frameMs = frame.onAirUs() / 1000 + 1;
    ESP_LOGI(TAG, "%s g%u m%u x%u: %02x %02x %02x %02x %02x %02x", name(r.cmd), r.target.group, r.target.member,
             r.repeats, msg[0], msg[1], msg[2], msg[3], msg[4], msg[5]);

    for (uint8_t i = 0; i < r.repeats; i++) {
      const uint32_t now = (uint32_t)(esp_timer_get_time() / 1000);
      if (!airtime.tryConsume(now, frameMs)) {
        ESP_LOGE(TAG, "duty-cycle budget used up (%lu of %lu ms this hour); dropping %s",
                 (unsigned long)airtime.usedMs(now), (unsigned long)airtime.budgetMs(), name(r.cmd));
        break;
      }
      if (!transmitFrame(frame)) {
        ESP_LOGE(TAG, "RMT transmit failed");
        break;
      }
    }
  }
}

esp_err_t init() {
  rmt_tx_channel_config_t chanCfg = {};
  chanCfg.gpio_num = DATA_PIN;
  chanCfg.clk_src = RMT_CLK_SRC_DEFAULT;
  chanCfg.resolution_hz = RESOLUTION_HZ;
  chanCfg.mem_block_symbols = 96;  // two S3 blocks; the driver refills while sending
  chanCfg.trans_queue_depth = 2;
  chanCfg.flags.init_level = 0;
  ESP_RETURN_ON_ERROR(rmt_new_tx_channel(&chanCfg, &channel), TAG, "RMT channel");

  rmt_copy_encoder_config_t encCfg = {};
  ESP_RETURN_ON_ERROR(rmt_new_copy_encoder(&encCfg, &encoder), TAG, "RMT encoder");
  ESP_RETURN_ON_ERROR(rmt_enable(channel), TAG, "RMT enable");

  requests = xQueueCreate(8, sizeof(Request));
  if (!requests || xTaskCreate(task, "rf_tx", 4096, nullptr, 6, nullptr) != pdPASS) return ESP_ERR_NO_MEM;
  return ESP_OK;
}

bool send(const fernotron::Target &target, fernotron::Command cmd, uint8_t repeats) {
  fernotron::Message probe{};
  if (fernotron::buildMessage(target, cmd, 1, probe) != fernotron::Error::None) return false;
  if (repeats < 1 || repeats > MAX_REPEATS) return false;
  Request r{target, cmd, repeats};
  return requests && xQueueSend(requests, &r, 0) == pdTRUE;
}

}  // namespace rf_tx
