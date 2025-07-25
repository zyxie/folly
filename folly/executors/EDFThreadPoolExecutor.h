/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#pragma once

#include <atomic>
#include <cstddef>
#include <limits>
#include <memory>
#include <vector>

#include <folly/executors/SoftRealTimeExecutor.h>
#include <folly/executors/ThreadPoolExecutor.h>

namespace folly {

class EDFThreadPoolSemaphore {
 public:
  virtual ~EDFThreadPoolSemaphore() = default;
  virtual void post(uint32_t value) = 0;
  virtual void wait() = 0;
};

template <class Semaphore>
class EDFThreadPoolSemaphoreImpl : public EDFThreadPoolSemaphore {
 public:
  template <class... Args>
  explicit EDFThreadPoolSemaphoreImpl(Args&&... args)
      : sem_(std::forward<Args>(args)...) {}

  void post(uint32_t value) override { sem_.post(value); }
  void wait() override { sem_.wait(); }

 private:
  Semaphore sem_;
};

/**
 * `EDFThreadPoolExecutor` is a `SoftRealTimeExecutor` that implements the
 * earliest-deadline-first scheduling policy. Deadline ties are resolved by
 * submission order.
 */
class EDFThreadPoolExecutor
    : public SoftRealTimeExecutor,
      public ThreadPoolExecutor {
 public:
  class Task;
  class TaskQueue;

  static constexpr uint64_t kEarliestDeadline = 0;
  static constexpr uint64_t kLatestDeadline =
      std::numeric_limits<uint64_t>::max();

  static std::unique_ptr<EDFThreadPoolSemaphore> makeDefaultSemaphore();
  static std::unique_ptr<EDFThreadPoolSemaphore> makeLifoSemSemaphore();
  static std::unique_ptr<EDFThreadPoolSemaphore> makeThrottledLifoSemSemaphore(
      std::chrono::nanoseconds wakeUpInterval = {});

  explicit EDFThreadPoolExecutor(
      std::size_t numThreads,
      std::shared_ptr<ThreadFactory> threadFactory =
          std::make_shared<NamedThreadFactory>("EDFThreadPool"),
      std::unique_ptr<EDFThreadPoolSemaphore> semaphore =
          makeDefaultSemaphore());

  ~EDFThreadPoolExecutor() override;

  using ThreadPoolExecutor::add;

  void add(Func f) override;
  void add(Func f, uint64_t deadline) override;
  void add(std::vector<Func> fs, uint64_t deadline) override;

  size_t getTaskQueueSize() const;

 protected:
  void threadRun(ThreadPtr thread) override;
  void stopThreads(std::size_t numThreads) override;
  std::size_t getPendingTaskCountImpl() const override final;

 private:
  bool shouldStop();
  std::shared_ptr<Task> take();

  void fillTaskInfo(const Task& task, TaskInfo& info);
  void registerTaskEnqueue(const Task& task);

  std::unique_ptr<TaskQueue> taskQueue_;
  std::unique_ptr<EDFThreadPoolSemaphore> sem_;
  std::atomic<int> threadsToStop_{0};

  // All operations performed on `numIdleThreads_` explicitly specify memory
  // ordering of `std::memory_order_seq_cst`. This is due to `numIdleThreads_`
  // performing Dekker's algorithm with `numItems` prior to consumer threads
  // (workers) wait on `sem_`.
  std::atomic<std::size_t> numIdleThreads_{0};
};

} // namespace folly
