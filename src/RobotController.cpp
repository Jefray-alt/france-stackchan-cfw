#include "RobotController.h"

void RobotController::begin()
{
  face.begin();
  lastFaceChangeState = 0;
}

void RobotController::setState(RobotState newState)
{
  if (state == newState)
  {
    return;
  }

  lastFaceChangeState = millis();
  state = newState;

  if (state != RobotState::SLEEPING)
  {
    lastState = newState;
  }

  face.setState(state);

  Serial.printf(
      "Robot state changed: %d\n",
      static_cast<int>(state));
}

void RobotController::update()
{
  handleTouch();
  face.update();
}

void RobotController::handleTouch()
{
  auto touch = M5.Touch.getDetail();

  auto isSleeping = millis() - lastFaceChangeState;

  if (!touch.wasPressed())
  {
    if (isSleeping >= 30000 && state != RobotState::SLEEPING)
    {
      setState(RobotState::SLEEPING);
      return;
    }
    return;
  }

  lastFaceChangeState = 0;

  switch (state)
  {
  case RobotState::IDLE:
    setState(RobotState::LISTENING);
    break;

  case RobotState::LISTENING:
    setState(RobotState::THINKING);
    break;

  case RobotState::THINKING:
    setState(RobotState::SPEAKING);
    break;

  case RobotState::SPEAKING:
    setState(RobotState::IDLE);
    break;

  case RobotState::SLEEPING:
    setState(lastState);
    break;
  }
}