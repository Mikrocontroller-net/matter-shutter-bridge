#include "rf_tx.h"

#include <atomic>

#include <driver/rmt_tx.h>
#include <esp_check.h>
#include <esp_log.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>

#include <fernotron/airtime.h>
#include <fernotron/batcher.h>
#include <fernotron/encoder.h>

namespace rf_tx {

static const char *TAG = "rf";

static constexpr gpio_num_t DATA_PIN = GPIO_NUM_2;
static constexpr uint32_t RESOLUTION_HZ = 400000;  // 2.5 us per tick
static constexpr uint8_t MAX_REPEATS = 8;
static constexpr int FRAME_TIMEOUT_MS = 1000;
// Alexa sends one command per shutter for a room, group or routine, about
// 200 ms apart; a batch leaves after 1 s without a new command, at most 3 s
// after its first. A timed STOP waits as long as the move it ends did.
static constexpr uint32_t BATCH_QUIET_MS = 1000;
static constexpr uint32_t BATCH_MAX_WAIT_MS = 3000;
// Silence after a command's last copy before the next command. Receivers
// ignored commands that followed another frame by 43 ms and acted on one after
// 20 s; 2 s worked in one test out of two.
static constexpr uint32_t GAP_MS = 2000;
// While idle, the task still wakes this often to refresh the airtime figure
// that activity() reports, since booked airtime expires over the hour.
static constexpr uint32_t IDLE_WAKE_MS = 10000;

struct Request {
  enum class Kind : uint8_t { Command, Members } kind;
  fernotron::Target target;
  fernotron::Command cmd;
  uint8_t repeats;
  uint8_t members[8];  // Members: bit m of members[g]
};

static rmt_channel_handle_t channel;
static rmt_encoder_handle_t encoder;
static QueueHandle_t requests;
static fernotron::Airtime airtime;
static rmt_symbol_word_t symbols[fernotron::MAX_MARKS];

// Written by the task, read by activity() from other tasks.
static std::atomic<bool> anyFrame{false};
static std::atomic<uint32_t> lastFrameMs{0};
static std::atomic<uint32_t> airtimeUsedMs{0};

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

static uint32_t nowMs() { return (uint32_t)(esp_timer_get_time() / 1000); }

// Task-owned state.
static fernotron::Batcher batcher(BATCH_QUIET_MS, BATCH_MAX_WAIT_MS, GAP_MS);
static uint32_t centralId = 0;
static uint8_t repeats = 1;
static uint8_t counter = 1;

static void accept(const Request &r) {
  if (r.kind == Request::Kind::Members) {
    batcher.clearMembers();
    for (uint8_t g = 1; g < 8; g++) {
      for (uint8_t m = 1; m < 8; m++) {
        if (r.members[g] & (1u << m)) batcher.addMember(g, m);
      }
    }
    return;
  }
  centralId = r.target.id;
  repeats = r.repeats;
  batcher.add(r.target.group, r.target.member, r.cmd, nowMs());
}

static void drain() {
  Request r;
  while (xQueueReceive(requests, &r, 0) == pdTRUE) accept(r);
}

static void sendCopy(const fernotron::Frame &frame, fernotron::Command cmd) {
  const uint32_t frameMs = frame.onAirUs() / 1000 + 1;
  const uint32_t now = nowMs();
  if (!airtime.tryConsume(now, frameMs)) {
    ESP_LOGE(TAG, "duty-cycle budget used up (%lu of %lu ms this hour); dropping a copy of %s",
             (unsigned long)airtime.usedMs(now), (unsigned long)airtime.budgetMs(), name(cmd));
    return;
  }
  anyFrame = true;
  lastFrameMs = now;
  airtimeUsedMs = airtime.usedMs(now);
  if (!transmitFrame(frame)) ESP_LOGE(TAG, "RMT transmit failed");
}

// One command: the next counter value, then `repeats` copies with it back to
// back, as a 2411 sends them.
static void transmit(const fernotron::Batcher::Frame &f) {
  fernotron::Message m;
  if (fernotron::buildMessage({centralId, f.group, f.member}, f.cmd, counter, m) != fernotron::Error::None) {
    ESP_LOGE(TAG, "cannot build %s for g%u m%u", name(f.cmd), f.group, f.member);
    return;
  }
  counter = fernotron::nextCounter(counter);
  ESP_LOGI(TAG, "%s g%u m%u (%u merged) x%u: %02x %02x %02x %02x %02x %02x", name(f.cmd), f.group, f.member, f.merged,
           repeats, m[0], m[1], m[2], m[3], m[4], m[5]);
  const fernotron::Frame frame = fernotron::encode(m);
  for (uint8_t i = 0; i < repeats; i++) sendCopy(frame, f.cmd);
}

// Rounded up, so a wait shorter than a tick does not turn into a busy loop.
static TickType_t ticksFor(uint32_t ms) { return (TickType_t)((ms + portTICK_PERIOD_MS - 1) / portTICK_PERIOD_MS); }

static void task(void *) {
  for (;;) {
    TickType_t wait = ticksFor(IDLE_WAKE_MS);
    if (batcher.pending()) wait = ticksFor(batcher.msUntilDue(nowMs()));
    Request r;
    if (xQueueReceive(requests, &r, wait) == pdTRUE) {
      accept(r);
      drain();
    }
    // One command per pass. Commands arriving meanwhile stay in the batcher,
    // where they can still merge or be replaced, until the gap has passed.
    fernotron::Batcher::Frame f;
    if (batcher.next(nowMs(), f)) {
      transmit(f);
      batcher.sent(nowMs());
    }
    airtimeUsedMs = airtime.usedMs(nowMs());
  }
}

esp_err_t init() {
  rmt_tx_channel_config_t chanCfg = {};
  chanCfg.gpio_num = DATA_PIN;
  chanCfg.clk_src = RMT_CLK_SRC_DEFAULT;
  chanCfg.resolution_hz = RESOLUTION_HZ;
  // Three of the S3's four TX blocks hold a whole frame (141 symbols and the end
  // marker), so nothing is refilled while it is on air: the refill interrupt
  // cannot run during flash writes, and a late refill corrupts the frame. The
  // status LED uses the fourth block.
  chanCfg.mem_block_symbols = 144;
  chanCfg.trans_queue_depth = 2;
  chanCfg.flags.init_level = 0;
  ESP_RETURN_ON_ERROR(rmt_new_tx_channel(&chanCfg, &channel), TAG, "RMT channel");

  rmt_copy_encoder_config_t encCfg = {};
  ESP_RETURN_ON_ERROR(rmt_new_copy_encoder(&encCfg, &encoder), TAG, "RMT encoder");
  ESP_RETURN_ON_ERROR(rmt_enable(channel), TAG, "RMT enable");

  requests = xQueueCreate(32, sizeof(Request));
  if (!requests || xTaskCreate(task, "rf_tx", 4096, nullptr, 6, nullptr) != pdPASS) return ESP_ERR_NO_MEM;
  return ESP_OK;
}

bool send(const fernotron::Target &target, fernotron::Command cmd, uint8_t repeats) {
  fernotron::Message probe{};
  if (fernotron::buildMessage(target, cmd, 1, probe) != fernotron::Error::None) return false;
  if (repeats < 1 || repeats > MAX_REPEATS) return false;
  Request r{Request::Kind::Command, target, cmd, repeats, {}};
  return requests && xQueueSend(requests, &r, 0) == pdTRUE;
}

Activity activity() { return {anyFrame, lastFrameMs, airtimeUsedMs, airtime.budgetMs()}; }

void setMembers(const uint8_t members[8]) {
  Request r{Request::Kind::Members, {}, fernotron::Command::Stop, 0, {}};
  for (int g = 0; g < 8; g++) r.members[g] = members[g];
  if (requests) xQueueSend(requests, &r, portMAX_DELAY);
}

}  // namespace rf_tx
