// Copyright 2017 The Abseil Authors.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      https://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#ifndef ABSL_SYNCHRONIZATION_INTERNAL_THREAD_POOL_H_
#define ABSL_SYNCHRONIZATION_INTERNAL_THREAD_POOL_H_

#include <cassert>
#include <cstddef>
#include <functional>
#include <queue>
#include <thread>  // NOLINT(build/c++11)
#include <utility>
#include <vector>

#include "absl/base/thread_annotations.h"
#include "absl/functional/any_invocable.h"
#include "absl/synchronization/mutex.h"

namespace absl {
ABSL_NAMESPACE_BEGIN
namespace synchronization_internal {

// A simple ThreadPool implementation for tests.
class ThreadPool {
 public:
  explicit ThreadPool(int num_threads) {
    threads_.reserve(num_threads);
    for (int i = 0; i < num_threads; ++i) {
      //  this关键字在类的成员函数中代表当前对象的指针。在这里，
      //  this指针被传递给std::thread构造函数，
      //  表明新创建的线程将使用当前ThreadPool对象来调用WorkLoop函数
      threads_.push_back(std::thread(&ThreadPool::WorkLoop, this));
    }
  }

  ThreadPool(const ThreadPool &) = delete;
  ThreadPool &operator=(const ThreadPool &) = delete;

  ~ThreadPool() {
    {
      absl::MutexLock l(&mu_);
      for (size_t i = 0; i < threads_.size(); i++) {
        queue_.push(nullptr);  // Shutdown signal.
      }
    }
    for (auto &t : threads_) {
      t.join();
    }
  }

  // Schedule a function to be run on a ThreadPool thread immediately.
  void Schedule(absl::AnyInvocable<void()> func) {
    assert(func != nullptr);
    absl::MutexLock l(&mu_);
    queue_.push(std::move(func));
  }

 private:
  bool WorkAvailable() const ABSL_EXCLUSIVE_LOCKS_REQUIRED(mu_) {
    return !queue_.empty();
  }

  void WorkLoop() {
    while (true) {
      // void()表示这个AnyInvocable对象存储的可调用对象不接受任何参数，
      // 并且调用时不返回任何值
      absl::AnyInvocable<void()> func;
      {
        absl::MutexLock l(&mu_);
        // this 这允许条件函数访问ThreadPool类的成员变量和方法。
        // 第二个参数 (&ThreadPool::WorkAvailable): 是一个指向成员函数的指针
        // 类似条件变量的await函数，用于等待条件变量变为真。
        mu_.Await(absl::Condition(this, &ThreadPool::WorkAvailable));
        // 去除函数
        func = std::move(queue_.front());
        queue_.pop();
      }
      if (func == nullptr) {  // Shutdown signal.
        break;
      }
      func();
    }
  }

  absl::Mutex mu_;
  std::queue<absl::AnyInvocable<void()>> queue_ ABSL_GUARDED_BY(mu_);
  std::vector<std::thread> threads_;
};

}  // namespace synchronization_internal
ABSL_NAMESPACE_END
}  // namespace absl

#endif  // ABSL_SYNCHRONIZATION_INTERNAL_THREAD_POOL_H_
