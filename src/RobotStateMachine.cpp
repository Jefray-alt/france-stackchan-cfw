#include "RobotStateMachine.h"

void RobotStateMachine::begin(uint32_t now)
{
  state = RobotState::IDLE;
  lastStateChange = now;
  operationFailed = false;
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

  // THINKING queues playback; entering SPEAKING must not extend its watchdog.
  if (newState != RobotState::SPEAKING)
  {
    lastStateChange = now;
  }

  state = newState;
  return true;
}

bool RobotStateMachine::update(
    bool wasPressed, uint32_t now, RobotAudio::Status audioStatus)
{
  operationFailed = false;
  if (state == RobotState::LISTENING || state == RobotState::THINKING ||
      state == RobotState::SPEAKING)
  {
    operationFailed = audioStatus == RobotAudio::Status::FAILED ||
        static_cast<uint32_t>(now - lastStateChange) >= 5000;
    if (operationFailed)
    {
      return setState(RobotState::IDLE, now);
    }

    if (state == RobotState::LISTENING && audioStatus == RobotAudio::Status::RECORDED)
    {
      return setState(RobotState::THINKING, now);
    }
    if (state == RobotState::THINKING && audioStatus == RobotAudio::Status::PLAYING)
    {
      return setState(RobotState::SPEAKING, now);
    }
    if (state == RobotState::SPEAKING && audioStatus == RobotAudio::Status::COMPLETE)
    {
      return setState(RobotState::IDLE, now);
    }

    // Ignore busy presses, including a press coincident with completion.
    return false;
  }

  if (wasPressed)
  {
    return setState(state == RobotState::SLEEPING
        ? RobotState::IDLE : RobotState::LISTENING, now);
  }

  if (state == RobotState::IDLE &&
      static_cast<uint32_t>(now - lastStateChange) >= 30000)
  {
    return setState(RobotState::SLEEPING, now);
  }

  return false;
}
