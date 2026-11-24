#include <unity.h>
#include "RobotStateMachine.h"

RobotStateMachine machine;
using AudioStatus = RobotAudio::Status;
using Inputs = RobotStateMachine::Inputs;
using ResultStatus = RobotStateMachine::ResultStatus;
using Completion = RobotStateMachine::Completion;
using Failure = RobotStateMachine::Failure;

void setUp()
{
  machine = RobotStateMachine{};
}

void tearDown()
{
}

bool tick(bool pressed, uint32_t now, Inputs inputs = Inputs{})
{
  return machine.update(pressed, now, inputs);
}

Inputs audio(AudioStatus status)
{
  Inputs inputs;
  inputs.audioStatus = status;
  return inputs;
}

Inputs result(uint32_t requestId, ResultStatus status)
{
  Inputs inputs;
  inputs.resultRequestId = requestId;
  inputs.resultStatus = status;
  return inputs;
}

void assertState(RobotState expected)
{
  TEST_ASSERT_EQUAL_INT(static_cast<int>(expected), static_cast<int>(machine.getState()));
}

void assertFailure(Failure expected)
{
  TEST_ASSERT_EQUAL_INT(static_cast<int>(expected), static_cast<int>(machine.failure()));
}

void assertCompletion(Completion expected)
{
  TEST_ASSERT_EQUAL_INT(static_cast<int>(expected), static_cast<int>(machine.actions().completion));
}

void assertNoActions()
{
  TEST_ASSERT_FALSE(machine.actions().startRecording);
  TEST_ASSERT_FALSE(machine.actions().submitTranscription);
  assertCompletion(Completion::NONE);
}

uint32_t enterThinking(uint32_t now)
{
  machine.begin(now);
  TEST_ASSERT_TRUE(tick(true, now));
  TEST_ASSERT_TRUE(machine.actions().startRecording);
  TEST_ASSERT_TRUE(tick(false, now + 2000, audio(AudioStatus::RECORDED)));
  TEST_ASSERT_TRUE(machine.actions().submitTranscription);
  assertState(RobotState::THINKING);
  return machine.requestId();
}

void test_startup_is_idle_without_actions()
{
  machine.begin(100);
  assertState(RobotState::IDLE);
  TEST_ASSERT_FALSE(tick(false, 100));
  TEST_ASSERT_FALSE(machine.didFail());
  assertNoActions();
}

void test_unavailable_press_reports_once_without_capturing_and_can_retry()
{
  Inputs unavailable;
  unavailable.transcriptionReady = false;
  TEST_ASSERT_FALSE(tick(true, 100, unavailable));
  assertState(RobotState::IDLE);
  assertFailure(Failure::TRANSCRIPTION_UNAVAILABLE);
  assertNoActions();
  TEST_ASSERT_FALSE(tick(false, 101, unavailable)); // Held touch has no new press.
  TEST_ASSERT_FALSE(machine.didFail());
  TEST_ASSERT_FALSE(tick(false, 30099, unavailable));
  TEST_ASSERT_TRUE(tick(true, 30100)); // A fresh press takes precedence over sleep.
  assertState(RobotState::LISTENING);
  TEST_ASSERT_TRUE(machine.actions().startRecording);
}

void test_busy_presses_and_held_touches_do_not_queue_actions()
{
  TEST_ASSERT_TRUE(tick(true, 100));
  TEST_ASSERT_TRUE(machine.actions().startRecording);
  TEST_ASSERT_FALSE(tick(true, 101, audio(AudioStatus::RECORDING)));
  assertNoActions();
  TEST_ASSERT_FALSE(tick(false, 102, audio(AudioStatus::RECORDING)));
  assertNoActions();
  TEST_ASSERT_TRUE(tick(true, 2100, audio(AudioStatus::RECORDED)));
  TEST_ASSERT_TRUE(machine.actions().submitTranscription);
  TEST_ASSERT_FALSE(tick(true, 2101, audio(AudioStatus::RECORDED)));
  assertNoActions();
  TEST_ASSERT_FALSE(tick(false, 2102));
  assertNoActions();
  assertState(RobotState::THINKING);
}

void test_ten_cycles_capture_submit_and_complete_once()
{
  unsigned captures = 0;
  unsigned submissions = 0;
  unsigned transcripts = 0;
  unsigned emptyResults = 0;
  for (uint32_t cycle = 0; cycle < 10; ++cycle)
  {
    const uint32_t now = 100 + cycle * 5000;
    TEST_ASSERT_TRUE(tick(true, now));
    captures += machine.actions().startRecording;
    TEST_ASSERT_EQUAL_UINT32(cycle + 1, machine.requestId());
    const uint32_t id = machine.requestId();
    TEST_ASSERT_FALSE(tick(false, now + 1, audio(AudioStatus::COMPLETE)));
    assertNoActions();
    TEST_ASSERT_TRUE(tick(true, now + 2000, audio(AudioStatus::RECORDED)));
    submissions += machine.actions().submitTranscription;
    TEST_ASSERT_FALSE(tick(true, now + 2001, audio(AudioStatus::RECORDED)));
    assertNoActions();
    const auto status = cycle % 2 == 0 ? ResultStatus::TRANSCRIPT : ResultStatus::EMPTY;
    TEST_ASSERT_TRUE(tick(true, now + 4000, result(id, status)));
    transcripts += machine.actions().completion == Completion::TRANSCRIPT;
    emptyResults += machine.actions().completion == Completion::NO_SPEECH;
    assertState(RobotState::IDLE);
    TEST_ASSERT_FALSE(machine.didFail());
    TEST_ASSERT_FALSE(tick(true, now + 4001, result(id, status)));
    assertNoActions(); // Duplicate result and coincident press cannot print or record.
    TEST_ASSERT_FALSE(tick(false, now + 4002));
    assertNoActions();
  }
  TEST_ASSERT_EQUAL_UINT(10, captures);
  TEST_ASSERT_EQUAL_UINT(10, submissions);
  TEST_ASSERT_EQUAL_UINT(5, transcripts);
  TEST_ASSERT_EQUAL_UINT(5, emptyResults);
}

void test_transcription_wait_survives_capture_watchdog()
{
  const uint32_t id = enterThinking(100);
  TEST_ASSERT_FALSE(tick(false, 7100, audio(AudioStatus::RECORDED)));
  assertState(RobotState::THINKING);
  TEST_ASSERT_FALSE(machine.didFail());
  assertNoActions();
  TEST_ASSERT_TRUE(tick(false, 32099, result(id, ResultStatus::TRANSCRIPT)));
  assertCompletion(Completion::TRANSCRIPT);
  assertState(RobotState::IDLE);
}

void test_playback_status_does_not_enter_speaking()
{
  enterThinking(100);
  TEST_ASSERT_FALSE(tick(false, 2101, audio(AudioStatus::PLAYING)));
  TEST_ASSERT_FALSE(tick(false, 2102, audio(AudioStatus::COMPLETE)));
  assertState(RobotState::THINKING);
  assertNoActions();
}

void test_capture_watchdog_boundary_and_rollover_reject_late_completion()
{
  const uint32_t starts[] = {100, 0xFFFFF000u};
  for (uint32_t start : starts)
  {
    machine.begin(start);
    TEST_ASSERT_TRUE(tick(true, start));
    TEST_ASSERT_FALSE(tick(true, start + 4999, audio(AudioStatus::RECORDING)));
    TEST_ASSERT_TRUE(tick(true, start + 5000, audio(AudioStatus::RECORDED)));
    assertState(RobotState::IDLE);
    assertFailure(Failure::CAPTURE_TIMED_OUT);
    assertNoActions();
    TEST_ASSERT_FALSE(tick(false, start + 5001, audio(AudioStatus::RECORDED)));
    TEST_ASSERT_FALSE(machine.didFail());
    TEST_ASSERT_TRUE(tick(true, start + 5002));
    TEST_ASSERT_TRUE(machine.actions().startRecording);
  }
}

void test_request_watchdog_has_its_own_boundary_and_rollover()
{
  const uint32_t starts[] = {100, 0xFFFFF000u};
  for (uint32_t start : starts)
  {
    machine.begin(start);
    TEST_ASSERT_TRUE(tick(true, start));
    TEST_ASSERT_TRUE(tick(false, start + 4999, audio(AudioStatus::RECORDED)));
    const uint32_t submittedAt = start + 4999;
    Inputs pending;
    pending.requestPending = true;
    TEST_ASSERT_FALSE(tick(true, submittedAt + 29999, pending));
    assertState(RobotState::THINKING);
    TEST_ASSERT_FALSE(machine.didFail());
    TEST_ASSERT_TRUE(tick(true, submittedAt + 30000,
        result(machine.requestId(), ResultStatus::TRANSCRIPT)));
    assertState(RobotState::IDLE);
    assertFailure(Failure::TRANSCRIPTION_TIMED_OUT);
    assertNoActions(); // A simultaneous result cannot win at the deadline.
    TEST_ASSERT_FALSE(tick(false, submittedAt + 30001, pending));
    TEST_ASSERT_FALSE(machine.didFail());
  }
}

void test_capture_driver_failure_and_immediate_admission_failure_allow_retry()
{
  machine.begin(100);
  TEST_ASSERT_TRUE(tick(true, 100));
  TEST_ASSERT_TRUE(tick(false, 101, audio(AudioStatus::FAILED)));
  assertFailure(Failure::CAPTURE_FAILED);
  assertState(RobotState::IDLE);
  assertNoActions();
  TEST_ASSERT_FALSE(tick(false, 102, audio(AudioStatus::FAILED)));
  TEST_ASSERT_FALSE(machine.didFail());
  TEST_ASSERT_TRUE(tick(true, 103));
  TEST_ASSERT_TRUE(tick(false, 2103, audio(AudioStatus::RECORDED)));
  const uint32_t rejectedId = machine.requestId();
  TEST_ASSERT_TRUE(tick(false, 2103, result(rejectedId, ResultStatus::FAILED)));
  assertFailure(Failure::TRANSCRIPTION_FAILED);
  assertNoActions();
  TEST_ASSERT_FALSE(tick(false, 2104, result(rejectedId, ResultStatus::FAILED)));
  TEST_ASSERT_FALSE(machine.didFail());
  TEST_ASSERT_TRUE(tick(true, 2105));
  TEST_ASSERT_TRUE(machine.actions().startRecording);
  TEST_ASSERT_NOT_EQUAL(rejectedId, machine.requestId());
}

void test_worker_failure_is_one_diagnostic_signal_and_refreshes_inactivity()
{
  const uint32_t id = enterThinking(100);
  TEST_ASSERT_TRUE(tick(true, 2500, result(id, ResultStatus::FAILED)));
  assertFailure(Failure::TRANSCRIPTION_FAILED);
  assertState(RobotState::IDLE);
  assertNoActions();
  TEST_ASSERT_FALSE(tick(false, 2501, result(id, ResultStatus::FAILED)));
  TEST_ASSERT_FALSE(machine.didFail());
  TEST_ASSERT_FALSE(tick(false, 32499));
  TEST_ASSERT_TRUE(tick(false, 32500));
  assertState(RobotState::SLEEPING);
}

void test_timeout_holds_admission_until_ack_and_discards_late_transcript()
{
  const uint32_t id = enterThinking(100);
  Inputs pending;
  pending.requestPending = true;
  // Even a readiness snapshot saying ready cannot overrule buffer ownership.
  pending.transcriptionReady = true;
  TEST_ASSERT_TRUE(tick(false, 32100, pending));
  assertFailure(Failure::TRANSCRIPTION_TIMED_OUT);
  TEST_ASSERT_FALSE(tick(true, 32101, pending));
  TEST_ASSERT_FALSE(machine.didFail());
  assertNoActions();
  TEST_ASSERT_FALSE(tick(false, 32102, pending));
  TEST_ASSERT_FALSE(tick(true, 32103, result(id, ResultStatus::TRANSCRIPT)));
  assertState(RobotState::IDLE);
  assertNoActions();
  TEST_ASSERT_FALSE(tick(false, 32104));
  TEST_ASSERT_TRUE(tick(true, 32105));
  TEST_ASSERT_TRUE(machine.actions().startRecording);
  TEST_ASSERT_NOT_EQUAL(id, machine.requestId());
}

void test_stale_ids_cannot_complete_or_fail_a_new_request()
{
  const uint32_t oldId = enterThinking(100);
  TEST_ASSERT_TRUE(tick(false, 2200, result(oldId, ResultStatus::TRANSCRIPT)));
  TEST_ASSERT_TRUE(tick(true, 2300));
  TEST_ASSERT_TRUE(tick(false, 4300, audio(AudioStatus::RECORDED)));
  const uint32_t currentId = machine.requestId();
  TEST_ASSERT_FALSE(tick(true, 4301, result(oldId, ResultStatus::TRANSCRIPT)));
  assertNoActions();
  TEST_ASSERT_FALSE(tick(false, 4302, result(oldId, ResultStatus::FAILED)));
  TEST_ASSERT_FALSE(machine.didFail());
  assertState(RobotState::THINKING);
  TEST_ASSERT_TRUE(tick(true, 4303, result(currentId, ResultStatus::EMPTY)));
  assertCompletion(Completion::NO_SPEECH);
  TEST_ASSERT_FALSE(tick(false, 4304));
  assertNoActions();
}

void test_idle_pending_transport_rejects_capture_without_duplicate_error()
{
  Inputs pending;
  pending.requestPending = true;
  TEST_ASSERT_FALSE(tick(true, 100, pending));
  assertState(RobotState::IDLE);
  assertNoActions();
  TEST_ASSERT_FALSE(machine.didFail());
  TEST_ASSERT_TRUE(tick(false, 30000, pending));
  assertState(RobotState::SLEEPING);
  TEST_ASSERT_TRUE(tick(true, 30001, pending));
  assertState(RobotState::IDLE);
  TEST_ASSERT_FALSE(tick(true, 30002, pending));
  assertNoActions();
}

void test_idle_sleep_boundary_and_press_precedence()
{
  TEST_ASSERT_FALSE(tick(false, 29999));
  TEST_ASSERT_TRUE(tick(true, 30000));
  assertState(RobotState::LISTENING);
  machine.begin();
  TEST_ASSERT_TRUE(tick(false, 30000));
  assertState(RobotState::SLEEPING);
  TEST_ASSERT_FALSE(tick(false, 60000));
  assertState(RobotState::SLEEPING);
}

void test_idle_inactivity_boundary_across_uint32_rollover()
{
  machine.begin(0xFFFFF000u);
  TEST_ASSERT_FALSE(tick(false, 0xFFFFFFFFu));
  TEST_ASSERT_FALSE(tick(false, 25903)); // Elapsed 29,999 ms.
  TEST_ASSERT_TRUE(tick(false, 25904));
  assertState(RobotState::SLEEPING);
  TEST_ASSERT_TRUE(tick(true, 26004));
  assertState(RobotState::IDLE);
  assertNoActions();
}

void test_repeated_sleep_wake_requires_a_fresh_recording_press()
{
  for (uint32_t cycle = 0; cycle < 3; ++cycle)
  {
    const uint32_t sleepAt = 30000 + cycle * 30100;
    TEST_ASSERT_TRUE(tick(false, sleepAt));
    assertState(RobotState::SLEEPING);
    TEST_ASSERT_TRUE(tick(true, sleepAt + 100));
    assertState(RobotState::IDLE);
    assertNoActions();
    TEST_ASSERT_FALSE(tick(false, sleepAt + 101));
    TEST_ASSERT_FALSE(tick(false, sleepAt + 30099));
  }
  TEST_ASSERT_TRUE(tick(true, 120300));
  TEST_ASSERT_TRUE(machine.actions().startRecording);
}

void test_all_completion_and_failure_paths_refresh_idle_clock()
{
  const uint32_t starts[] = {100, 0xFFFFF000u};
  for (uint32_t start : starts)
  {
    for (unsigned outcome = 0; outcome < 6; ++outcome)
    {
      uint32_t endedAt;
      if (outcome < 3 || outcome == 5)
      {
        const uint32_t id = enterThinking(start);
        endedAt = start + (outcome == 5 ? 32000 : 2100);
        const ResultStatus statuses[] = {
            ResultStatus::TRANSCRIPT, ResultStatus::EMPTY, ResultStatus::FAILED};
        TEST_ASSERT_TRUE(tick(true, endedAt,
            outcome == 5 ? Inputs{} : result(id, statuses[outcome])));
      }
      else
      {
        machine.begin(start);
        TEST_ASSERT_TRUE(tick(true, start));
        endedAt = start + (outcome == 3 ? 100 : 5000);
        TEST_ASSERT_TRUE(tick(true, endedAt,
            audio(outcome == 3 ? AudioStatus::FAILED : AudioStatus::RECORDING)));
      }
      assertState(RobotState::IDLE);
      TEST_ASSERT_FALSE(tick(false, endedAt + 29999));
      TEST_ASSERT_TRUE(tick(false, endedAt + 30000));
      assertState(RobotState::SLEEPING);
      TEST_ASSERT_TRUE(tick(true, endedAt + 30001));
      assertState(RobotState::IDLE);
      assertNoActions();
    }
  }
}

void test_begin_resets_actions_failures_ids_and_uses_current_uptime()
{
  TEST_ASSERT_TRUE(tick(true, 100));
  machine.begin(90000);
  assertState(RobotState::IDLE);
  assertNoActions();
  TEST_ASSERT_FALSE(machine.didFail());
  TEST_ASSERT_EQUAL_UINT32(0, machine.requestId());
  TEST_ASSERT_FALSE(tick(false, 119999));
  TEST_ASSERT_TRUE(tick(false, 120000));
  assertState(RobotState::SLEEPING);
}

int main()
{
  UNITY_BEGIN();
  RUN_TEST(test_startup_is_idle_without_actions);
  RUN_TEST(test_unavailable_press_reports_once_without_capturing_and_can_retry);
  RUN_TEST(test_busy_presses_and_held_touches_do_not_queue_actions);
  RUN_TEST(test_ten_cycles_capture_submit_and_complete_once);
  RUN_TEST(test_transcription_wait_survives_capture_watchdog);
  RUN_TEST(test_playback_status_does_not_enter_speaking);
  RUN_TEST(test_capture_watchdog_boundary_and_rollover_reject_late_completion);
  RUN_TEST(test_request_watchdog_has_its_own_boundary_and_rollover);
  RUN_TEST(test_capture_driver_failure_and_immediate_admission_failure_allow_retry);
  RUN_TEST(test_worker_failure_is_one_diagnostic_signal_and_refreshes_inactivity);
  RUN_TEST(test_timeout_holds_admission_until_ack_and_discards_late_transcript);
  RUN_TEST(test_stale_ids_cannot_complete_or_fail_a_new_request);
  RUN_TEST(test_idle_pending_transport_rejects_capture_without_duplicate_error);
  RUN_TEST(test_idle_sleep_boundary_and_press_precedence);
  RUN_TEST(test_idle_inactivity_boundary_across_uint32_rollover);
  RUN_TEST(test_repeated_sleep_wake_requires_a_fresh_recording_press);
  RUN_TEST(test_all_completion_and_failure_paths_refresh_idle_clock);
  RUN_TEST(test_begin_resets_actions_failures_ids_and_uses_current_uptime);
  return UNITY_END();
}
