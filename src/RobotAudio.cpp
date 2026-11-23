#include "RobotAudio.h"

#include <M5Unified.h>

namespace
{
  constexpr uint8_t kSpeakerVolume = 255;
  constexpr uint8_t kSpeakermMagnification = 32;
  constexpr uint8_t kPlaybackChannel = 0;
}

RobotAudio::~RobotAudio()
{
  releaseHardware();
}

bool RobotAudio::startRecording()
{
  if (currentStatus == Status::RECORDING || currentStatus == Status::PLAYING)
  {
    return false;
  }

  stop();

  auto config = M5.Mic.config();
  M5.Mic.config(config);

  if (!M5.Mic.begin())
  {
    return fail(Error::MICROPHONE_START);
  }

  if (!M5.Mic.record(samples, kSampleCount, kSampleRate, false))
  {
    return fail(Error::RECORD_REQUEST);
  }

  currentStatus = Status::RECORDING;
  return true;
}

bool RobotAudio::startPlayback()
{
  if (currentStatus != Status::RECORDED)
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
          samples, kSampleCount, kSampleRate, false, 1, kPlaybackChannel, false))
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
    if (!M5.Mic.isRunning())
    {
      fail(Error::MICROPHONE_STOPPED);
    }
    else if (M5.Mic.isRecording() == 0)
    {
      M5.Mic.end();
      currentStatus = Status::RECORDED;
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
  releaseHardware();
  currentStatus = Status::IDLE;
  currentError = Error::NONE;
  drainingPlayback = false;
}

void RobotAudio::releaseHardware()
{
  // Do not use Speaker.stop(): it only queues a marker and does not join.
  // Keep samples alive and unchanged until both end() calls return.
  M5.Mic.end();
  M5.Speaker.end();
}

bool RobotAudio::fail(Error error)
{
  releaseHardware();
  currentError = error;
  currentStatus = Status::FAILED;
  drainingPlayback = false;
  return false;
}
