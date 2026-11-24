#include "RobotStateMachine.h"

void RobotStateMachine::begin(uint32_t now)
{
  state = RobotState::IDLE;
  lastStateChange = now;
  activeRequestId = 0;
  requestedActions = Actions{};
  currentFailure = Failure::NONE;
}

bool RobotStateMachine::setState(RobotState newState, uint32_t now)
{
  if (state == newState)
  {
    return false;
  }

  lastStateChange = now;
  state = newState;
  return true;
}

bool RobotStateMachine::fail(Failure failure, uint32_t now)
{
  currentFailure = failure;
  // Failed idle admission also refreshes inactivity without a face transition.
  lastStateChange = now;
  return setState(RobotState::IDLE, now);
}

bool RobotStateMachine::update(bool wasPressed, uint32_t now, const Inputs& inputs)
{
  requestedActions = Actions{};
  currentFailure = Failure::NONE;

  if (state == RobotState::LISTENING)
  {
    // A finalized clip on this update takes precedence over the wall clock.
    if (inputs.audioStatus == RobotAudio::Status::RECORDED)
    {
      requestedActions.submitTranscription = true;
      return setState(RobotState::THINKING, now);
    }
    if (inputs.audioStatus == RobotAudio::Status::NO_SPEECH)
    {
      requestedActions.completion = Completion::NO_SPEECH;
      return setState(RobotState::IDLE, now);
    }
    if (static_cast<uint32_t>(now - lastStateChange) >= 11000)
    {
      return fail(Failure::CAPTURE_TIMED_OUT, now);
    }
    if (inputs.audioStatus == RobotAudio::Status::FAILED)
    {
      return fail(Failure::CAPTURE_FAILED, now);
    }
    return false;
  }

  if (state == RobotState::THINKING)
  {
    if (static_cast<uint32_t>(now - lastStateChange) >= 30000)
    {
      return fail(Failure::TRANSCRIPTION_TIMED_OUT, now);
    }
    if (inputs.resultStatus != ResultStatus::NONE &&
        inputs.resultRequestId == activeRequestId)
    {
      if (inputs.resultStatus == ResultStatus::FAILED)
      {
        return fail(Failure::TRANSCRIPTION_FAILED, now);
      }
      requestedActions.completion = inputs.resultStatus == ResultStatus::EMPTY
          ? Completion::NO_SPEECH : Completion::TRANSCRIPT;
      return setState(RobotState::IDLE, now);
    }
    // Ignore busy touches, stale IDs and playback facts. Never queue a press.
    return false;
  }

  if (state == RobotState::SLEEPING)
  {
    return wasPressed ? setState(RobotState::IDLE, now) : false;
  }

  // A late acknowledgment can release the old clip, but its coincident press
  // is still a busy press. A new distinct press is required after cleanup.
  if (wasPressed && !inputs.requestPending && inputs.resultStatus == ResultStatus::NONE)
  {
    if (!inputs.transcriptionReady)
    {
      return fail(Failure::TRANSCRIPTION_UNAVAILABLE, now);
    }
    if (++activeRequestId == 0)
    {
      ++activeRequestId;
    }
    requestedActions.startRecording = true;
    return setState(RobotState::LISTENING, now);
  }

  if (static_cast<uint32_t>(now - lastStateChange) >= 30000)
  {
    return setState(RobotState::SLEEPING, now);
  }
  return false;
}
