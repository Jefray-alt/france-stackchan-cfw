#pragma once

#include "RobotStateMachine.h"
#include "FaceRenderer.h"

class RobotController
{
public:
  void begin();
  void update();

private:
  RobotStateMachine stateMachine;
  RobotAudio audio;
  FaceRenderer face;

  void notifyStateChange();
};
