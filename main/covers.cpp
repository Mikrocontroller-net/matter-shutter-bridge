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
#include "rf_tx.h"
#include "settings.h"

using namespace esp_matter;
using namespace chip::app::Clusters;

namespace covers {

static const char *TAG = "covers";

static constexpr uint32_t TICK_MS = 100;
static constexpr uint32_t REPORT_MS = 500;  // position report interval while moving

struct Event {
  enum class Kind : uint8_t { Move, Stop, Run, Reconfigure } kind;
  uint8_t index;
  uint16_t target;  // Move: Matter target; Run: OPEN or CLOSED
};

static QueueHandle_t events;

static uint32_t nowMs() { return (uint32_t)(esp_timer_get_time() / 1000); }

static void post(Event e) { xQueueSend(events, &e, 0); }

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
    post({Event::Kind::Stop, index_, 0});
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

static Cover covers[config::MAX_SHUTTERS];
static size_t count = 0;

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
static void fillCoverConfig(endpoint::window_covering::config_t &cfg, Delegate *delegate, uint16_t position) {
  auto &wc = cfg.window_covering;
  wc.type = chip::to_underlying(WindowCovering::Type::kRollerShadeExterior);
  wc.config_status = chip::to_underlying(WindowCovering::ConfigStatus::kOperational) |
                     chip::to_underlying(WindowCovering::ConfigStatus::kLiftPositionAware);
  wc.feature_flags = cluster::window_covering::feature::lift::get_id() |
                     cluster::window_covering::feature::position_aware_lift::get_id();
  const uint16_t reported = shutter::reportedPosition(position);
  wc.features.position_aware_lift.current_position_lift_percent_100ths = nullable<uint16_t>(reported);
  wc.features.position_aware_lift.target_position_lift_percent_100ths = nullable<uint16_t>(reported);
  wc.delegate = delegate;
}

static shutter::Config controllerConfig(const config::Shutter &s) {
  shutter::Config cfg;
  cfg.times = {s.upMs, s.downMs};
  cfg.flags.invertEnds = s.invertEnds;
  cfg.flags.invertMid = s.invertMid;
  return cfg;
}

// Tells the radio which motors are ours, so it knows when a room or the whole
// house can be moved with one frame.
static void syncRadioMembers() {
  uint8_t members[8] = {};
  for (size_t i = 0; i < count; i++) {
    const config::Shutter s = settings::shutter(i);
    members[s.group] |= (uint8_t)(1u << s.member);
  }
  rf_tx::setMembers(members);
}

esp_err_t create(node_t *node) {
  count = settings::count();
  syncRadioMembers();

  // Bridge layout: an Aggregator with one bridged node per shutter, each
  // carrying its own name.
  endpoint::aggregator::config_t aggregatorCfg;
  endpoint_t *aggregator = endpoint::aggregator::create(node, &aggregatorCfg, ENDPOINT_FLAG_NONE, nullptr);
  if (!aggregator) return ESP_FAIL;

  for (size_t i = 0; i < count; i++) {
    const config::Shutter s = settings::shutter(i);
    Cover &c = covers[i];
    c.delegate.setIndex((uint8_t)i);
    const uint16_t position = settings::savedPosition(i);
    c.controller = shutter::Controller(controllerConfig(s), position);
    endpoint::window_covering::config_t cfg(chip::to_underlying(WindowCovering::EndProductType::kRollerShutter));
    fillCoverConfig(cfg, &c.delegate, position);

    endpoint::bridged_node::config_t bridgedCfg;
    // Fixed per slot, so a renamed shutter is still the same device to Alexa.
    snprintf(bridgedCfg.bridged_device_basic_information.unique_id,
             sizeof(bridgedCfg.bridged_device_basic_information.unique_id), "fernomatter-slot%u", (unsigned)(i + 1));
    endpoint_t *ep = endpoint::bridged_node::create(node, &bridgedCfg, ENDPOINT_FLAG_BRIDGE, nullptr);
    if (!ep || endpoint::window_covering::add(ep, &cfg) != ESP_OK ||
        endpoint::set_parent_endpoint(ep, aggregator) != ESP_OK) {
      return ESP_FAIL;
    }
    cluster_t *info = cluster::get(ep, BridgedDeviceBasicInformation::Id);
    cluster::bridged_device_basic_information::attribute::create_node_label(info, const_cast<char *>(s.name),
                                                                            (uint16_t)strlen(s.name));

    c.endpoint = endpoint::get_id(ep);
    c.delegate.SetEndpoint(c.endpoint);
    hookCommandLogging(cluster::get(ep, WindowCovering::Id));
    ESP_LOGI(TAG, "endpoint %u: %s (group %u, member %u), position %u", c.endpoint, s.name, s.group, s.member,
             position);
  }
  return ESP_OK;
}

static void transmit(size_t i, shutter::Action action) {
  const config::Shutter s = settings::shutter(i);
  const settings::Radio radio = settings::radio();
  const fernotron::Command cmd = action == shutter::Action::Up     ? fernotron::Command::Up
                                 : action == shutter::Action::Down ? fernotron::Command::Down
                                                                   : fernotron::Command::Stop;
  if (!rf_tx::send({radio.centralId, s.group, s.member}, cmd, radio.repeats)) {
    ESP_LOGE(TAG, "radio refused a command for %s", s.name);
  }
}

static uint8_t operationalStatus(const shutter::Controller &ctl) {
  // Lift field (bits 2-3) and global field (bits 0-1) carry the same state.
  const uint8_t state = !ctl.moving()                                ? 0
                        : ctl.direction() == shutter::Direction::Up ? 1   // MovingUpOrOpen
                                                                    : 2;  // MovingDownOrClose
  return (uint8_t)(state | (state << 2));
}

// Publishes the estimate to Matter. `syncTarget` is set when a move has ended
// (arrived or stopped): the target follows the position and the position is
// kept for the next boot.
static void report(Cover &c, uint32_t now, bool syncTarget) {
  using namespace WindowCovering::Attributes;
  const uint16_t pos = c.controller.position(now);
  if (syncTarget && !c.controller.moving()) settings::savePosition((size_t)(&c - covers), pos);

  const auto reported = chip::app::DataModel::MakeNullable<chip::Percent100ths>(shutter::reportedPosition(pos));
  lock::ScopedChipStackLock lock(portMAX_DELAY);
  CurrentPositionLiftPercent100ths::Set(c.endpoint, reported);
  if (syncTarget) TargetPositionLiftPercent100ths::Set(c.endpoint, reported);
  OperationalStatus::Set(c.endpoint, chip::BitMask<WindowCovering::OperationalStatus>(operationalStatus(c.controller)));
  c.lastReportMs = now;
}

static void applySettings(size_t i, uint32_t now) {
  const config::Shutter s = settings::shutter(i);
  Cover &c = covers[i];
  c.controller.setTimes({s.upMs, s.downMs}, now);
  c.controller.flags() = controllerConfig(s).flags;
  char label[config::MAX_NAME_BYTES + 1];
  strncpy(label, s.name, sizeof(label));
  esp_matter_attr_val_t val = esp_matter_char_str(label, (uint16_t)strlen(label));
  lock::ScopedChipStackLock lock(portMAX_DELAY);
  attribute::update(c.endpoint, BridgedDeviceBasicInformation::Id,
                    BridgedDeviceBasicInformation::Attributes::NodeLabel::Id, &val);
}

static void handle(const Event &e, uint32_t now) {
  Cover &c = covers[e.index];
  shutter::Action action = shutter::Action::None;
  switch (e.kind) {
    case Event::Kind::Move: action = c.controller.goTo(e.target, now); break;
    case Event::Kind::Stop: action = c.controller.stop(now); break;
    case Event::Kind::Run:
      action = c.controller.run(e.target == shutter::OPEN ? shutter::Direction::Up : shutter::Direction::Down, now);
      break;
    case Event::Kind::Reconfigure: applySettings(e.index, now); return;
  }
  if (action != shutter::Action::None) transmit(e.index, action);
  report(c, now, e.kind != Event::Kind::Move || !c.controller.moving());
}

static void worker(void *) {
  for (;;) {
    Event e;
    if (xQueueReceive(events, &e, pdMS_TO_TICKS(TICK_MS)) == pdTRUE) handle(e, nowMs());

    const uint32_t now = nowMs();
    for (size_t i = 0; i < count; i++) {
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
  xTaskCreate(worker, "covers", 6144, nullptr, 5, nullptr);
}

void run(size_t index, bool up) {
  if (index < count) post({Event::Kind::Run, (uint8_t)index, up ? shutter::OPEN : shutter::CLOSED});
}

void stop(size_t index) {
  if (index < count) post({Event::Kind::Stop, (uint8_t)index, 0});
}

void reconfigure(size_t index) {
  if (index >= count) return;
  syncRadioMembers();
  post({Event::Kind::Reconfigure, (uint8_t)index, 0});
}

uint16_t endpointOf(size_t index) { return index < count ? covers[index].endpoint : 0; }

void printStatus() {
  const uint32_t now = nowMs();
  for (size_t i = 0; i < count; i++) {
    const Cover &c = covers[i];
    ESP_LOGI(TAG, "ep %u %-32s position %5u target %5u %s", c.endpoint, settings::shutter(i).name,
             c.controller.position(now), c.controller.target(), c.controller.moving() ? "moving" : "stopped");
  }
}

}  // namespace covers
