#pragma once

// Line-based serial commands. Type `help` in the serial monitor for the list.
namespace cli {

void poll();  // reads available serial input and runs complete lines

}  // namespace cli
