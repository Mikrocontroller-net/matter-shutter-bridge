#include "covers.h"

#include <string.h>

#include <app-common/zap-generated/attributes/Accessors.h>
#include <app/CommandHandler.h>
#include <app/clusters/window-covering-server/window-covering-delegate.h>
#include <esp_log.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>
#include <lib/support/TypeTraits.h>

#include <fernotron/message.h>
#include <shutter/controller.h>

#include "diag.h"

using namespace esp_matter;
using namespace chip::app::Clusters;

namespace covers {

static const char *TAG = "covers";

// The house: one 2411 group per room, two shutters each. Names are what the
// bridged layout shows in Alexa; rename them here until the config store
// exists. Order fixes the endpoint IDs, so only ever append.
struct ShutterSpec {
  const char *name;
  uint8_t group;
  uint8_t member;
};
static constexpr ShutterSpec SHUTTERS[] = {
    {"Raum 1 links", 1, 1}, {"Raum 1 rechts", 1, 2}, {"Raum 2 links", 2, 1}, {"Raum 2 rechts", 2, 2},
    {"Raum 3 links", 3, 1}, {"Raum 3 rechts", 3, 2}, {"Raum 4 links", 4, 1}, {"Raum 4 rechts", 4, 2},
};
static constexpr size_t COUNT = sizeof(SHUTTERS) / sizeof(SHUTTERS[0]);

static constexpr uint32_t TICK_MS = 100;
static constexpr uint32_t REPORT_MS = 500;  // position report interval while moving

struct Event {
  enum class Kind : uint8_t { Move, Stop } kind;
  uint8_t index;
  uint16_t target;
};

static QueueHandle_t events;

static uint32_t nowMs() { return (uint32_t)(esp_timer_get_time() / 1000); }

// Runs on the Matter task after the cluster server has stored the new target:
// read it, queue it and return.
class Delegate : public WindowCovering::WindowCoveringDelegate {
 public:
  void setIndex(uint8_t index) { index_ = index; }

  CHIP_ERROR HandleMovement(WindowCovering::WindowCoveringType type) override {
    if (type != WindowCovering::WindowCoveringType::Lift) return CHIP_NO_ERROR;
    chip::app::DataModel::Nullable<chip::Percent100ths> target;
    if (WindowCovering::Attributes::TargetPositionLiftPercent100ths::Get(mEndpoint, target) !=
            chip::Protocols::InteractionModel::Status::Success ||
        target.IsNull()) {
      return CHIP_NO_ERROR;
    }
    Event e{Event::Kind::Move, index_, target.Value()};
    return xQueueSend(events, &e, 0) == pdTRUE ? CHIP_NO_ERROR : CHIP_ERROR_NO_MEMORY;
  }

  // IN_PROGRESS keeps the server from setting target = current itself; the
  // worker does that once the estimate is frozen.
  CHIP_ERROR HandleStopMotion() override {
    Event e{Event::Kind::Stop, index_, 0};
    xQueueSend(events, &e, 0);
    return CHIP_ERROR_IN_PROGRESS;
  }

 private:
  uint8_t index_ = 0;
};

struct Cover {
  Delegate delegate;
  uint16_t endpoint = 0;
  shutter::Controller controller{shutter::Config{}, shutter::OPEN};
  uint32_t lastReportMs = 0;
};

static Cover covers[COUNT];
static uint8_t counter = 1;  // Fernotron per-command counter, shared by all shutters on one sender ID

// Runs on the Matter task before the cluster server handles the command.
static esp_err_t logCommand(const chip::app::ConcreteCommandPath &path, chip::TLV::TLVReader &tlv, void *opaque) {
  int32_t value = -1;
  if (path.mClusterId == WindowCovering::Id && path.mCommandId == WindowCovering::Commands::GoToLiftPercentage::Id) {
    WindowCovering::Commands::GoToLiftPercentage::DecodableType data;
    if (chip::app::DataModel::Decode(tlv, data) == CHIP_NO_ERROR) value = data.liftPercent100thsValue;
  }
  auto *handler = static_cast<chip::app::CommandHandler *>(opaque);
  const uint8_t fabric = handler ? handler->GetAccessingFabricIndex() : 0;
  diag::record({nowMs(), path.mEndpointId, path.mClusterId, path.mCommandId, value, fabric});
  return ESP_OK;
}

static void hookCommandLogging(cluster_t *cluster) {
  const uint32_t ids[] = {WindowCovering::Commands::UpOrOpen::Id, WindowCovering::Commands::DownOrClose::Id,
                          WindowCovering::Commands::StopMotion::Id, WindowCovering::Commands::GoToLiftPercentage::Id};
  for (uint32_t id : ids) {
    command_t *cmd = command::get(cluster, id, COMMAND_FLAG_ACCEPTED);
    if (cmd) command::set_user_callback(cmd, logCommand);
  }
}

// Lift only, position aware. Type and end product type are cached by Alexa at
// pairing, so they must be right before the first commissioning.
static void fillCoverConfig(endpoint::window_covering::config_t &cfg, Delegate *delegate) {
  auto &wc = cfg.window_covering;
  wc.type = chip::to_underlying(WindowCovering::Type::kRollerShadeExterior);
  wc.config_status = chip::to_underlying(WindowCovering::ConfigStatus::kOperational) |
                     chip::to_underlying(WindowCovering::ConfigStatus::kLiftPositionAware);
  wc.feature_flags = cluster::window_covering::feature::lift::get_id() |
                     cluster::window_covering::feature::position_aware_lift::get_id();
  wc.features.position_aware_lift.current_position_lift_percent_100ths = nullable<uint16_t>(shutter::OPEN);
  wc.features.position_aware_lift.target_position_lift_percent_100ths = nullable<uint16_t>(shutter::OPEN);
  wc.delegate = delegate;
}

esp_err_t create(node_t *node) {
  shutter::Config controllerCfg;
  controllerCfg.times = {CONFIG_FERNO_SIM_UP_MS, CONFIG_FERNO_SIM_DOWN_MS};

#if CONFIG_FERNO_BRIDGE_LAYOUT
  endpoint::aggregator::config_t aggregatorCfg;
  endpoint_t *aggregator = endpoint::aggregator::create(node, &aggregatorCfg, ENDPOINT_FLAG_NONE, nullptr);
  if (!aggregator) return ESP_FAIL;
#endif

  for (size_t i = 0; i < COUNT; i++) {
    Cover &c = covers[i];
    c.delegate.setIndex((uint8_t)i);
    c.controller = shutter::Controller(controllerCfg, shutter::OPEN);
    endpoint::window_covering::config_t cfg(chip::to_underlying(WindowCovering::EndProductType::kRollerShutter));
    fillCoverConfig(cfg, &c.delegate);

#if CONFIG_FERNO_BRIDGE_LAYOUT
    endpoint::bridged_node::config_t bridgedCfg;
    snprintf(bridgedCfg.bridged_device_basic_information.unique_id,
             sizeof(bridgedCfg.bridged_device_basic_information.unique_id), "fernomatter-g%um%u", SHUTTERS[i].group,
             SHUTTERS[i].member);
    endpoint_t *ep = endpoint::bridged_node::create(node, &bridgedCfg, ENDPOINT_FLAG_BRIDGE, nullptr);
    if (!ep || endpoint::window_covering::add(ep, &cfg) != ESP_OK ||
        endpoint::set_parent_endpoint(ep, aggregator) != ESP_OK) {
      return ESP_FAIL;
    }
    cluster_t *info = cluster::get(ep, BridgedDeviceBasicInformation::Id);
    cluster::bridged_device_basic_information::attribute::create_node_label(
        info, const_cast<char *>(SHUTTERS[i].name), (uint16_t)strlen(SHUTTERS[i].name));
#else
    endpoint_t *ep = endpoint::window_covering::create(node, &cfg, ENDPOINT_FLAG_NONE, nullptr);
    if (!ep) return ESP_FAIL;
#endif

    c.endpoint = endpoint::get_id(ep);
    c.delegate.SetEndpoint(c.endpoint);
    hookCommandLogging(cluster::get(ep, WindowCovering::Id));
    ESP_LOGI(TAG, "endpoint %u: %s (group %u, member %u)", c.endpoint, SHUTTERS[i].name, SHUTTERS[i].group,
             SHUTTERS[i].member);
  }
  return ESP_OK;
}

// Logs the frame rf_tx will send once the radio exists.
static void transmit(size_t i, shutter::Action action) {
  fernotron::Command cmd = action == shutter::Action::Up     ? fernotron::Command::Up
                           : action == shutter::Action::Down ? fernotron::Command::Down
                                                             : fernotron::Command::Stop;
  fernotron::Message msg{};
  const fernotron::Error err =
      fernotron::buildMessage({CONFIG_FERNO_CENTRAL_ID, SHUTTERS[i].group, SHUTTERS[i].member}, cmd, counter, msg);
  if (err != fernotron::Error::None) {
    ESP_LOGE(TAG, "cannot build frame for %s: error %d", SHUTTERS[i].name, (int)err);
    return;
  }
  counter = fernotron::nextCounter(counter);
  ESP_LOGI(TAG, "[rf] %s %s: %02x %02x %02x %02x %02x %02x", SHUTTERS[i].name,
           cmd == fernotron::Command::Up ? "UP" : cmd == fernotron::Command::Down ? "DOWN" : "STOP", msg[0], msg[1],
           msg[2], msg[3], msg[4], msg[5]);
}

static uint8_t operationalStatus(const shutter::Controller &ctl) {
  // Lift field (bits 2-3) and global field (bits 0-1) carry the same state.
  const uint8_t state = !ctl.moving()                                     ? 0
                        : ctl.direction() == shutter::Direction::Up ? 1  // MovingUpOrOpen
                                                                    : 2; // MovingDownOrClose
  return (uint8_t)(state | (state << 2));
}

static void report(Cover &c, uint32_t now, bool syncTarget) {
  using namespace WindowCovering::Attributes;
  const uint16_t pos = c.controller.position(now);
  lock::ScopedChipStackLock lock(portMAX_DELAY);
  CurrentPositionLiftPercent100ths::Set(c.endpoint, chip::app::DataModel::MakeNullable<chip::Percent100ths>(pos));
  if (syncTarget) {
    TargetPositionLiftPercent100ths::Set(c.endpoint, chip::app::DataModel::MakeNullable<chip::Percent100ths>(pos));
  }
  OperationalStatus::Set(c.endpoint, chip::BitMask<WindowCovering::OperationalStatus>(operationalStatus(c.controller)));
  c.lastReportMs = now;
}

static void handle(const Event &e, uint32_t now) {
  Cover &c = covers[e.index];
  const shutter::Action action =
      e.kind == Event::Kind::Stop ? c.controller.stop(now) : c.controller.goTo(e.target, now);
  if (action != shutter::Action::None) transmit(e.index, action);
  report(c, now, e.kind == Event::Kind::Stop || !c.controller.moving());
}

static void worker(void *) {
  for (;;) {
    Event e;
    if (xQueueReceive(events, &e, pdMS_TO_TICKS(TICK_MS)) == pdTRUE) handle(e, nowMs());

    const uint32_t now = nowMs();
    for (size_t i = 0; i < COUNT; i++) {
      Cover &c = covers[i];
      const bool wasMoving = c.controller.moving();
      const shutter::Action action = c.controller.tick(now);
      if (action != shutter::Action::None) transmit(i, action);
      if (wasMoving && !c.controller.moving()) {
        report(c, now, true);  // arrived: final position and Stall
      } else if (c.controller.moving() && now - c.lastReportMs >= REPORT_MS) {
        report(c, now, false);
      }
    }
  }
}

void start() {
  events = xQueueCreate(32, sizeof(Event));
  xTaskCreate(worker, "covers", 4096, nullptr, 5, nullptr);
}

void printStatus() {
  const uint32_t now = nowMs();
  for (size_t i = 0; i < COUNT; i++) {
    const Cover &c = covers[i];
    ESP_LOGI(TAG, "ep %u %-14s g%u m%u position %5u target %5u %s", c.endpoint, SHUTTERS[i].name, SHUTTERS[i].group,
             SHUTTERS[i].member, c.controller.position(now), c.controller.target(),
             c.controller.moving() ? "moving" : "stopped");
  }
}

}  // namespace covers
