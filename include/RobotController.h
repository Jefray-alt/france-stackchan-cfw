#pragma once

#include "RobotState.h"
#include "FaceRenderer.h"

class RobotController
{
public:
  void begin();
  void update();
  void setState(RobotState newState);

private:
  RobotState state = RobotState::IDLE;
  RobotState lastState = RobotState::IDLE;
  FaceRenderer face;
  unsigned long lastFaceChangeState = 0;

  void handleTouch();
};