#include "RobotAudio.h"

#include <M5Unified.h>
#include <esp_heap_caps.h>

namespace
{
  constexpr uint8_t kSpeakerVolume = 255;
  constexpr uint8_t kPlaybackChannel = 0;
}

RobotAudio::~RobotAudio()
{
  releaseHardware();
  heap_caps_free(samples);
  heap_caps_free(ring);
}

bool RobotAudio::startRecording()
{
  return startCapture(false);
}

bool RobotAudio::startStreaming()
{
  return startCapture(true);
}

bool RobotAudio::startCapture(bool stream)
{
  if (clipBorrowed || streamBorrowed || currentStatus == Status::RECORDING || currentStatus == Status::PLAYING)
  {
    return false;
  }

  stop();

  streaming = stream;
  int16_t*& buffer = stream ? ring : samples;
  if (!buffer)
  {
    buffer = static_cast<int16_t*>(heap_caps_malloc(
        stream ? kStreamBytes : kMaxSampleCount * sizeof(int16_t),
        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (!buffer)
    {
      return fail(Error::BUFFER_ALLOCATION);
    }
  }

  if (!M5.Mic.begin())
  {
    return fail(Error::MICROPHONE_START);
  }

  completedFrames.store(0, std::memory_order_relaxed);
  classifiedFrames.store(0, std::memory_order_relaxed);
  releasedFrames.store(0, std::memory_order_relaxed);
  highWaterFrames.store(2, std::memory_order_relaxed);
  queueExhausted.store(false, std::memory_order_relaxed);
  processedFrames = 0;
  nextQueuedFrame = 2;
  streamPhase.store(StreamStatus::CAPTURING, std::memory_order_release);
  completedSamples.store(0, std::memory_order_relaxed);
  processedSamples = activitySamples = silentSamples = 0;
  refillFailed.store(false, std::memory_order_relaxed);
  nextQueuedSample = 2 * kFrameSamples;
  captureStarting.store(true, std::memory_order_release);
  captureStopping.store(false, std::memory_order_release);
  M5.Mic.setBufferReleaseCallback(this, bufferReleased);
  // Maintain two requests: the next one is already queued when a frame ends.
  // If startup is preempted for a whole frame, fail rather than accept a gap.
  const bool admitted = M5.Mic.record(buffer, kFrameSamples, kSampleRate, false)
      && M5.Mic.record(buffer + kFrameSamples, kFrameSamples, kSampleRate, false);
  captureStarting.store(false, std::memory_order_release);
  if (!admitted || refillFailed.load(std::memory_order_acquire))
  {
    return fail(Error::RECORD_REQUEST);
  }

  currentStatus = Status::RECORDING;
  return true;
}

void RobotAudio::bufferReleased(void* context, void* data, size_t count)
{
  auto& audio = *static_cast<RobotAudio*>(context);
  if (audio.captureStopping.load(std::memory_order_acquire))
  {
    return;
  }

  if (audio.streaming)
  {
    const uint32_t completed = audio.completedFrames.load(std::memory_order_relaxed);
    if (data != audio.ring + (completed % kStreamFrames) * kFrameSamples
        || count != kFrameSamples || audio.captureStarting.load(std::memory_order_acquire))
    {
      audio.refillFailed.store(true, std::memory_order_release);
      return;
    }
    audio.completedFrameAt.store(millis(), std::memory_order_relaxed);
    audio.completedFrames.store(completed + 1, std::memory_order_release);
    const uint32_t occupied = audio.nextQueuedFrame
        - audio.releasedFrames.load(std::memory_order_acquire);
    if (occupied >= kStreamFrames)
    {
      audio.queueExhausted.store(true, std::memory_order_release);
      return;
    }
    if (!M5.Mic.record(audio.ring + (audio.nextQueuedFrame % kStreamFrames) * kFrameSamples,
            kFrameSamples, kSampleRate, false))
    {
      if (!audio.captureStopping.load(std::memory_order_acquire))
      {
        audio.refillFailed.store(true, std::memory_order_release);
      }
      return;
    }
    ++audio.nextQueuedFrame;
    if (occupied + 1 > audio.highWaterFrames.load(std::memory_order_relaxed))
    {
      audio.highWaterFrames.store(occupied + 1, std::memory_order_relaxed);
    }
    return;
  }

  const size_t completed = audio.completedSamples.load(std::memory_order_relaxed);
  if (data != audio.samples + completed || count != kFrameSamples
      || completed + count > kMaxSampleCount
      || audio.captureStarting.load(std::memory_order_acquire))
  {
    audio.refillFailed.store(true, std::memory_order_release);
    return;
  }
  audio.completedSamples.store(completed + count, std::memory_order_release);

  // No energy calculation, allocation, end(), or controller work here.
  if (audio.nextQueuedSample < kMaxSampleCount)
  {
    if (!M5.Mic.record(audio.samples + audio.nextQueuedSample,
            kFrameSamples, kSampleRate, false))
    {
      if (!audio.captureStopping.load(std::memory_order_acquire))
      {
        audio.refillFailed.store(true, std::memory_order_release);
      }
      return;
    }
    audio.nextQueuedSample += kFrameSamples;
  }
}

void RobotAudio::processRecording()
{
  if (!M5.Mic.isRunning())
  {
    fail(Error::MICROPHONE_STOPPED);
    return;
  }
  if (refillFailed.load(std::memory_order_acquire))
  {
    fail(Error::RECORD_REQUEST);
    return;
  }

  // isRecording() can drop before the buffer-release callback. Only consume
  // its published watermark; never infer completion from queue emptiness.
  const size_t available = completedSamples.load(std::memory_order_acquire);
  while (processedSamples < available)
  {
    uint32_t energy = 0;
    for (size_t i = processedSamples; i < processedSamples + kFrameSamples; ++i)
    {
      const int32_t value = samples[i];
      energy += value < 0 ? -value : value;
    }
    processedSamples += kFrameSamples;
    if (energy >= kActivityThreshold * kFrameSamples)
    {
      activitySamples += kFrameSamples;
      silentSamples = 0;
    }
    else
    {
      silentSamples += kFrameSamples;
    }

    const bool initialSilence = activitySamples == 0
        && processedSamples >= kInitialSilenceSamples;
    const bool hangover = activitySamples != 0 && silentSamples >= kHangoverSamples;
    if (initialSilence || hangover || processedSamples == kMaxSampleCount)
    {
      // Drop any queued/finished tail after this frame. end() joins the task
      // before either the transport or speaker may borrow the retained range.
      captureStopping.store(true, std::memory_order_release);
      M5.Mic.end();
      M5.Mic.setBufferReleaseCallback(nullptr, nullptr);
      if (refillFailed.load(std::memory_order_acquire))
      {
        fail(Error::RECORD_REQUEST);
        return;
      }
      sampleCount = processedSamples;
      currentStatus = activitySamples >= kMinimumActivitySamples
          ? Status::RECORDED : Status::NO_SPEECH;
      return;
    }
  }
}

bool RobotAudio::startPlayback()
{
  if (clipBorrowed || streamBorrowed || streaming || currentStatus != Status::RECORDED)
  {
    return false;
  }

  // The capture task must release the buffer before the speaker borrows it.
  M5.Mic.end();
  auto config = M5.Speaker.config();
  config.magnification = 12; // Try 24–48; watch for distortion
  M5.Speaker.config(config);
  if (config.sample_rate == 0)
  {
    return fail(Error::SPEAKER_START);
  }

  // Queue emptiness precedes audible completion. Allow the configured DMA
  // capacity plus one mixer block to drain. Each block holds at most two
  // 16-bit samples per 32-bit word (conservative for both output modes).
  const uint64_t tailSamples =
      (static_cast<uint64_t>(config.dma_buf_count) + 1) * config.dma_buf_len * 2;
  drainDurationMs = (tailSamples * 1000 + config.sample_rate - 1) / config.sample_rate;

  M5.Speaker.setVolume(kSpeakerVolume);
  M5.Speaker.setAllChannelVolume(kSpeakerVolume);
  if (!M5.Speaker.begin())
  {
    return fail(Error::SPEAKER_START);
  }

  if (!M5.Speaker.playRaw(
          samples, sampleCount, kSampleRate, false, 1, kPlaybackChannel, false))
  {
    return fail(Error::PLAY_REQUEST);
  }

  drainingPlayback = false;
  currentStatus = Status::PLAYING;
  return true;
}

void RobotAudio::update(uint32_t now)
{
  if (currentStatus == Status::RECORDING)
  {
    if (streaming)
    {
      processStreaming();
    }
    else
    {
      processRecording();
    }
  }
  else if (currentStatus == Status::PLAYING)
  {
    if (!M5.Speaker.isRunning())
    {
      fail(Error::SPEAKER_STOPPED);
    }
    else if (M5.Speaker.isPlaying(kPlaybackChannel) == 0)
    {
      if (!drainingPlayback)
      {
        drainingPlayback = true;
        drainStartedAt = now;
      }
      else if (static_cast<uint32_t>(now - drainStartedAt) >= drainDurationMs)
      {
        // end() joins the playback task before a future recording can write.
        M5.Speaker.end();
        currentStatus = Status::COMPLETE;
      }
    }
  }
}

void RobotAudio::stop()
{
  if (clipBorrowed)
  {
    return;
  }

  releaseHardware();
  if (streamBorrowed)
  {
    currentStatus = Status::IDLE;
    return; // Socket/reader still owns ring storage; admission stays busy.
  }
  currentStatus = Status::IDLE;
  currentError = Error::NONE;
  drainingPlayback = false;
  sampleCount = 0;
}

void RobotAudio::releaseHardware()
{
  // Do not use Speaker.stop(): it only queues a marker and does not join.
  // Keep samples alive and unchanged until both end() calls return.
  streamPhase.store(StreamStatus::CANCELLED, std::memory_order_release);
  captureStopping.store(true, std::memory_order_release);
  M5.Mic.end();
  M5.Mic.setBufferReleaseCallback(nullptr, nullptr);
  M5.Speaker.end();
}

bool RobotAudio::fail(Error error)
{
  releaseHardware();
  sampleCount = 0;
  currentError = error;
  currentStatus = Status::FAILED;
  drainingPlayback = false;
  return false;
}

bool RobotAudio::borrowCompletedClip(Clip& clip)
{
  if (clipBorrowed || streaming || currentStatus != Status::RECORDED)
  {
    return false;
  }

  clipBorrowed = true;
  clip.samples = samples;
  clip.sampleCount = sampleCount;
  return true;
}

void RobotAudio::releaseCompletedClip()
{
  clipBorrowed = false;
}

uint32_t RobotAudio::completedFrameCount() const
{
  return streaming ? completedFrames.load(std::memory_order_acquire)
      : completedSamples.load(std::memory_order_acquire) / kFrameSamples;
}

bool RobotAudio::borrowStream()
{
  if (!streaming || streamBorrowed || currentStatus != Status::RECORDING)
  {
    return false;
  }
  streamBorrowed = true;
  return true;
}

void RobotAudio::releaseStream()
{
  streamBorrowed = false;
}

const int16_t* RobotAudio::streamFrame() const
{
  const uint32_t released = releasedFrames.load(std::memory_order_relaxed);
  if (released == classifiedFrames.load(std::memory_order_acquire))
  {
    return nullptr;
  }
  return ring + (released % kStreamFrames) * kFrameSamples;
}

void RobotAudio::releaseStreamFrame()
{
  releasedFrames.fetch_add(1, std::memory_order_release);
}

bool RobotAudio::streamDrained() const
{
  return releasedFrames.load(std::memory_order_relaxed)
      == classifiedFrames.load(std::memory_order_acquire);
}

void RobotAudio::processStreaming()
{
  if (!M5.Mic.isRunning())
  {
    fail(Error::MICROPHONE_STOPPED);
    return;
  }
  if (queueExhausted.load(std::memory_order_acquire))
  {
    fail(Error::QUEUE_EXHAUSTED);
    return;
  }
  if (refillFailed.load(std::memory_order_acquire))
  {
    fail(Error::RECORD_REQUEST);
    return;
  }

  const uint32_t available = completedFrames.load(std::memory_order_acquire);
  while (processedFrames != available)
  {
    const int16_t* frame = ring + (processedFrames % kStreamFrames) * kFrameSamples;
    uint32_t energy = 0;
    for (size_t i = 0; i < kFrameSamples; ++i)
    {
      const int32_t value = frame[i];
      energy += value < 0 ? -value : value;
    }
    ++processedFrames;
    // Only the initial quiet window needs the cumulative sample count.
    if (processedSamples < kInitialSilenceSamples)
    {
      processedSamples += kFrameSamples;
    }
    if (energy >= kActivityThreshold * kFrameSamples)
    {
      if (activitySamples < kMinimumActivitySamples)
      {
        activitySamples += kFrameSamples;
      }
      silentSamples = 0;
    }
    else
    {
      silentSamples += kFrameSamples;
    }
    // The classifier has finished reading before the worker can see this slot.
    classifiedFrames.store(processedFrames, std::memory_order_release);
    const bool initialSilence = activitySamples == 0 && processedSamples >= kInitialSilenceSamples;
    const bool hangover = activitySamples != 0 && silentSamples >= kHangoverSamples;
    if (initialSilence || hangover)
    {
      captureStopping.store(true, std::memory_order_release);
      M5.Mic.end(); // Join before publishing a valid, immutable final boundary.
      M5.Mic.setBufferReleaseCallback(nullptr, nullptr);
      if (queueExhausted.load(std::memory_order_acquire)
          || refillFailed.load(std::memory_order_acquire))
      {
        fail(queueExhausted.load() ? Error::QUEUE_EXHAUSTED : Error::RECORD_REQUEST);
        return;
      }
      const bool valid = activitySamples >= kMinimumActivitySamples;
      currentStatus = valid ? Status::RECORDED : Status::NO_SPEECH;
      streamPhase.store(valid ? StreamStatus::FINISHED : StreamStatus::CANCELLED,
          std::memory_order_release);
      return;
    }
  }
}
