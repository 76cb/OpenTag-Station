#include <Arduino.h>

#include "application/application.hpp"

// Provisioning added a configured-boot JSON decode path to setup()/loop(). The
// supported Arduino-ESP32 weak override keeps real margin beyond the reduced
// compiler frames while runtime high-water diagnostics verify it on hardware.
SET_LOOP_TASK_STACK_SIZE(16384U);

namespace {
opentag::application::Application application;
}

void setup() {
  application.setup();
}

void loop() {
  application.loop();
}
