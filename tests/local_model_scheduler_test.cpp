#include "agent/local_model_adapter.h"
#include "agent/parallel_tab_scheduler.h"

#include <gtest/gtest.h>

#include <array>
#include <chrono>
#include <map>
#include <string>

using namespace std::chrono_literals;

TEST(LocalModelAdapterTest, LoopbackPolicyIsFailClosed) {
  EXPECT_TRUE(cx::agent::LocalModelAdapter::IsLoopbackEndpoint(
      "http://127.0.0.1:11434/v1/chat/completions"));
  EXPECT_TRUE(cx::agent::LocalModelAdapter::IsLoopbackEndpoint(
      "https://[::1]:8080/v1/chat/completions"));
  EXPECT_FALSE(cx::agent::LocalModelAdapter::IsLoopbackEndpoint(
      "http://localhost:11434/v1/chat/completions"));
  EXPECT_FALSE(cx::agent::LocalModelAdapter::IsLoopbackEndpoint(
      "https://models.example/v1/chat/completions"));
  EXPECT_FALSE(cx::agent::LocalModelAdapter::IsLoopbackEndpoint(
      "file:///C:/model"));
  EXPECT_FALSE(cx::agent::LocalModelAdapter::IsLoopbackEndpoint(
      "http://user:password@127.0.0.1/v1/chat/completions"));
}

TEST(LocalModelAdapterTest, DisabledAndRemoteEndpointsNeverSend) {
  std::string response = "unchanged";
  cx::agent::LocalModelAdapter disabled(
      "http://127.0.0.1:1/v1/chat/completions", "local");
  EXPECT_FALSE(disabled.enabled());
  EXPECT_FALSE(disabled.Complete("hello", &response, 1ms));
  EXPECT_EQ(response, "unchanged");

  cx::agent::LocalModelAdapter remote(
      "https://models.example/v1/chat/completions", "model",
      cx::agent::LocalModelPolicy{true, true, false});
  EXPECT_FALSE(remote.enabled());
  EXPECT_FALSE(remote.uses_explicit_non_loopback_override());

  cx::agent::LocalModelAdapter acknowledged(
      "https://models.example/v1/chat/completions", "model",
      cx::agent::LocalModelPolicy{true, true, true});
  EXPECT_TRUE(acknowledged.enabled());
  EXPECT_TRUE(acknowledged.uses_explicit_non_loopback_override());
}

TEST(LocalModelAdapterTest, UnavailableLoopbackReturnsFailureWithoutFallback) {
  cx::agent::LocalModelAdapter adapter(
      "http://127.0.0.1:1/v1/chat/completions", "local",
      cx::agent::LocalModelPolicy{true, false, false});
  ASSERT_TRUE(adapter.enabled());
  std::string response;
  EXPECT_FALSE(adapter.Complete("hello", &response, 50ms));
  EXPECT_TRUE(response.empty());
}

TEST(ParallelTabSchedulerTest, DisabledSchedulerFailsClosed) {
  cx::agent::ParallelTabScheduler scheduler;
  EXPECT_FALSE(scheduler.enabled());
  EXPECT_FALSE(scheduler.AddOrUpdate(1, 0, 1s));
  EXPECT_FALSE(scheduler.Next().has_value());
}

TEST(ParallelTabSchedulerTest, BudgetsPauseResumeAndCountdown) {
  cx::agent::ParallelTabScheduler scheduler(true);
  ASSERT_TRUE(scheduler.AddOrUpdate(7, 2, 1000ms));
  ASSERT_TRUE(scheduler.Next().has_value());
  ASSERT_TRUE(scheduler.Charge(7, 250ms));
  EXPECT_EQ(scheduler.Get(7)->remaining, 750ms);
  EXPECT_TRUE(scheduler.Pause(7));
  EXPECT_FALSE(scheduler.Next().has_value());
  EXPECT_TRUE(scheduler.Resume(7));
  ASSERT_TRUE(scheduler.Charge(7, 800ms));
  EXPECT_EQ(scheduler.Get(7)->remaining, 0ms);
  EXPECT_TRUE(scheduler.Get(7)->paused);
  EXPECT_FALSE(scheduler.Resume(7));
}

TEST(ParallelTabSchedulerTest, WeightedRoundRobinRemainsFair) {
  cx::agent::ParallelTabScheduler scheduler(true);
  ASSERT_TRUE(scheduler.AddOrUpdate(1, 0, 10s));
  ASSERT_TRUE(scheduler.AddOrUpdate(2, 3, 10s));

  std::map<std::int64_t, int> selections;
  for (int i = 0; i < 50; ++i) {
    const auto task = scheduler.Next();
    ASSERT_TRUE(task.has_value());
    ++selections[task->tab_id];
  }
  EXPECT_EQ(selections[1], 10);
  EXPECT_EQ(selections[2], 40);
}

TEST(ParallelTabSchedulerTest, RejectsInvalidTasksAndSupportsRemoval) {
  cx::agent::ParallelTabScheduler scheduler(true);
  EXPECT_FALSE(scheduler.AddOrUpdate(0, 0, 1s));
  EXPECT_FALSE(scheduler.AddOrUpdate(1, -1, 1s));
  EXPECT_FALSE(scheduler.AddOrUpdate(1, 10, 1s));
  EXPECT_FALSE(scheduler.AddOrUpdate(1, 0, 0ms));
  ASSERT_TRUE(scheduler.AddOrUpdate(1, 0, 1s));
  EXPECT_TRUE(scheduler.Remove(1));
  EXPECT_FALSE(scheduler.Remove(1));
}
