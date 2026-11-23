#include "RobotController.h"

void RobotController::begin()
{
  // M5.begin() may enable audio; idle owns no active microphone or speaker.
  audio.stop();
  stateMachine.begin(millis());
  face.begin();
  face.setState(stateMachine.getState());
}

void RobotController::update()
{
  auto touch = M5.Touch.getDetail();
  uint32_t now = millis();
  audio.update(now);

  if (stateMachine.update(touch.wasPressed(), now, audio.status()))
  {
    notifyStateChange();
    bool accepted = true;
    if (stateMachine.getState() == RobotState::LISTENING)
    {
      accepted = audio.startRecording();
    }
    else if (stateMachine.getState() == RobotState::THINKING)
    {
      accepted = audio.startPlayback();
    }

    if (!accepted)
    {
      stateMachine.update(false, millis(), RobotAudio::Status::FAILED);
      notifyStateChange();
    }
  }

  face.update();
}

void RobotController::notifyStateChange()
{
  RobotState state = stateMachine.getState();
  if (stateMachine.didFail())
  {
    Serial.printf("Robot audio failed or timed out (driver error %d)\n",
        static_cast<int>(audio.error()));
  }
  if (state == RobotState::IDLE)
  {
    audio.stop();
  }

  face.setState(state);

  Serial.printf(
      "Robot state changed: %d\n",
      static_cast<int>(state));
}
