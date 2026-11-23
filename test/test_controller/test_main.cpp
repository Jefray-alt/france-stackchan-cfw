#include <unity.h>
#include "RobotStateMachine.h"

RobotStateMachine machine;
using AudioStatus = RobotAudio::Status;

void setUp()
{
  machine = RobotStateMachine{};
}

void tearDown()
{
}

void assertState(RobotState expected)
{
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(expected), static_cast<int>(machine.getState()));
}

void test_startup_is_idle_without_a_transition()
{
  machine.begin();

  assertState(RobotState::IDLE);
  TEST_ASSERT_FALSE(machine.update(false, 0));
  assertState(RobotState::IDLE);
}

void enterPhase(unsigned phase, uint32_t now)
{
  machine.begin(now);
  TEST_ASSERT_TRUE(machine.update(true, now));
  if (phase >= 1)
  {
    TEST_ASSERT_TRUE(machine.update(false, now + 100, AudioStatus::RECORDED));
  }
  if (phase >= 2)
  {
    TEST_ASSERT_TRUE(machine.update(false, now + 101, AudioStatus::PLAYING));
  }
}

void test_busy_presses_do_not_cycle_or_queue()
{
  for (unsigned phase = 0; phase < 3; ++phase)
  {
    enterPhase(phase, 100);
    RobotState expected = machine.getState();
    TEST_ASSERT_FALSE(machine.update(true, 500));
    TEST_ASSERT_FALSE(machine.update(true, 600));
    TEST_ASSERT_FALSE(machine.update(false, 700)); // A held touch has no new event.
    assertState(expected);
  }
}

void test_inactivity_sleeps_and_stays_asleep()
{
  TEST_ASSERT_TRUE(machine.update(false, 30000));
  assertState(RobotState::SLEEPING);
  TEST_ASSERT_FALSE(machine.update(false, 60000));
  assertState(RobotState::SLEEPING);
}

void test_inactivity_starts_exactly_at_thirty_seconds()
{
  TEST_ASSERT_FALSE(machine.update(false, 29999));
  assertState(RobotState::IDLE);
  TEST_ASSERT_TRUE(machine.update(false, 30000));
  assertState(RobotState::SLEEPING);
}

void test_press_at_idle_timeout_starts_capture()
{
  TEST_ASSERT_TRUE(machine.update(true, 30000));
  assertState(RobotState::LISTENING);
  TEST_ASSERT_FALSE(machine.update(false, 30001));
  assertState(RobotState::LISTENING);
}

void test_ten_cycles_advance_once_and_ignore_completion_presses()
{
  for (uint32_t now = 100; now < 41100; now += 4100)
  {
    TEST_ASSERT_TRUE(machine.update(true, now));
    assertState(RobotState::LISTENING);
    TEST_ASSERT_FALSE(machine.update(false, now + 1, AudioStatus::COMPLETE));
    TEST_ASSERT_TRUE(machine.update(true, now + 2000, AudioStatus::RECORDED));
    assertState(RobotState::THINKING);
    TEST_ASSERT_FALSE(machine.update(true, now + 2001, AudioStatus::RECORDED));
    TEST_ASSERT_TRUE(machine.update(true, now + 2002, AudioStatus::PLAYING));
    assertState(RobotState::SPEAKING);
    TEST_ASSERT_FALSE(machine.update(true, now + 2003, AudioStatus::PLAYING));
    TEST_ASSERT_TRUE(machine.update(true, now + 4000, AudioStatus::COMPLETE));
    assertState(RobotState::IDLE);
    TEST_ASSERT_FALSE(machine.didFail());
    TEST_ASSERT_FALSE(machine.update(false, now + 4001, AudioStatus::COMPLETE));
    TEST_ASSERT_FALSE(machine.update(false, now + 4002, AudioStatus::RECORDED));
  }
}

void test_inactivity_boundary_across_uint32_rollover()
{
  machine.begin(0xFFFFF000u);
  TEST_ASSERT_FALSE(machine.update(false, 0xFFFFFFFFu));
  // 4096 ms remain before wrap; 25903/25904 are elapsed 29999/30000.
  TEST_ASSERT_FALSE(machine.update(false, 25903));
  assertState(RobotState::IDLE);
  TEST_ASSERT_TRUE(machine.update(false, 25904));
  assertState(RobotState::SLEEPING);
  TEST_ASSERT_TRUE(machine.update(true, 26004));
  assertState(RobotState::IDLE);
}

void test_repeated_sleep_wake_returns_idle_and_requires_new_press()
{
  for (uint32_t sleepAt = 30000; sleepAt <= 90200; sleepAt += 30100)
  {
    TEST_ASSERT_TRUE(machine.update(false, sleepAt));
    assertState(RobotState::SLEEPING);
    TEST_ASSERT_FALSE(machine.update(false, sleepAt + 50));
    assertState(RobotState::SLEEPING);
    TEST_ASSERT_TRUE(machine.update(true, sleepAt + 100));
    assertState(RobotState::IDLE);
    TEST_ASSERT_FALSE(machine.update(false, sleepAt + 30099));
    assertState(RobotState::IDLE);
  }
  TEST_ASSERT_TRUE(machine.update(true, 120300));
  assertState(RobotState::LISTENING);
}

void test_failure_at_each_phase_is_once_and_allows_fresh_retry()
{
  for (unsigned phase = 0; phase < 3; ++phase)
  {
    enterPhase(phase, 100);
    TEST_ASSERT_TRUE(machine.update(true, 202, AudioStatus::FAILED));
    assertState(RobotState::IDLE);
    TEST_ASSERT_TRUE(machine.didFail());
    TEST_ASSERT_FALSE(machine.update(false, 203, AudioStatus::FAILED));
    TEST_ASSERT_FALSE(machine.didFail());
    TEST_ASSERT_FALSE(machine.update(false, 204, AudioStatus::RECORDED));
    TEST_ASSERT_TRUE(machine.update(true, 205));
    assertState(RobotState::LISTENING);
    TEST_ASSERT_FALSE(machine.update(false, 206, AudioStatus::COMPLETE));
    TEST_ASSERT_TRUE(machine.update(false, 2205, AudioStatus::RECORDED));
    assertState(RobotState::THINKING);
  }
}

void test_watchdog_boundaries_and_rollover_at_each_phase()
{
  const uint32_t starts[] = {100, 0xFFFFF000u};
  const AudioStatus completions[] = {
      AudioStatus::RECORDED, AudioStatus::PLAYING, AudioStatus::COMPLETE};
  for (uint32_t start : starts)
  {
    for (unsigned phase = 0; phase < 3; ++phase)
    {
      enterPhase(phase, start);
      uint32_t operationStart = start + (phase == 0 ? 0 : 100);
      TEST_ASSERT_FALSE(machine.update(true, operationStart + 4999));
      TEST_ASSERT_TRUE(machine.update(true, operationStart + 5000, completions[phase]));
      assertState(RobotState::IDLE);
      TEST_ASSERT_TRUE(machine.didFail());
      TEST_ASSERT_FALSE(machine.update(false, operationStart + 5001, AudioStatus::COMPLETE));
      TEST_ASSERT_FALSE(machine.didFail());
      TEST_ASSERT_TRUE(machine.update(true, operationStart + 5002));
      assertState(RobotState::LISTENING);
    }
  }
}

void test_completion_failure_and_timeout_refresh_idle_clock()
{
  const uint32_t starts[] = {100, 0xFFFFF000u};
  for (uint32_t start : starts)
  {
    for (unsigned outcome = 0; outcome < 3; ++outcome)
    {
      enterPhase(outcome == 0 ? 2 : 0, start);
      uint32_t endedAt = start + (outcome == 2 ? 5000 : 2100);
      AudioStatus status = outcome == 0 ? AudioStatus::COMPLETE :
          (outcome == 1 ? AudioStatus::FAILED : AudioStatus::RECORDING);
      TEST_ASSERT_TRUE(machine.update(true, endedAt, status));
      assertState(RobotState::IDLE);
      TEST_ASSERT_FALSE(machine.update(false, endedAt + 29999));
      TEST_ASSERT_TRUE(machine.update(false, endedAt + 30000));
      assertState(RobotState::SLEEPING);
      TEST_ASSERT_TRUE(machine.update(true, endedAt + 30001));
      assertState(RobotState::IDLE);
    }
  }
}

void test_begin_resets_state_and_uses_current_uptime()
{
  TEST_ASSERT_TRUE(machine.update(true, 100));
  machine.begin(90000);
  assertState(RobotState::IDLE);
  TEST_ASSERT_FALSE(machine.didFail());
  TEST_ASSERT_FALSE(machine.update(false, 119999));
  TEST_ASSERT_TRUE(machine.update(false, 120000));
  assertState(RobotState::SLEEPING);
}

int main()
{
  UNITY_BEGIN();
  RUN_TEST(test_startup_is_idle_without_a_transition);
  RUN_TEST(test_busy_presses_do_not_cycle_or_queue);
  RUN_TEST(test_inactivity_sleeps_and_stays_asleep);
  RUN_TEST(test_inactivity_starts_exactly_at_thirty_seconds);
  RUN_TEST(test_press_at_idle_timeout_starts_capture);
  RUN_TEST(test_ten_cycles_advance_once_and_ignore_completion_presses);
  RUN_TEST(test_inactivity_boundary_across_uint32_rollover);
  RUN_TEST(test_repeated_sleep_wake_returns_idle_and_requires_new_press);
  RUN_TEST(test_failure_at_each_phase_is_once_and_allows_fresh_retry);
  RUN_TEST(test_watchdog_boundaries_and_rollover_at_each_phase);
  RUN_TEST(test_completion_failure_and_timeout_refresh_idle_clock);
  RUN_TEST(test_begin_resets_state_and_uses_current_uptime);
  return UNITY_END();
}
