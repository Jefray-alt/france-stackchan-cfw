#pragma once

#include "RobotStateMachine.h"
#include "FaceRenderer.h"

class RobotController
{
public:
  void begin();
  void update();
  void setState(RobotState newState);

private:
  RobotStateMachine stateMachine;
  FaceRenderer face;

  void handleTouch();
  void notifyStateChange();
};
