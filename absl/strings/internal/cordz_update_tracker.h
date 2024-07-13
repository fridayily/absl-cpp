// Copyright 2021 The Abseil Authors
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     https://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#ifndef ABSL_STRINGS_INTERNAL_CORDZ_UPDATE_TRACKER_H_
#define ABSL_STRINGS_INTERNAL_CORDZ_UPDATE_TRACKER_H_

#include <atomic>
#include <cstdint>

#include "absl/base/config.h"

namespace absl {
ABSL_NAMESPACE_BEGIN
namespace cord_internal {

// CordzUpdateTracker tracks counters for Cord update methods.
//
// The purpose of CordzUpdateTracker is to track the number of calls to methods
// updating Cord data for sampled cords. The class internally uses 'lossy'
// atomic operations: Cord is thread-compatible, so there is no need to
// synchronize updates. However, Cordz collection threads may call 'Value()' at
// any point, so the class needs to provide thread safe access.
//
// This class is thread-safe. But as per above comments, all non-const methods
// should be used single-threaded only: updates are thread-safe but lossy.
class CordzUpdateTracker {
 public:
  // Tracked update methods.
  enum MethodIdentifier {
    kUnknown,
    kAppendCord,
    kAppendCordBuffer,
    kAppendExternalMemory,
    kAppendString,
    kAssignCord,
    kAssignString,
    kClear,
    kConstructorCord,
    kConstructorString,
    kCordReader,
    kFlatten,
    kGetAppendBuffer,
    kGetAppendRegion,
    kMakeCordFromExternal,
    kMoveAppendCord,
    kMoveAssignCord,
    kMovePrependCord,
    kPrependCord,
    kPrependCordBuffer,
    kPrependString,
    kRemovePrefix,
    kRemoveSuffix,
    kSetExpectedChecksum,
    kSubCord,

    // kNumMethods defines the number of entries: must be the last entry.
    kNumMethods,
  };

  // Constructs a new instance. All counters are zero-initialized.
  constexpr CordzUpdateTracker() noexcept : values_{} {}

  // Copy constructs a new instance.
  CordzUpdateTracker(const CordzUpdateTracker& rhs) noexcept { *this = rhs; }

  // Assigns the provided value to this instance.
  CordzUpdateTracker& operator=(const CordzUpdateTracker& rhs) noexcept {
    for (int i = 0; i < kNumMethods; ++i) {
      values_[i].store(rhs.values_[i].load(std::memory_order_relaxed),
                       std::memory_order_relaxed);
    }
    return *this;
  }

  // Returns the value for the specified method.
  int64_t Value(MethodIdentifier method) const {
    return values_[method].load(std::memory_order_relaxed);
  }

  // Increases the value for the specified method by `n`
  // value.load(std::memory_order_relaxed)：以放松的内存排序方式读取value的当前值。
  //    这意味着其他线程可能不会立即看到这一变化，并且此操作与其他读写操作的顺序可以被重排。
  // value.store(..., std::memory_order_relaxed)：同样以放松的内存排序方式将结果
  // 存回value。与加载操作类似，此存储操作也可能与其他线程中的内存操作重排。
  // 读取时可能读取到的不是最新值，写入的值可能没有被其他线程看到。

  // 由于value.load和value.store方法都是原子操作，即使在单线程环境中，也能确保读取、修改、
  // 再存储这一系列动作不会被其他线程（或同一线程内的其他操作）打断，从而破坏数据的完整性。

  // 虽然这里使用了std::memory_order_relaxed，在多线程环境中可能引起数据竞争问题，
  // 但在单线程中，内存模型的约束并不影响操作的安全性。因为单线程环境中不存在数据竞争，
  // 所以放松的内存顺序在此情境下不影响操作的正确执行。
  void LossyAdd(MethodIdentifier method, int64_t n = 1) {
    auto& value = values_[method];
    value.store(value.load(std::memory_order_relaxed) + n,
                std::memory_order_relaxed);
  }

  // Adds all the values from `src` to this instance
  void LossyAdd(const CordzUpdateTracker& src) {
    for (int i = 0; i < kNumMethods; ++i) {
      MethodIdentifier method = static_cast<MethodIdentifier>(i);
      if (int64_t value = src.Value(method)) {
        LossyAdd(method, value);
      }
    }
  }

 private:

  //    std::atomic<int64_t> counter;
  //    该对象有 load 、store 、fetch_add、 exchange 等方法用于原子操作。

  // Until C++20 std::atomic is not constexpr default-constructible, so we need
  // a wrapper for this class to be constexpr constructible.

  class Counter : public std::atomic<int64_t> {
   public:
    constexpr Counter() noexcept : std::atomic<int64_t>(0) {}
  };

  // values_数组实际上是kNumMethods个Counter对象的集合，每个对象都是线程安全的，可以独立进行原子的加减等操作
  Counter values_[kNumMethods];
};

}  // namespace cord_internal
ABSL_NAMESPACE_END
}  // namespace absl

#endif  // ABSL_STRINGS_INTERNAL_CORDZ_UPDATE_TRACKER_H_
