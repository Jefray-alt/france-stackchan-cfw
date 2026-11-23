#pragma once

#include <cstddef>
#include <cstdint>

// One long-lived instance exclusively owns M5.Mic and M5.Speaker.
class RobotAudio
{
public:
  enum class Status
  {
    IDLE,
    RECORDING,
    RECORDED,
    PLAYING,
    COMPLETE,
    FAILED
  };

  enum class Error
  {
    NONE,
    MICROPHONE_START,
    RECORD_REQUEST,
    MICROPHONE_STOPPED,
    SPEAKER_START,
    PLAY_REQUEST,
    SPEAKER_STOPPED
  };

  static constexpr uint32_t kSampleRate = 16000;
  static constexpr size_t kSampleCount = 32000;

  RobotAudio() = default;
  ~RobotAudio();
  RobotAudio(const RobotAudio&) = delete;
  RobotAudio& operator=(const RobotAudio&) = delete;
  RobotAudio(RobotAudio&&) = delete;
  RobotAudio& operator=(RobotAudio&&) = delete;

  // A busy request is rejected without changing the current operation.
  bool startRecording();
  // Only a fully recorded, unused clip can be played.
  bool startPlayback();
  void update(uint32_t now);
  // Cancels either operation, invalidates the clip, and returns to IDLE.
  // M5Unified end() synchronously releases driver ownership and may wait.
  void stop();
  Status status() const { return currentStatus; }
  Error error() const { return currentError; }

private:
  static_assert(kSampleCount * sizeof(int16_t) == 64000,
      "The reusable clip must hold two seconds of 16-bit mono PCM");

  int16_t samples[kSampleCount];
  Status currentStatus = Status::IDLE;
  Error currentError = Error::NONE;
  bool drainingPlayback = false;
  uint32_t drainStartedAt = 0;
  uint32_t drainDurationMs = 0;

  void releaseHardware();
  bool fail(Error error);
};
