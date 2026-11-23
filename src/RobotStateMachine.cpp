#include "RobotStateMachine.h"

void RobotStateMachine::begin()
{
  lastStateChange = 0;
}

RobotState RobotStateMachine::getState() const
{
  return state;
}

bool RobotStateMachine::setState(RobotState newState, uint32_t now)
{
  if (state == newState)
  {
    return false;
  }

  lastStateChange = now;
  state = newState;

  if (state != RobotState::SLEEPING)
  {
    lastAwakeState = state;
  }

  return true;
}

bool RobotStateMachine::update(bool wasPressed, uint32_t now)
{
  if (!wasPressed)
  {
    if (state != RobotState::SLEEPING &&
        static_cast<uint32_t>(now - lastStateChange) >= 30000)
    {
      return setState(RobotState::SLEEPING, now);
    }

    return false;
  }

  switch (state)
  {
  case RobotState::IDLE:
    return setState(RobotState::LISTENING, now);

  case RobotState::LISTENING:
    return setState(RobotState::THINKING, now);

  case RobotState::THINKING:
    return setState(RobotState::SPEAKING, now);

  case RobotState::SPEAKING:
    return setState(RobotState::IDLE, now);

  case RobotState::SLEEPING:
    return setState(lastAwakeState, now);
  }

  return false;
}
