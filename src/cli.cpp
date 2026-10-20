#include "cli.h"

#include <Arduino.h>

#include "diag.h"
#include "matter_app.h"

namespace cli {

static char line[64];
static size_t len = 0;

static void run(const char *cmd) {
  if (strcmp(cmd, "status") == 0) {
    matter_app::printStatus();
  } else if (strcmp(cmd, "diag") == 0) {
    diag::dump();
  } else if (strcmp(cmd, "clear") == 0) {
    diag::clear();
    Serial.println("[cli] diag cleared");
  } else if (strcmp(cmd, "decommission") == 0) {
    matter_app::decommission();
  } else if (cmd[0] != '\0') {
    Serial.println("[cli] commands: status, diag, clear, decommission");
  }
}

void poll() {
  while (Serial.available()) {
    char ch = (char)Serial.read();
    if (ch == '\r' || ch == '\n') {
      line[len] = '\0';
      run(line);
      len = 0;
    } else if (len < sizeof(line) - 1) {
      line[len++] = ch;
    }
  }
}

}  // namespace cli
