#pragma once

#include <cstddef>
#include <cstdint>
#include "TranscriptionProtocol.h"

class RobotAudio;

// One firmware-lifetime instance. All public methods run on the main task;
// audio and this object must outlive its persistent worker, including timeouts.
// begin() is explicit: merely constructing this transport starts no networking.
class RobotTranscription
{
public:
  enum class Error
  {
    NONE,
    CANCELLED,
    NOT_CONFIGURED,
    INVALID_CONFIGURATION,
    WORKER_UNAVAILABLE,
    WIFI_DISCONNECTED,
    BUSY,
    INVALID_CLIP,
    CONNECT_FAILED,
    IO_FAILED,
    TIMED_OUT,
    HTTP_REJECTED,
    MALFORMED_RESPONSE,
    RESPONSE_TOO_LARGE,
    INVALID_UTF8
  };

  enum class Readiness
  {
    UNCONFIGURED,
    INVALID_CONFIGURATION,
    WORKER_UNAVAILABLE,
    CONNECTING,
    READY,
    BUSY
  };

  struct Result
  {
    uint32_t requestId = 0;
    Error error = Error::NONE;
    uint16_t httpStatus = 0;
    size_t textLength = 0;
    // Use textLength when printing; valid UTF-8 can include embedded NUL.
    char text[TranscriptionProtocol::kMaxTranscriptBytes + 1] = {};
  };

  RobotTranscription() = default;
  RobotTranscription(const RobotTranscription&) = delete;
  RobotTranscription& operator=(const RobotTranscription&) = delete;
  RobotTranscription(RobotTranscription&&) = delete;
  RobotTranscription& operator=(RobotTranscription&&) = delete;

  Error begin();
  Readiness readiness() const;
  Error startStream(RobotAudio& audio, uint32_t requestId);
  // Errors become visible before a potentially delayed socket-close acknowledgment.
  Error activeError() const;
  // Release is acknowledged only here, after socket close and the last read of
  // the ring. Poll even after a logical interaction timeout; discard stale IDs.
  // No retry or new recording is allowed while hasPendingRequest() is true.
  bool poll(Result& result);
  bool hasPendingRequest() const { return borrowedAudio != nullptr; }
  static const char* errorMessage(Error error);

private:
  struct Runtime;
  Runtime* runtime = nullptr;
  RobotAudio* borrowedAudio = nullptr;
  Error initializationError = Error::NOT_CONFIGURED;
  bool began = false;

  static void workerEntry(void* context);
};
