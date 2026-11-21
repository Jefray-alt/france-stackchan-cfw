#pragma once

#include <M5Unified.h>
#include <optional>
#include "RobotState.h"

class FaceRenderer
{
public:
  void begin();
  void setState(RobotState state);
  void update();
  void getLastState();

private:
  RobotState state = RobotState::IDLE;

  bool eyesClosed = false;
  unsigned long lastBlink = 0;
  uint16_t color = TFT_WHITE;

  void draw();
};