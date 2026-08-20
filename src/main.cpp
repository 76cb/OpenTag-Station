#include <Arduino.h>

#include "application/application.hpp"

namespace {
opentag::application::Application application;
}

void setup() {
  application.setup();
}

void loop() {
  application.loop();
}
