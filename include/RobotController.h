#pragma once

#include "RobotStateMachine.h"
#include "RobotTranscription.h"
#include "FaceRenderer.h"

class RobotController
{
public:
  void begin();
  void update();

private:
  RobotStateMachine stateMachine;
  RobotAudio audio;
  RobotTranscription transcription;
  RobotTranscription::Result transcriptionResult;
  bool hasTranscriptionResult = false;
  FaceRenderer face;

  void applyDecision(bool stateChanged,
      RobotTranscription::Error error = RobotTranscription::Error::NONE,
      uint16_t httpStatus = 0);
  void notifyStateChange();
};
