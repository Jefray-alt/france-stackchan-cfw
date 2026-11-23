#include "RobotController.h"

void RobotController::begin()
{
  face.begin();
  stateMachine.begin();
}

void RobotController::setState(RobotState newState)
{
  if (stateMachine.setState(newState, millis()))
  {
    notifyStateChange();
  }
}

void RobotController::update()
{
  handleTouch();
  face.update();
}

void RobotController::handleTouch()
{
  auto touch = M5.Touch.getDetail();
  uint32_t now = millis();

  if (stateMachine.update(touch.wasPressed(), now))
  {
    notifyStateChange();
  }
}

void RobotController::notifyStateChange()
{
  RobotState state = stateMachine.getState();
  face.setState(state);

  Serial.printf(
      "Robot state changed: %d\n",
      static_cast<int>(state));
}
