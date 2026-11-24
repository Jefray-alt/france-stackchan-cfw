#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>

class RobotTranscription;

// One long-lived instance exclusively owns M5.Mic and M5.Speaker.
class RobotAudio
{
public:
  enum class Status
  {
    IDLE,
    RECORDING,
    RECORDED,
    NO_SPEECH,
    PLAYING,
    COMPLETE,
    FAILED
  };

  enum class Error
  {
    NONE,
    BUFFER_ALLOCATION,
    MICROPHONE_START,
    RECORD_REQUEST,
    MICROPHONE_STOPPED,
    SPEAKER_START,
    PLAY_REQUEST,
    SPEAKER_STOPPED
  };

  static constexpr uint32_t kSampleRate = 16000;
  static constexpr size_t kMaxSampleCount = 160000;
  static constexpr size_t kFrameSamples = 320;            // 20 ms at 16 kHz.
  static constexpr size_t kInitialSilenceSamples = 48000; // 3 seconds.
  static constexpr size_t kHangoverSamples = 80000;       // 5000 ms.
  static constexpr size_t kMinimumActivitySamples = 1920; // 120 ms.
  // Mean absolute PCM amplitude. Initial setting; tune on CoreS3.
  static constexpr uint32_t kActivityThreshold = 100;

  struct Clip
  {
    const int16_t *samples = nullptr;
    size_t sampleCount = 0;
  };

  RobotAudio() = default;
  ~RobotAudio();
  RobotAudio(const RobotAudio &) = delete;
  RobotAudio &operator=(const RobotAudio &) = delete;
  RobotAudio(RobotAudio &&) = delete;
  RobotAudio &operator=(RobotAudio &&) = delete;

  // A busy request is rejected without changing the current operation.
  bool startRecording();
  // Only a fully recorded, unused clip can be played.
  bool startPlayback();
  void update(uint32_t now);
  // Cancels either operation, invalidates the clip, and returns to IDLE.
  // While transcription borrows a completed clip, stop() leaves it intact.
  // M5Unified end() synchronously releases driver ownership and may wait.
  void stop();
  Status status() const { return currentStatus; }
  Error error() const { return currentError; }

private:
  friend class RobotTranscription;

  // Main-task only. Only the transport may release after worker acknowledgment.
  bool borrowCompletedClip(Clip &clip);
  void releaseCompletedClip();

  static_assert(kMaxSampleCount * sizeof(int16_t) == 320000,
                "The reusable PSRAM clip holds ten seconds of 16-bit mono PCM");
  static_assert(kMaxSampleCount % kFrameSamples == 0, "The cap ends on a frame");

  int16_t *samples = nullptr;
  size_t sampleCount = 0;
  size_t processedSamples = 0;
  size_t activitySamples = 0;
  size_t silentSamples = 0;
  // Capture-task producer, main-task consumer. Samples below this release
  // watermark are immutable until end() joins the capture task.
  std::atomic<size_t> completedSamples{0};
  std::atomic<bool> captureStopping{true};
  std::atomic<bool> captureStarting{false};
  std::atomic<bool> refillFailed{false};
  size_t nextQueuedSample = 0; // Capture-task only after initial admission.
  Status currentStatus = Status::IDLE;
  Error currentError = Error::NONE;
  bool clipBorrowed = false;
  bool drainingPlayback = false;
  uint32_t drainStartedAt = 0;
  uint32_t drainDurationMs = 0;

  static void bufferReleased(void *context, void *data, size_t count);
  void processRecording();
  void releaseHardware();
  bool fail(Error error);
};
