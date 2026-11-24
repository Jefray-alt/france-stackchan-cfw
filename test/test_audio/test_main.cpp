#include <unity.h>
#include <M5Unified.h>
#include <esp_heap_caps.h>
#include "RobotAudio.h"

// Exercise the same private borrow/release boundary as the real transport.
class RobotAudioTestAccess
{
public:
  static bool borrow(RobotAudio& audio, RobotAudio::Clip& clip)
  {
    return audio.borrowCompletedClip(clip);
  }
  static void release(RobotAudio& audio) { audio.releaseCompletedClip(); }
};

void setUp()
{
  M5 = FakeM5{};
  fakeHeap() = FakeHeap{};
}
void tearDown() {}

void frames(RobotAudio& audio, unsigned count, int16_t amplitude)
{
  for (unsigned i = 0; i < count; i++)
  {
    TEST_ASSERT_FALSE(M5.Mic.queued.empty());
    TEST_ASSERT_EQUAL_UINT(320, M5.Mic.queued.front().count);
    M5.Mic.release(amplitude);
    audio.update(i * 20); // Capture windows must depend on samples, not now.
  }
}

void assertStatus(RobotAudio& audio, RobotAudio::Status expected)
{
  TEST_ASSERT_EQUAL_INT(static_cast<int>(expected), static_cast<int>(audio.status()));
}

void test_capture_queues_continuously_and_keeps_speech_after_two_seconds()
{
  RobotAudio audio;
  TEST_ASSERT_TRUE(audio.startRecording());
  TEST_ASSERT_EQUAL_UINT(2, M5.Mic.queued.size());
  frames(audio, 150, 1000); // Three seconds of speech, all retained.
  assertStatus(audio, RobotAudio::Status::RECORDING);
  frames(audio, 149, 0); // 2.98 seconds of silence keeps capture open.
  assertStatus(audio, RobotAudio::Status::RECORDING);
  frames(audio, 1, 0); // The 3.00-second boundary ends capture.
  assertStatus(audio, RobotAudio::Status::RECORDED);
  TEST_ASSERT_FALSE(M5.Mic.running);
  RobotAudio::Clip clip;
  TEST_ASSERT_TRUE(RobotAudioTestAccess::borrow(audio, clip));
  TEST_ASSERT_EQUAL_UINT(96000, clip.sampleCount);
  TEST_ASSERT_EQUAL_INT16(1000, clip.samples[47999]);
  TEST_ASSERT_EQUAL_INT16(0, clip.samples[48000]);
  TEST_ASSERT_EQUAL_INT16(0, clip.samples[96000 - 1]);
  RobotAudioTestAccess::release(audio);
}

void test_short_pause_resets_hangover_and_minimum_activity_is_cumulative()
{
  RobotAudio audio;
  TEST_ASSERT_TRUE(audio.startRecording());
  frames(audio, 3, 1000);
  frames(audio, 100, 0); // A 2-second pause; the next activity resets the pause.
  frames(audio, 3, -1000); // Six active frames = 120 ms.
  frames(audio, 149, 0);
  assertStatus(audio, RobotAudio::Status::RECORDING);
  frames(audio, 1, 0);
  assertStatus(audio, RobotAudio::Status::RECORDED);
  RobotAudio::Clip clip;
  TEST_ASSERT_TRUE(RobotAudioTestAccess::borrow(audio, clip));
  TEST_ASSERT_EQUAL_UINT(256 * 320, clip.sampleCount);
  RobotAudioTestAccess::release(audio);
  TEST_ASSERT_TRUE(audio.startPlayback());
  TEST_ASSERT_EQUAL_UINT(256 * 320, M5.Speaker.playedSamples);
}

void test_continuous_activity_hits_exact_cap_and_borrow_blocks_reuse()
{
  RobotAudio audio;
  TEST_ASSERT_TRUE(audio.startRecording());
  frames(audio, 499, 1000);
  assertStatus(audio, RobotAudio::Status::RECORDING);
  frames(audio, 1, 1000);
  assertStatus(audio, RobotAudio::Status::RECORDED);
  TEST_ASSERT_TRUE(M5.Mic.queued.empty());
  RobotAudio::Clip clip;
  TEST_ASSERT_TRUE(RobotAudioTestAccess::borrow(audio, clip));
  TEST_ASSERT_EQUAL_UINT(160000, clip.sampleCount);
  TEST_ASSERT_FALSE(audio.startRecording());
  TEST_ASSERT_FALSE(audio.startPlayback());
  audio.stop();
  assertStatus(audio, RobotAudio::Status::RECORDED);
  RobotAudio::Clip other;
  TEST_ASSERT_FALSE(RobotAudioTestAccess::borrow(audio, other));
  TEST_ASSERT_EQUAL_INT16(1000, clip.samples[159999]);
  RobotAudioTestAccess::release(audio);
  TEST_ASSERT_TRUE(audio.startRecording());
  TEST_ASSERT_EQUAL_UINT(1, fakeHeap().allocations);
  TEST_ASSERT_EQUAL_UINT(320000, fakeHeap().bytes);
  TEST_ASSERT_EQUAL_UINT(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT, fakeHeap().capabilities);

  frames(audio, 149, 0); // Late onset leaves little room before the ten-second cap.
  frames(audio, 202, 100);
  frames(audio, 148, 0); // 9.98 seconds total, 2.96 seconds quiet.
  assertStatus(audio, RobotAudio::Status::RECORDING);
  frames(audio, 1, 0); // The ten-second cap wins at 2.98 seconds quiet.
  assertStatus(audio, RobotAudio::Status::RECORDED);
  TEST_ASSERT_TRUE(RobotAudioTestAccess::borrow(audio, clip));
  TEST_ASSERT_EQUAL_UINT(160000, clip.sampleCount);
  TEST_ASSERT_EQUAL_INT16(100, clip.samples[149 * 320]);
  TEST_ASSERT_EQUAL_INT16(0, clip.samples[159999]);
  RobotAudioTestAccess::release(audio);
}

void test_refill_failure_is_capture_error_and_retry_works()
{
  RobotAudio audio;
  TEST_ASSERT_TRUE(audio.startRecording());
  M5.Mic.failRecord = true;
  M5.Mic.release(1000);
  audio.update(20);
  assertStatus(audio, RobotAudio::Status::FAILED);
  TEST_ASSERT_FALSE(M5.Mic.running);
  RobotAudio::Clip clip;
  TEST_ASSERT_FALSE(RobotAudioTestAccess::borrow(audio, clip));
  M5.Mic.failRecord = false;
  TEST_ASSERT_TRUE(audio.startRecording());
  frames(audio, 500, 1000);
  assertStatus(audio, RobotAudio::Status::RECORDED);
}

void test_initial_silence_boundary_late_onset_and_quiet_noise()
{
  RobotAudio audio;
  TEST_ASSERT_TRUE(audio.startRecording());
  frames(audio, 149, 99); // Below the initial energy threshold.
  assertStatus(audio, RobotAudio::Status::RECORDING);
  frames(audio, 1, 99);
  assertStatus(audio, RobotAudio::Status::NO_SPEECH);
  RobotAudio::Clip clip;
  TEST_ASSERT_FALSE(RobotAudioTestAccess::borrow(audio, clip));
  TEST_ASSERT_FALSE(audio.startPlayback());

  TEST_ASSERT_TRUE(audio.startRecording());
  frames(audio, 149, 0);
  frames(audio, 6, 100); // Onset on the last allowed frame, exactly 120 ms.
  frames(audio, 149, 0);
  assertStatus(audio, RobotAudio::Status::RECORDING);
  frames(audio, 1, 0);
  assertStatus(audio, RobotAudio::Status::RECORDED);
  TEST_ASSERT_TRUE(RobotAudioTestAccess::borrow(audio, clip));
  TEST_ASSERT_EQUAL_UINT(305 * 320, clip.sampleCount);
  TEST_ASSERT_EQUAL_INT16(0, clip.samples[0]);
  TEST_ASSERT_EQUAL_INT16(100, clip.samples[149 * 320]);
  RobotAudioTestAccess::release(audio);

  TEST_ASSERT_TRUE(audio.startRecording());
  frames(audio, 5, -32768); // A 100 ms burst is insufficient activity.
  frames(audio, 149, 0);
  assertStatus(audio, RobotAudio::Status::RECORDING);
  frames(audio, 1, 0);
  assertStatus(audio, RobotAudio::Status::NO_SPEECH);
  TEST_ASSERT_FALSE(RobotAudioTestAccess::borrow(audio, clip));
  TEST_ASSERT_TRUE(audio.startRecording());
}

void test_slow_updates_discard_tail_and_join_before_clip_is_visible()
{
  RobotAudio audio;
  TEST_ASSERT_TRUE(audio.startRecording());
  for (unsigned frame = 0; frame < 6; ++frame) M5.Mic.release(1000);
  for (unsigned frame = 0; frame < 150; ++frame) M5.Mic.release(0);
  for (unsigned frame = 0; frame < 4; ++frame) M5.Mic.release(2000);
  bool joined = false;
  M5.Mic.onEnd = [&]() {
    RobotAudio::Clip unavailable;
    TEST_ASSERT_FALSE(RobotAudioTestAccess::borrow(audio, unavailable));
    joined = true;
  };
  audio.update(9000);
  M5.Mic.onEnd = {};
  TEST_ASSERT_TRUE(joined);
  assertStatus(audio, RobotAudio::Status::RECORDED);
  RobotAudio::Clip clip;
  TEST_ASSERT_TRUE(RobotAudioTestAccess::borrow(audio, clip));
  TEST_ASSERT_EQUAL_UINT(156 * 320, clip.sampleCount);
  TEST_ASSERT_EQUAL_INT16(0, clip.samples[clip.sampleCount - 1]);
  RobotAudioTestAccess::release(audio);
}

void test_empty_driver_queue_waits_for_delayed_final_release_callback()
{
  RobotAudio audio;
  TEST_ASSERT_TRUE(audio.startRecording());
  frames(audio, 499, 1000);
  const auto last = M5.Mic.release(1000, false);
  TEST_ASSERT_TRUE(M5.Mic.queued.empty());
  audio.update(10000);
  assertStatus(audio, RobotAudio::Status::RECORDING);
  RobotAudio::Clip clip;
  TEST_ASSERT_FALSE(RobotAudioTestAccess::borrow(audio, clip));
  M5.Mic.callback(M5.Mic.context, last.data, last.count);
  audio.update(10001);
  assertStatus(audio, RobotAudio::Status::RECORDED);
  TEST_ASSERT_TRUE(RobotAudioTestAccess::borrow(audio, clip));
  TEST_ASSERT_EQUAL_UINT(160000, clip.sampleCount);
  RobotAudioTestAccess::release(audio);
}

void test_allocation_and_driver_failures_release_ownership_for_retry()
{
  RobotAudio audio;
  fakeHeap().fail = true;
  TEST_ASSERT_FALSE(audio.startRecording());
  assertStatus(audio, RobotAudio::Status::FAILED);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(RobotAudio::Error::BUFFER_ALLOCATION),
      static_cast<int>(audio.error()));
  TEST_ASSERT_FALSE(M5.Mic.running);
  fakeHeap().fail = false;
  M5.Mic.failBegin = true;
  TEST_ASSERT_FALSE(audio.startRecording());
  TEST_ASSERT_EQUAL_INT(static_cast<int>(RobotAudio::Error::MICROPHONE_START),
      static_cast<int>(audio.error()));
  M5.Mic.failBegin = false;
  M5.Mic.failRecord = true;
  TEST_ASSERT_FALSE(audio.startRecording());
  TEST_ASSERT_FALSE(M5.Mic.running);
  M5.Mic.failRecord = false;
  TEST_ASSERT_TRUE(audio.startRecording());
  M5.Mic.running = false;
  audio.update(100);
  assertStatus(audio, RobotAudio::Status::FAILED);
  TEST_ASSERT_TRUE(audio.startRecording());
  audio.stop();
  assertStatus(audio, RobotAudio::Status::IDLE);
  TEST_ASSERT_TRUE(M5.Mic.queued.empty());
  TEST_ASSERT_NULL(M5.Mic.callback);
  TEST_ASSERT_TRUE(audio.startRecording());
  frames(audio, 6, 1000);
  frames(audio, 149, 0);
  assertStatus(audio, RobotAudio::Status::RECORDING);
  frames(audio, 1, 0);
  assertStatus(audio, RobotAudio::Status::RECORDED);
  TEST_ASSERT_EQUAL_UINT(2, fakeHeap().allocations); // Failed allocation + one reusable buffer.
}

int main()
{
  UNITY_BEGIN();
  RUN_TEST(test_capture_queues_continuously_and_keeps_speech_after_two_seconds);
  RUN_TEST(test_short_pause_resets_hangover_and_minimum_activity_is_cumulative);
  RUN_TEST(test_continuous_activity_hits_exact_cap_and_borrow_blocks_reuse);
  RUN_TEST(test_refill_failure_is_capture_error_and_retry_works);
  RUN_TEST(test_initial_silence_boundary_late_onset_and_quiet_noise);
  RUN_TEST(test_slow_updates_discard_tail_and_join_before_clip_is_visible);
  RUN_TEST(test_empty_driver_queue_waits_for_delayed_final_release_callback);
  RUN_TEST(test_allocation_and_driver_failures_release_ownership_for_retry);
  return UNITY_END();
}
