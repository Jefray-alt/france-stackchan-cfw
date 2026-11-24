#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>

// Native-only driver boundary: model the two request slots and delayed release.
struct FakeMic
{
  struct Config {};
  struct Request { int16_t* data; size_t count; };
  std::deque<Request> queued;
  void* context = nullptr;
  void (*callback)(void*, void*, size_t) = nullptr;
  bool running = false;
  bool failBegin = false;
  bool failRecord = false;
  unsigned endCalls = 0;
  std::function<void()> onEnd;

  Config config() const { return {}; }
  void config(Config) {}
  bool begin() { return running = !failBegin; }
  bool isRunning() const { return running; }
  size_t isRecording() const { return queued.size(); }
  void setBufferReleaseCallback(void* args, void (*func)(void*, void*, size_t))
  {
    context = args;
    callback = func;
  }
  bool record(int16_t* data, size_t count, uint32_t, bool)
  {
    if (failRecord || !running || queued.size() >= 2) return false;
    queued.push_back({data, count});
    return true;
  }
  Request release(int16_t amplitude, bool notify = true)
  {
    const Request request = queued.front();
    queued.pop_front();
    std::fill(request.data, request.data + request.count, amplitude);
    if (notify && callback) callback(context, request.data, request.count);
    return request;
  }
  void end()
  {
    endCalls++;
    if (onEnd) onEnd();
    running = false;
    queued.clear();
  }
};

struct FakeSpeaker
{
  struct Config
  {
    uint32_t sample_rate = 16000;
    size_t dma_buf_count = 8;
    size_t dma_buf_len = 128;
    uint8_t magnification = 16;
  };
  bool running = false;
  bool playing = false;
  size_t playedSamples = 0;
  Config config() const { return {}; }
  void config(Config) {}
  bool begin() { return running = true; }
  bool isRunning() const { return running; }
  bool isPlaying(uint8_t) const { return playing; }
  void setVolume(uint8_t) {}
  void setAllChannelVolume(uint8_t) {}
  bool playRaw(const int16_t*, size_t count, uint32_t, bool, unsigned, uint8_t, bool)
  {
    playedSamples = count;
    return playing = true;
  }
  void end() { running = playing = false; }
};

struct FakeM5
{
  FakeMic Mic;
  FakeSpeaker Speaker;
};

inline FakeM5& fakeM5()
{
  static FakeM5 hardware;
  return hardware;
}
#define M5 fakeM5()
