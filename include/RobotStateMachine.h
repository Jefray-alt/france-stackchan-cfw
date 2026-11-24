#pragma once

#include <cstdint>
#include "RobotAudio.h"
#include "RobotState.h"

class RobotStateMachine
{
public:
  // Main-task facts only; no Wi-Fi, socket, queue or driver dependencies.
  enum class ResultStatus
  {
    NONE,
    TRANSCRIPT,
    EMPTY,
    CANCELLED,
    FAILED
  };

  enum class Completion
  {
    NONE,
    TRANSCRIPT,
    NO_SPEECH
  };

  enum class Failure
  {
    NONE,
    TRANSCRIPTION_UNAVAILABLE,
    CAPTURE_FAILED,
    CAPTURE_TIMED_OUT,
    TRANSCRIPTION_FAILED,
    TRANSCRIPTION_TIMED_OUT
  };

  struct Inputs
  {
    RobotAudio::Status audioStatus = RobotAudio::Status::IDLE;
    uint32_t completedFrames = 0;
    uint32_t lastCompletedFrameAt = 0;
    bool transcriptionReady = true;
    bool requestPending = false;
    ResultStatus resultStatus = ResultStatus::NONE;
    uint32_t resultRequestId = 0;
  };

  struct Actions
  {
    bool startRecording = false;
    bool captureFinished = false;
    Completion completion = Completion::NONE;
  };

  void begin(uint32_t now = 0);
  RobotState getState() const { return state; }
  bool update(bool wasPressed, uint32_t now, const Inputs& inputs);
  const Actions& actions() const { return requestedActions; }
  Failure failure() const { return currentFailure; }
  bool didFail() const { return currentFailure != Failure::NONE; }
  uint32_t requestId() const { return activeRequestId; }

private:
  RobotState state = RobotState::IDLE;
  uint32_t lastStateChange = 0;
  uint32_t activeRequestId = 0;
  uint32_t lastCaptureProgressAt = 0;
  uint32_t lastCompletedFrames = 0;
  Actions requestedActions;
  Failure currentFailure = Failure::NONE;

  bool setState(RobotState newState, uint32_t now);
  bool fail(Failure failure, uint32_t now);
};
