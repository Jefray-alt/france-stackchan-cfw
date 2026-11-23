#pragma once

#include <cstdint>
#include "RobotState.h"

class RobotStateMachine
{
public:
  void begin();
  RobotState getState() const;
  bool setState(RobotState newState, uint32_t now);
  bool update(bool wasPressed, uint32_t now);

private:
  RobotState state = RobotState::IDLE;
  RobotState lastAwakeState = RobotState::IDLE;
  uint32_t lastStateChange = 0;
};
