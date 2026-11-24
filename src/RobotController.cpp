#include "RobotController.h"

namespace
{
  RobotTranscription::Error unavailableError(RobotTranscription::Readiness readiness)
  {
    using Readiness = RobotTranscription::Readiness;
    using Error = RobotTranscription::Error;
    switch (readiness)
    {
      case Readiness::UNCONFIGURED: return Error::NOT_CONFIGURED;
      case Readiness::INVALID_CONFIGURATION: return Error::INVALID_CONFIGURATION;
      case Readiness::WORKER_UNAVAILABLE: return Error::WORKER_UNAVAILABLE;
      case Readiness::CONNECTING: return Error::WIFI_DISCONNECTED;
      default: return Error::BUSY;
    }
  }
}

void RobotController::begin()
{
  // M5.begin() may enable audio; idle owns no active microphone or speaker.
  audio.stop();
  stateMachine.begin(millis());
  face.begin();
  face.setState(stateMachine.getState());
  transcription.begin(); // Starts association in the worker without waiting.
}

void RobotController::update()
{
  const bool wasPressed = M5.Touch.getDetail().wasPressed();
  const uint32_t now = millis();
  audio.update(now);

  // Cache an acknowledgment if it races the final capture update. A valid
  // capture must visibly enter THINKING before consuming even a fast result.
  if (transcription.poll(transcriptionResult))
  {
    hasTranscriptionResult = true;
  }
  const auto readiness = transcription.readiness();
  const auto activeError = transcription.activeError();
  RobotStateMachine::Inputs inputs;
  inputs.audioStatus = audio.status();
  inputs.completedFrames = audio.completedFrameCount();
  inputs.lastCompletedFrameAt = audio.lastCompletedFrameAt();
  inputs.transcriptionReady = readiness == RobotTranscription::Readiness::READY;
  inputs.requestPending = transcription.hasPendingRequest();
  RobotTranscription::Error error = unavailableError(readiness);
  uint16_t httpStatus = 0;
  if (hasTranscriptionResult)
  {
    error = transcriptionResult.error;
    httpStatus = transcriptionResult.httpStatus;
    inputs.resultRequestId = transcriptionResult.requestId;
    inputs.resultStatus = error == RobotTranscription::Error::CANCELLED
        ? RobotStateMachine::ResultStatus::CANCELLED
        : error != RobotTranscription::Error::NONE
        ? RobotStateMachine::ResultStatus::FAILED
        : (transcriptionResult.textLength == 0 ? RobotStateMachine::ResultStatus::EMPTY
            : RobotStateMachine::ResultStatus::TRANSCRIPT);
  }
  else if (activeError != RobotTranscription::Error::NONE)
  {
    error = activeError;
    inputs.resultRequestId = stateMachine.requestId();
    inputs.resultStatus = RobotStateMachine::ResultStatus::FAILED;
  }

  const bool changed = stateMachine.update(wasPressed, millis(), inputs);
  applyDecision(changed, error, httpStatus);
  if (stateMachine.getState() == RobotState::IDLE)
  {
    hasTranscriptionResult = false;
  }

  if (stateMachine.actions().startRecording)
  {
    if (!audio.startStreaming())
    {
      inputs = RobotStateMachine::Inputs{};
      inputs.audioStatus = RobotAudio::Status::FAILED;
      applyDecision(stateMachine.update(false, millis(), inputs));
    }
    else
    {
      const auto admission = transcription.startStream(audio, stateMachine.requestId());
      if (admission != RobotTranscription::Error::NONE)
      {
        inputs = RobotStateMachine::Inputs{};
        inputs.resultRequestId = stateMachine.requestId();
        inputs.resultStatus = RobotStateMachine::ResultStatus::FAILED;
        applyDecision(stateMachine.update(false, millis(), inputs), admission);
      }
    }
  }

  face.update();
}

void RobotController::applyDecision(bool stateChanged,
    RobotTranscription::Error error, uint16_t httpStatus)
{
  using Failure = RobotStateMachine::Failure;
  switch (stateMachine.failure())
  {
    case Failure::CAPTURE_FAILED:
      Serial.printf("Robot capture failed (driver error %d)\n", static_cast<int>(audio.error()));
      break;
    case Failure::CAPTURE_TIMED_OUT:
      Serial.println("Robot capture timed out");
      break;
    case Failure::TRANSCRIPTION_UNAVAILABLE:
    case Failure::TRANSCRIPTION_FAILED:
      Serial.printf("Transcription failed: %s", RobotTranscription::errorMessage(error));
      if (httpStatus != 0)
      {
        Serial.printf(" (HTTP %u)", static_cast<unsigned>(httpStatus));
      }
      Serial.println();
      break;
    case Failure::TRANSCRIPTION_TIMED_OUT:
      Serial.println("Transcription timed out; retry after transport cleanup");
      break;
    case Failure::NONE:
      break;
  }

  if (stateMachine.actions().completion == RobotStateMachine::Completion::TRANSCRIPT)
  {
    Serial.print("Transcript: ");
    Serial.write(reinterpret_cast<const uint8_t*>(transcriptionResult.text),
        transcriptionResult.textLength);
    Serial.println();
  }
  else if (stateMachine.actions().completion == RobotStateMachine::Completion::NO_SPEECH)
  {
    Serial.println("No speech detected");
  }

  if (stateMachine.didFail()
      || stateMachine.actions().completion != RobotStateMachine::Completion::NONE)
  {
    // Join the microphone immediately; the ring borrow remains until poll()
    // acknowledges the socket close, even after a logical result timeout.
    audio.stop();
  }

  if (stateChanged)
  {
    notifyStateChange();
  }
}

void RobotController::notifyStateChange()
{
  const RobotState state = stateMachine.getState();
  if (state == RobotState::IDLE && !transcription.hasPendingRequest())
  {
    audio.stop();
  }
  face.setState(state);
  Serial.printf("Robot state changed: %d\n", static_cast<int>(state));
}
