#include "cli.h"

#include <string.h>

#include <esp_matter_console.h>
#include <esp_timer.h>
#include <stdio.h>

#include <config/command.h>

#include "covers.h"
#include "diag.h"
#include "rf_tx.h"
#include "settings.h"
#include "setup_code.h"

namespace cli {

// Calibration stopwatch: started by `cal <n> up|down`, read by `cal done`.
struct Calibration {
  bool active = false;
  size_t index = 0;
  bool up = false;
  int64_t startUs = 0;
};
static Calibration cal;

static void printShutters() {
  printf(" #  ep  name                              grp mem  up_ms down_ms ends mid\n");
  const size_t n = settings::count();
  for (size_t i = 0; i < n; i++) {
    const config::Shutter s = settings::shutter(i);
    printf("%2u %3u  %-32s  %3u %3u %6lu %7lu %-4s %s\n", (unsigned)(i + 1), covers::endpointOf(i), s.name, s.group,
           s.member, (unsigned long)s.upMs, (unsigned long)s.downMs, s.invertEnds ? "on" : "off",
           s.invertMid ? "on" : "off");
  }
}

static void printHouse() {
  const settings::Radio r = settings::radio();
  printf("2411 ID %06lx, %u repeats, %u shutters\n", (unsigned long)r.centralId, r.repeats,
         (unsigned)settings::count());
}

static void calibrate(const config::Command &c) {
  if (c.op == config::Op::CalStart) {
    if (c.index >= settings::count()) {
      printf("%s\n", config::describe(config::Error::BadIndex));
      return;
    }
    cal = {true, c.index, c.on, esp_timer_get_time()};
    covers::run(c.index, c.on);
    printf("shutter %u running %s; enter `matter esp cal done` the moment it stops\n", (unsigned)(c.index + 1),
           c.on ? "up" : "down");
    return;
  }
  if (!cal.active) {
    printf("no calibration running\n");
    return;
  }
  cal.active = false;
  if (c.op == config::Op::CalCancel) {
    covers::stop(cal.index);
    printf("calibration cancelled, STOP sent\n");
    return;
  }
  const uint32_t measured = (uint32_t)((esp_timer_get_time() - cal.startUs) / 1000);
  const config::Shutter s = settings::shutter(cal.index);
  const config::Error err =
      settings::setTimes(cal.index, cal.up ? measured : s.upMs, cal.up ? s.downMs : measured);
  if (err != config::Error::None) {
    printf("measured %lu ms: %s\n", (unsigned long)measured, config::describe(err));
    return;
  }
  covers::reconfigure(cal.index);
  printf("shutter %u: %s time %lu ms saved\n", (unsigned)(cal.index + 1), cal.up ? "up" : "down",
         (unsigned long)measured);
}

static void sendDirect(const config::Command &c) {
  const settings::Radio r = settings::radio();
  const fernotron::Command cmd = c.move == config::Move::Up     ? fernotron::Command::Up
                                 : c.move == config::Move::Down ? fernotron::Command::Down
                                                                : fernotron::Command::Stop;
  const bool queued = rf_tx::send({r.centralId, (uint8_t)c.a, (uint8_t)c.b}, cmd, r.repeats);
  printf("%s\n", queued ? "queued" : "radio refused the command");
}

// esp-matter passes only the words after the command; the parser wants the
// command word first.
static esp_err_t run(const char *word, int argc, char **argv) {
  const char *args[16];
  if (argc + 1 > (int)(sizeof(args) / sizeof(args[0]))) return ESP_ERR_INVALID_ARG;
  args[0] = word;
  for (int i = 0; i < argc; i++) args[i + 1] = argv[i];
  const config::Command c = config::parse(argc + 1, args);

  switch (c.op) {
    case config::Op::Invalid: printf("usage: matter esp %s\n", c.usage); return ESP_OK;
    case config::Op::List: printShutters(); return ESP_OK;
    case config::Op::ShowHouse: printHouse(); return ESP_OK;
    case config::Op::CalStart:
    case config::Op::CalDone:
    case config::Op::CalCancel: calibrate(c); return ESP_OK;
    case config::Op::Send: sendDirect(c); return ESP_OK;
    default: break;
  }

  const config::Error err = settings::apply(c);
  if (err != config::Error::None) {
    printf("%s\n", config::describe(err));
    return ESP_OK;
  }
  if (c.op == config::Op::CentralId || c.op == config::Op::Repeats) {
    printHouse();
  } else {
    covers::reconfigure(c.index);
    printShutters();
  }
  return ESP_OK;
}

static esp_err_t cmdShutter(int argc, char **argv) { return run("shutter", argc, argv); }
static esp_err_t cmdHouse(int argc, char **argv) { return run("house", argc, argv); }
static esp_err_t cmdCal(int argc, char **argv) { return run("cal", argc, argv); }
static esp_err_t cmdSend(int argc, char **argv) { return run("send", argc, argv); }

static esp_err_t cmdDiag(int argc, char **argv) {
  if (argc > 0 && strcmp(argv[0], "clear") == 0) {
    diag::clear();
  } else {
    diag::dump();
  }
  return ESP_OK;
}

static esp_err_t cmdStatus(int, char **) {
  covers::printStatus();
  return ESP_OK;
}

static esp_err_t cmdPairing(int, char **) {
  setup_code::print();
  return ESP_OK;
}

void registerCommands() {
  static const esp_matter::console::command_t commands[] = {
      {"shutter", "List shutters, or set name/address/times/invert of one", cmdShutter},
      {"house", "Show or set the 2411 ID and repeat count", cmdHouse},
      {"cal", "Measure travel time: cal <n> up|down, then cal done", cmdCal},
      {"send", "Transmit one command: send <group> <member> up|down|stop (0 = all)", cmdSend},
      {"diag", "Dump received Matter commands; 'diag clear' empties the log", cmdDiag},
      {"status", "Show each shutter's position and motion", cmdStatus},
      {"pairing", "Show this device's pairing code and QR code", cmdPairing},
  };
  esp_matter::console::add_commands(commands, sizeof(commands) / sizeof(commands[0]));
}

}  // namespace cli
