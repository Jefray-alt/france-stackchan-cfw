#include <unity.h>
#include <M5Unified.h>
#include <esp_heap_caps.h>
#include <algorithm>
#include <cstring>
#include <string>
#include <vector>
#include "TranscriptionUpload.h"
#include "RobotStateMachine.h"

class RobotAudioTestAccess
{
public:
  static bool borrow(RobotAudio& audio) { return audio.borrowStream(); }
  static void acknowledge(RobotAudio& audio) { audio.releaseStream(); }
  static const int16_t* frame(RobotAudio& audio) { return audio.streamFrame(); }
};

using Error = RobotTranscription::Error;
using AudioStatus = RobotAudio::Status;

struct SocketFixture : TranscriptionUpload::Io
{
  std::string output;
  std::string incoming;
  size_t budget = SIZE_MAX;
  size_t maxWrite = SIZE_MAX;
  size_t maxRead = SIZE_MAX;
  bool disconnected = false;
  bool writeFailed = false;
  unsigned writes = 0;

  int write(const uint8_t* bytes, size_t count) override
  {
    ++writes;
    if (writeFailed) return -2;
    const size_t accepted = std::min(count, std::min(budget, maxWrite));
    if (!accepted) return -1;
    output.append(reinterpret_cast<const char*>(bytes), accepted);
    budget -= accepted;
    return accepted;
  }
  int read(uint8_t* bytes, size_t count) override
  {
    if (incoming.empty()) return disconnected ? 0 : -1;
    const size_t received = std::min(count, std::min(incoming.size(), maxRead));
    std::memcpy(bytes, incoming.data(), received);
    incoming.erase(0, received);
    return received;
  }
  void respond()
  {
    incoming = "HTTP/1.1 200 OK\r\nContent-Type: text/plain; charset=utf-8\r\nContent-Length: 5\r\n\r\nhello";
  }
};

struct Session
{
  RobotAudio audio;
  TranscriptionProtocol::Endpoint endpoint;
  char text[TranscriptionProtocol::kMaxTranscriptBytes + 1] = {};
  TranscriptionProtocol::ResponseParser parser{text};
  TranscriptionUpload upload{audio, endpoint, parser, fakeMillis()};
  SocketFixture socket;

  void start()
  {
    TEST_ASSERT_TRUE(audio.startStreaming());
    TEST_ASSERT_TRUE(RobotAudioTestAccess::borrow(audio));
    upload.update(fakeMillis(), socket); // Request headers, no PCM yet.
    TEST_ASSERT_FALSE(upload.done());
  }
  void frame(int16_t value, bool drain = true)
  {
    TEST_ASSERT_FALSE(M5.Mic.queued.empty());
    fakeMillis() += 20;
    M5.Mic.release(value);
    audio.update(fakeMillis());
    if (drain) upload.update(fakeMillis(), socket);
  }
  void frames(unsigned count, int16_t value, bool drain = true)
  {
    for (unsigned i = 0; i < count; ++i) frame(value, drain);
  }
  void acknowledge()
  {
    // Model main polling only after the socket owner has closed and stopped reading.
    RobotAudioTestAccess::acknowledge(audio);
    audio.stop();
  }
};

void setUp()
{
  M5 = FakeM5{};
  fakeHeap() = FakeHeap{};
  fakeMillis() = 0;
}
void tearDown() {}

void assertError(Error expected, const TranscriptionUpload& upload)
{
  TEST_ASSERT_TRUE(upload.done());
  TEST_ASSERT_EQUAL_INT(static_cast<int>(expected), static_cast<int>(upload.error()));
}

std::string decoded(const std::string& http, bool& finalized)
{
  const size_t headerEnd = http.find("\r\n\r\n");
  TEST_ASSERT_NOT_EQUAL(SIZE_MAX, headerEnd);
  TEST_ASSERT_NOT_EQUAL(SIZE_MAX, http.find("Transfer-Encoding: chunked\r\n"));
  TEST_ASSERT_EQUAL(SIZE_MAX, http.find("Content-Length:"));
  size_t cursor = headerEnd + 4;
  std::string pcm;
  finalized = false;
  while (cursor < http.size())
  {
    const size_t end = http.find("\r\n", cursor);
    TEST_ASSERT_NOT_EQUAL(SIZE_MAX, end);
    const size_t bytes = std::stoul(http.substr(cursor, end - cursor), nullptr, 16);
    cursor = end + 2;
    TEST_ASSERT_TRUE(cursor + bytes + 2 <= http.size());
    pcm.append(http, cursor, bytes);
    cursor += bytes;
    TEST_ASSERT_EQUAL_STRING("\r\n", http.substr(cursor, 2).c_str());
    cursor += 2;
    if (!bytes)
    {
      finalized = true;
      TEST_ASSERT_EQUAL(http.size(), cursor);
    }
  }
  return pcm;
}

void test_seventy_seconds_wraps_ordered_pcm_and_result_clock_starts_after_capture()
{
  Session session;
  session.start();
  RobotStateMachine machine;
  machine.begin();
  TEST_ASSERT_TRUE(machine.update(true, 0, {}));
  std::string expected;
  for (unsigned i = 0; i < 3500; ++i)
  {
    const int16_t amplitude = 100 + i % 20000;
    for (size_t n = 0; n < RobotAudio::kFrameSamples; ++n)
    {
      expected.push_back(amplitude & 0xff);
      expected.push_back((amplitude >> 8) & 0xff);
    }
    session.frame(amplitude);
    RobotStateMachine::Inputs inputs;
    inputs.audioStatus = session.audio.status();
    inputs.completedFrames = session.audio.completedFrameCount();
    inputs.lastCompletedFrameAt = session.audio.lastCompletedFrameAt();
    inputs.requestPending = true;
    TEST_ASSERT_FALSE(machine.update(false, fakeMillis(), inputs));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(RobotState::LISTENING), static_cast<int>(machine.getState()));
    TEST_ASSERT_FALSE(session.upload.done());
  }
  TEST_ASSERT_EQUAL_UINT(40960, fakeHeap().bytes);
  TEST_ASSERT_EQUAL_UINT(1, fakeHeap().allocations);
  TEST_ASSERT_TRUE(session.audio.streamHighWaterFrames() <= RobotAudio::kStreamFrames);
  TEST_ASSERT_TRUE(session.socket.output.size() > 1920000); // Firmware imposes no backend length cap.
  session.frames(250, 0);
  expected.append(250 * RobotAudio::kFrameSamples * sizeof(int16_t), '\0');
  TEST_ASSERT_EQUAL_INT(static_cast<int>(AudioStatus::RECORDED), static_cast<int>(session.audio.status()));
  bool finalized;
  const std::string pcm = decoded(session.socket.output, finalized);
  TEST_ASSERT_TRUE(finalized);
  TEST_ASSERT_EQUAL(expected.size(), pcm.size());
  TEST_ASSERT_EQUAL_MEMORY(expected.data(), pcm.data(), pcm.size());
  RobotStateMachine::Inputs ended;
  ended.audioStatus = session.audio.status();
  TEST_ASSERT_TRUE(machine.update(false, fakeMillis(), ended));
  TEST_ASSERT_EQUAL_INT(static_cast<int>(RobotState::THINKING), static_cast<int>(machine.getState()));
  TEST_ASSERT_FALSE(machine.update(false, fakeMillis() + 29999, {}));
  TEST_ASSERT_TRUE(machine.update(false, fakeMillis() + 30000, {}));
  TEST_ASSERT_EQUAL_INT(static_cast<int>(RobotStateMachine::Failure::TRANSCRIPTION_TIMED_OUT),
      static_cast<int>(machine.failure()));
  session.socket.respond();
  session.upload.update(fakeMillis() + 1, session.socket);
  assertError(Error::NONE, session.upload);
  TEST_ASSERT_EQUAL_STRING("hello", session.text);
  session.acknowledge();
}

void test_driver_release_precedes_classification_and_partial_pcm_retains_slot()
{
  Session session;
  session.start();
  const auto delayed = M5.Mic.release(1234, false);
  session.audio.update(20);
  session.upload.update(20, session.socket);
  TEST_ASSERT_NULL(RobotAudioTestAccess::frame(session.audio));
  fakeMillis() = 20;
  M5.Mic.callback(M5.Mic.context, delayed.data, delayed.count);
  session.audio.update(20);
  const int16_t* held = RobotAudioTestAccess::frame(session.audio);
  TEST_ASSERT_EQUAL_PTR(delayed.data, held);
  session.socket.budget = 5 + 100; // Prefix and only the first 100 PCM bytes.
  session.upload.update(20, session.socket);
  TEST_ASSERT_EQUAL_PTR(held, RobotAudioTestAccess::frame(session.audio));
  session.frames(61, 2000, false); // All 64 slots owned: two driver, 62 classified.
  TEST_ASSERT_EQUAL_INT16(1234, held[0]);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(AudioStatus::RECORDING), static_cast<int>(session.audio.status()));
  session.socket.budget = 540; // Accept the rest of PCM, but block its suffix.
  session.upload.update(fakeMillis(), session.socket);
  TEST_ASSERT_NOT_EQUAL(held, RobotAudioTestAccess::frame(session.audio));
  session.frame(3000, false);
  TEST_ASSERT_EQUAL_PTR(held, M5.Mic.queued.back().data); // Slot reusable before suffix or response.
  TEST_ASSERT_FALSE(session.upload.done());
  session.audio.stop();
  TEST_ASSERT_FALSE(M5.Mic.running);
  TEST_ASSERT_FALSE(session.audio.startStreaming()); // Socket owner still borrows ring.
  session.upload.update(fakeMillis(), session.socket);
  assertError(Error::CANCELLED, session.upload);
  session.acknowledge();
  TEST_ASSERT_TRUE(session.audio.startStreaming());
  TEST_ASSERT_EQUAL_UINT(1, fakeHeap().allocations);
}

void test_exact_tail_excludes_unclassified_driver_tail_and_final_waits_for_join()
{
  Session session;
  session.start();
  session.frames(6, 100);
  session.frames(249, 0);
  M5.Mic.release(0);
  for (unsigned i = 0; i < 4; ++i) M5.Mic.release(2000);
  M5.Mic.onEnd = [&]()
  {
    session.upload.update(fakeMillis(), session.socket);
    TEST_ASSERT_EQUAL(SIZE_MAX, session.socket.output.find("0\r\n\r\n"));
  };
  session.audio.update(fakeMillis());
  M5.Mic.onEnd = {};
  session.upload.update(fakeMillis(), session.socket);
  bool finalized;
  const auto pcm = decoded(session.socket.output, finalized);
  TEST_ASSERT_TRUE(finalized);
  TEST_ASSERT_EQUAL_UINT(256 * 640, pcm.size());
  for (size_t i = 6 * 640; i < pcm.size(); ++i) TEST_ASSERT_EQUAL_UINT8(0, pcm[i]);
  session.acknowledge();
}

void test_quiet_noise_and_minimum_cumulative_activity_cancel_or_finish_once()
{
  for (unsigned cycle = 0; cycle < 10; ++cycle)
  {
    Session session;
    session.start();
    if (cycle % 2) session.frames(5, -32768);
    session.frames(cycle % 2 ? 250 : 150, 0);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(AudioStatus::NO_SPEECH), static_cast<int>(session.audio.status()));
    assertError(Error::CANCELLED, session.upload);
    bool finalized;
    decoded(session.socket.output, finalized);
    TEST_ASSERT_FALSE(finalized);
    TEST_ASSERT_FALSE(session.audio.startStreaming());
    session.acknowledge();
  }
  Session session;
  session.start();
  session.frames(149, 99); // Delayed onset just before initial quiet deadline.
  session.frames(3, 100);
  session.frames(240, 0);
  session.frames(3, -100); // Cumulative 120 ms, reset hangover.
  session.frames(249, 0);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(AudioStatus::RECORDING), static_cast<int>(session.audio.status()));
  session.frames(1, 0);
  bool finalized;
  const auto pcm = decoded(session.socket.output, finalized);
  TEST_ASSERT_TRUE(finalized);
  TEST_ASSERT_EQUAL_UINT((149 + 3 + 240 + 3 + 250) * 640, pcm.size());
  session.acknowledge();
}

void test_exhaustion_and_capture_failure_cancel_without_final_chunk()
{
  for (unsigned failure = 0; failure < 3; ++failure)
  {
    Session session;
    session.start();
    if (failure == 0)
    {
      session.frames(63, 1000, false);
      TEST_ASSERT_EQUAL_INT(static_cast<int>(RobotAudio::Error::QUEUE_EXHAUSTED), static_cast<int>(session.audio.error()));
    }
    else
    {
      if (failure == 1) M5.Mic.failRecord = true;
      else M5.Mic.running = false;
      if (failure == 1) M5.Mic.release(1000);
      session.audio.update(20);
    }
    TEST_ASSERT_EQUAL_INT(static_cast<int>(AudioStatus::FAILED), static_cast<int>(session.audio.status()));
    session.upload.update(20, session.socket);
    assertError(Error::CANCELLED, session.upload);
    TEST_ASSERT_EQUAL(SIZE_MAX, session.socket.output.find("0\r\n\r\n"));
    TEST_ASSERT_FALSE(session.audio.startStreaming());
    session.acknowledge();
    M5.Mic.failRecord = false;
  }
}

void test_rejection_disconnect_and_io_error_are_observed_during_capture()
{
  for (unsigned failure = 0; failure < 3; ++failure)
  {
    Session session;
    session.start();
    session.frame(1000);
    session.socket.budget = 0; // Rejection must be observed even while write stalls.
    if (failure == 0)
    {
      session.socket.incoming = "HTTP/1.1 413 Payload Too Large\r\n";
      session.socket.maxRead = 1; // Fragmented live rejection.
    }
    else if (failure == 1) session.socket.disconnected = true;
    else session.socket.writeFailed = true;
    session.frame(1000);
    for (unsigned i = 0; i < 80 && !session.upload.done(); ++i)
      session.upload.update(fakeMillis() + i, session.socket);
    assertError(failure == 0 ? Error::HTTP_REJECTED : Error::IO_FAILED, session.upload);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(AudioStatus::RECORDING), static_cast<int>(session.audio.status()));
    session.audio.stop(); // Controller responds before socket cleanup acknowledgment.
    TEST_ASSERT_FALSE(M5.Mic.running);
    TEST_ASSERT_FALSE(session.audio.startStreaming());
    TEST_ASSERT_EQUAL(SIZE_MAX, session.socket.output.find("0\r\n\r\n"));
    session.acknowledge();
    TEST_ASSERT_TRUE(session.audio.startStreaming());
  }
}

void test_write_and_response_idle_deadlines_reset_on_progress_and_roll_over()
{
  for (uint32_t start : {100u, 0xFFFFF000u})
  {
    Session session;
    session.start();
    session.socket.budget = 1;
    fakeMillis() = start;
    session.frame(1000);
    const uint32_t lastWrite = fakeMillis();
    session.upload.update(lastWrite + 4999, session.socket);
    TEST_ASSERT_FALSE(session.upload.done());
    session.socket.budget = 1;
    session.upload.update(lastWrite + 4999, session.socket); // One byte resets stall clock.
    session.upload.update(lastWrite + 9998, session.socket);
    TEST_ASSERT_FALSE(session.upload.done());
    session.upload.update(lastWrite + 9999, session.socket);
    assertError(Error::TIMED_OUT, session.upload);
    session.audio.stop();
    session.acknowledge();
  }
  for (unsigned partialResponse = 0; partialResponse < 2; ++partialResponse)
  {
    Session session;
    session.start();
    session.frames(6, 1000);
    session.frames(250, 0);
    const uint32_t finalizedAt = fakeMillis();
    session.upload.update(finalizedAt + 24999, session.socket);
    TEST_ASSERT_FALSE(session.upload.done());
    if (partialResponse)
    {
      session.socket.incoming = "HTTP/1.1 ";
      session.upload.update(finalizedAt + 24999, session.socket);
      session.upload.update(finalizedAt + 29998, session.socket);
      TEST_ASSERT_FALSE(session.upload.done());
      session.upload.update(finalizedAt + 29999, session.socket);
    }
    else session.upload.update(finalizedAt + 25000, session.socket);
    assertError(Error::TIMED_OUT, session.upload);
    session.acknowledge();
  }
}

void test_reusable_ring_allocation_and_start_failure_can_retry_without_clip_allocation()
{
  RobotAudio audio;
  fakeHeap().fail = true;
  TEST_ASSERT_FALSE(audio.startStreaming());
  fakeHeap().fail = false;
  M5.Mic.failBegin = true;
  TEST_ASSERT_FALSE(audio.startStreaming());
  M5.Mic.failBegin = false;
  for (unsigned i = 0; i < 10; ++i)
  {
    TEST_ASSERT_TRUE(audio.startStreaming());
    TEST_ASSERT_EQUAL_UINT(40960, fakeHeap().bytes);
    audio.stop();
  }
  TEST_ASSERT_EQUAL_UINT(2, fakeHeap().allocations); // Failed attempt, then one reusable ring.
}

void test_premature_success_and_logical_abort_cannot_finalize_a_capture()
{
  for (bool premature : {false, true})
  {
    Session session;
    session.start();
    session.frame(1000);
    if (premature) session.socket.respond();
    else session.audio.stop(); // Main watchdog cancellation, before valid capture end.
    session.upload.update(fakeMillis(), session.socket);
    assertError(premature ? Error::MALFORMED_RESPONSE : Error::CANCELLED, session.upload);
    TEST_ASSERT_EQUAL(SIZE_MAX, session.socket.output.find("0\r\n\r\n"));
    session.audio.stop();
    TEST_ASSERT_FALSE(session.audio.startStreaming());
    session.acknowledge();
  }
}

void test_success_response_begun_during_capture_stays_premature_across_final_chunk()
{
  Session session;
  session.start();
  session.frames(6, 1000);
  session.frames(249, 0);
  session.socket.incoming = "HTTP/1.1 200 OK"; // One frame before finalization, without CRLF.
  session.upload.update(fakeMillis(), session.socket);
  TEST_ASSERT_FALSE(session.upload.done());
  TEST_ASSERT_EQUAL_INT(static_cast<int>(AudioStatus::RECORDING), static_cast<int>(session.audio.status()));
  session.frames(1, 0);
  bool finalized;
  decoded(session.socket.output, finalized);
  TEST_ASSERT_TRUE(finalized);
  session.socket.incoming = "\r\nContent-Type: text/plain; charset=utf-8\r\nContent-Length: 5\r\n\r\nhello";
  session.upload.update(fakeMillis() + 1, session.socket);
  assertError(Error::MALFORMED_RESPONSE, session.upload);
  session.acknowledge();
}

void test_readable_first_response_obeys_exact_deadline_before_reading()
{
  for (uint32_t start : {0u, 0xFFFFE000u})
  {
    for (uint32_t wait : {24999u, 25000u})
    {
      fakeMillis() = start;
      Session session;
      session.start();
      session.frames(6, 1000);
      session.frames(250, 0);
      session.socket.respond();
      session.upload.update(fakeMillis() + wait, session.socket);
      assertError(wait == 25000 ? Error::TIMED_OUT : Error::NONE, session.upload);
      if (wait == 25000)
      {
        TEST_ASSERT_FALSE(session.socket.incoming.empty()); // Expiry wins over readable data.
        TEST_ASSERT_EQUAL_UINT(0, session.parser.textLength());
      }
      session.acknowledge();
    }
  }
}

void test_readable_response_remainder_obeys_exact_idle_deadline_before_reading()
{
  for (uint32_t start : {0u, 0xFFFFE000u})
  {
    for (uint32_t wait : {4999u, 5000u})
    {
      fakeMillis() = start;
      Session session;
      session.start();
      session.frames(6, 1000);
      session.frames(250, 0);
      session.socket.respond();
      const std::string response = session.socket.incoming;
      session.socket.incoming = response.substr(0, response.size() - 3); // Body is only "he".
      const uint32_t fragmentAt = fakeMillis() + 100;
      session.upload.update(fragmentAt, session.socket);
      TEST_ASSERT_FALSE(session.upload.done());
      TEST_ASSERT_EQUAL_UINT(2, session.parser.textLength());
      session.socket.incoming = "llo";
      session.upload.update(fragmentAt + wait, session.socket);
      assertError(wait == 5000 ? Error::TIMED_OUT : Error::NONE, session.upload);
      if (wait == 5000)
      {
        TEST_ASSERT_EQUAL_STRING("llo", session.socket.incoming.c_str());
        TEST_ASSERT_EQUAL_UINT(2, session.parser.textLength());
      }
      session.acknowledge();
    }
  }
}

int main()
{
  UNITY_BEGIN();
  RUN_TEST(test_success_response_begun_during_capture_stays_premature_across_final_chunk);
  RUN_TEST(test_readable_first_response_obeys_exact_deadline_before_reading);
  RUN_TEST(test_readable_response_remainder_obeys_exact_idle_deadline_before_reading);
  RUN_TEST(test_premature_success_and_logical_abort_cannot_finalize_a_capture);
  RUN_TEST(test_seventy_seconds_wraps_ordered_pcm_and_result_clock_starts_after_capture);
  RUN_TEST(test_driver_release_precedes_classification_and_partial_pcm_retains_slot);
  RUN_TEST(test_exact_tail_excludes_unclassified_driver_tail_and_final_waits_for_join);
  RUN_TEST(test_quiet_noise_and_minimum_cumulative_activity_cancel_or_finish_once);
  RUN_TEST(test_exhaustion_and_capture_failure_cancel_without_final_chunk);
  RUN_TEST(test_rejection_disconnect_and_io_error_are_observed_during_capture);
  RUN_TEST(test_write_and_response_idle_deadlines_reset_on_progress_and_roll_over);
  RUN_TEST(test_reusable_ring_allocation_and_start_failure_can_retry_without_clip_allocation);
  return UNITY_END();
}
