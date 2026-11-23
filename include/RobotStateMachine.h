#pragma once

#include <cstdint>
#include "RobotAudio.h"
#include "RobotState.h"

class RobotStateMachine
{
public:
  void begin(uint32_t now = 0);
  RobotState getState() const;
  bool update(bool wasPressed, uint32_t now,
      RobotAudio::Status audioStatus = RobotAudio::Status::IDLE);
  bool didFail() const { return operationFailed; }

private:
  RobotState state = RobotState::IDLE;
  uint32_t lastStateChange = 0;
  bool operationFailed = false;

  bool setState(RobotState newState, uint32_t now);
};
