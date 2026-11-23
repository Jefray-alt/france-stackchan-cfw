#include <unity.h>
#include "RobotStateMachine.h"

RobotStateMachine machine;

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

void test_presses_cycle_through_all_awake_states()
{
  TEST_ASSERT_TRUE(machine.update(true, 100));
  assertState(RobotState::LISTENING);
  TEST_ASSERT_TRUE(machine.update(true, 200));
  assertState(RobotState::THINKING);
  TEST_ASSERT_TRUE(machine.update(true, 300));
  assertState(RobotState::SPEAKING);
  TEST_ASSERT_TRUE(machine.update(true, 400));
  assertState(RobotState::IDLE);
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

void test_press_refreshes_inactivity_timer()
{
  TEST_ASSERT_TRUE(machine.update(true, 20000));
  assertState(RobotState::LISTENING);
  TEST_ASSERT_FALSE(machine.update(false, 30000));
  assertState(RobotState::LISTENING);
  TEST_ASSERT_FALSE(machine.update(false, 49999));
  assertState(RobotState::LISTENING);
  TEST_ASSERT_TRUE(machine.update(false, 50000));
  assertState(RobotState::SLEEPING);
}

void test_press_at_timeout_takes_precedence_and_refreshes_timer()
{
  TEST_ASSERT_TRUE(machine.update(true, 30000));
  assertState(RobotState::LISTENING);
  TEST_ASSERT_FALSE(machine.update(false, 30001));
  assertState(RobotState::LISTENING);
  TEST_ASSERT_FALSE(machine.update(false, 59999));
  assertState(RobotState::LISTENING);
  TEST_ASSERT_TRUE(machine.update(false, 60000));
  assertState(RobotState::SLEEPING);
}

void test_no_additional_cycle_without_another_press_event()
{
  TEST_ASSERT_TRUE(machine.update(true, 100));
  assertState(RobotState::LISTENING);

  for (uint32_t now = 101; now <= 103; ++now)
  {
    TEST_ASSERT_FALSE(machine.update(false, now));
    assertState(RobotState::LISTENING);
  }

  TEST_ASSERT_TRUE(machine.update(true, 104));
  assertState(RobotState::THINKING);
  TEST_ASSERT_FALSE(machine.update(false, 105));
  assertState(RobotState::THINKING);
}

void test_inactivity_boundary_across_uint32_rollover()
{
  TEST_ASSERT_TRUE(machine.setState(RobotState::SPEAKING, 0xFFFFF000u));
  TEST_ASSERT_FALSE(machine.update(false, 0xFFFFFFFFu));
  assertState(RobotState::SPEAKING);
  // 4096 ms remain before wrap; 25903/25904 are elapsed 29999/30000.
  TEST_ASSERT_FALSE(machine.update(false, 25903));
  assertState(RobotState::SPEAKING);
  TEST_ASSERT_TRUE(machine.update(false, 25904));
  assertState(RobotState::SLEEPING);
  TEST_ASSERT_TRUE(machine.update(true, 26004));
  assertState(RobotState::SPEAKING);
}

void assertWakeRestores(RobotState awakeState)
{
  machine.setState(awakeState, 100);

  for (uint32_t sleepAt = 30100; sleepAt <= 90300; sleepAt += 30100)
  {
    TEST_ASSERT_TRUE(machine.update(false, sleepAt));
    assertState(RobotState::SLEEPING);
    TEST_ASSERT_FALSE(machine.update(false, sleepAt + 50));
    assertState(RobotState::SLEEPING);
    TEST_ASSERT_TRUE(machine.update(true, sleepAt + 100));
    assertState(awakeState);
    TEST_ASSERT_FALSE(machine.update(false, sleepAt + 30099));
    assertState(awakeState);
  }
}

void test_wake_restores_idle()
{
  assertWakeRestores(RobotState::IDLE);
}

void test_wake_restores_listening()
{
  assertWakeRestores(RobotState::LISTENING);
}

void test_wake_restores_thinking()
{
  assertWakeRestores(RobotState::THINKING);
}

void test_wake_restores_speaking()
{
  assertWakeRestores(RobotState::SPEAKING);
}

void test_same_state_assignment_does_not_refresh_inactivity()
{
  TEST_ASSERT_FALSE(machine.setState(RobotState::IDLE, 20000));
  TEST_ASSERT_TRUE(machine.update(false, 30000));
  assertState(RobotState::SLEEPING);
  TEST_ASSERT_FALSE(machine.setState(RobotState::SLEEPING, 31000));
  TEST_ASSERT_TRUE(machine.update(true, 32000));
  assertState(RobotState::IDLE);
}

void test_begin_retains_awake_state_and_resets_timer_to_zero()
{
  TEST_ASSERT_TRUE(machine.setState(RobotState::SPEAKING, 90000));

  machine.begin();

  assertState(RobotState::SPEAKING);
  TEST_ASSERT_TRUE(machine.update(false, 90001));
  assertState(RobotState::SLEEPING);
  TEST_ASSERT_TRUE(machine.update(true, 90002));
  assertState(RobotState::SPEAKING);
}

void test_begin_retains_sleeping_state_and_wake_target()
{
  machine.setState(RobotState::THINKING, 100);
  machine.setState(RobotState::SLEEPING, 200);

  machine.begin();

  assertState(RobotState::SLEEPING);
  TEST_ASSERT_TRUE(machine.update(true, 300));
  assertState(RobotState::THINKING);
}

int main()
{
  UNITY_BEGIN();
  RUN_TEST(test_startup_is_idle_without_a_transition);
  RUN_TEST(test_presses_cycle_through_all_awake_states);
  RUN_TEST(test_inactivity_sleeps_and_stays_asleep);
  RUN_TEST(test_inactivity_starts_exactly_at_thirty_seconds);
  RUN_TEST(test_press_refreshes_inactivity_timer);
  RUN_TEST(test_press_at_timeout_takes_precedence_and_refreshes_timer);
  RUN_TEST(test_no_additional_cycle_without_another_press_event);
  RUN_TEST(test_inactivity_boundary_across_uint32_rollover);
  RUN_TEST(test_wake_restores_idle);
  RUN_TEST(test_wake_restores_listening);
  RUN_TEST(test_wake_restores_thinking);
  RUN_TEST(test_wake_restores_speaking);
  RUN_TEST(test_same_state_assignment_does_not_refresh_inactivity);
  RUN_TEST(test_begin_retains_awake_state_and_resets_timer_to_zero);
  RUN_TEST(test_begin_retains_sleeping_state_and_wake_target);
  return UNITY_END();
}
