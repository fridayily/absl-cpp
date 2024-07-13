// Copyright 2019 The Abseil Authors.
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
#include "absl/strings/internal/cordz_handle.h"

#include <random>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/memory/memory.h"
#include "absl/synchronization/internal/thread_pool.h"
#include "absl/synchronization/notification.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"

namespace absl {
ABSL_NAMESPACE_BEGIN
namespace cord_internal {
namespace {

using ::testing::ElementsAre;
using ::testing::Gt;
using ::testing::IsEmpty;
using ::testing::SizeIs;

// Local less verbose helper
std::vector<const CordzHandle*> DeleteQueue() {
  return CordzHandle::DiagnosticsGetDeleteQueue();
}
// 先构造基类，再构造派生类
struct CordzHandleDeleteTracker : public CordzHandle {
  bool* deleted;
  explicit CordzHandleDeleteTracker(bool* deleted) : deleted(deleted) {}
  ~CordzHandleDeleteTracker() override { *deleted = true; }
};

TEST(CordzHandleTest, DeleteQueueIsEmpty) {
  EXPECT_THAT(DeleteQueue(), SizeIs(0));
}

TEST(CordzHandleTest, CordzHandleCreateDelete) {
  bool deleted = false;
  auto* handle = new CordzHandleDeleteTracker(&deleted);
  EXPECT_FALSE(handle->is_snapshot());
  EXPECT_TRUE(handle->SafeToDelete());
  EXPECT_THAT(DeleteQueue(), SizeIs(0));

  CordzHandle::Delete(handle);
  EXPECT_THAT(DeleteQueue(), SizeIs(0));
  EXPECT_TRUE(deleted);
}

TEST(CordzHandleTest, CordzSnapshotCreateDelete) {
  auto* snapshot = new CordzSnapshot();
  EXPECT_TRUE(snapshot->is_snapshot());
  EXPECT_TRUE(snapshot->SafeToDelete());
  EXPECT_THAT(DeleteQueue(), ElementsAre(snapshot));
  delete snapshot;
  EXPECT_THAT(DeleteQueue(), SizeIs(0));
}

TEST(CordzHandleTest, CordzHandleCreateDeleteWithSnapshot) {
  bool deleted = false;
  auto* snapshot = new CordzSnapshot();
  auto* handle = new CordzHandleDeleteTracker(&deleted);
  EXPECT_FALSE(handle->SafeToDelete());

  CordzHandle::Delete(handle);
  EXPECT_THAT(DeleteQueue(), ElementsAre(handle, snapshot));
  EXPECT_FALSE(deleted);
  EXPECT_FALSE(handle->SafeToDelete());

  // 会调用析构函数，删除队列
  delete snapshot;
  EXPECT_THAT(DeleteQueue(), SizeIs(0));
  EXPECT_TRUE(deleted);
}

TEST(CordzHandleTest, MultiSnapshot) {
  bool deleted[3] = {false, false, false};

  CordzSnapshot* snapshot[3];
  CordzHandleDeleteTracker* handle[3];
  for (int i = 0; i < 3; ++i) {
    // new 调用 operator new(size_t) 分配空间
    // 一旦内存分配成功，编译器会在这块内存上构造一个新的T类型的对象
    snapshot[i] = new CordzSnapshot();
    handle[i] = new CordzHandleDeleteTracker(&deleted[i]);
    // 要删除的handle 不是 快照，则插入队列尾部，没有真的删除
    CordzHandle::Delete(handle[i]);
  }
  // 上面代码插入顺序为 snapshot[0] handle[0] snapshot[1] handle[1] snapshot[2] handle[2]

  EXPECT_THAT(DeleteQueue(), ElementsAre(handle[2], snapshot[2], handle[1],
                                         snapshot[1], handle[0], snapshot[0]));
  EXPECT_THAT(deleted, ElementsAre(false, false, false));

  // delete 检查这个指针是否为nullptr。如果不为nullptr，编译器会调用该对象的析构函数
  // 在析构函数执行完毕后，C++会调用内存释放函数，通常是operator delete(void*)，
  // 来释放之前由new分配的内存空间
  // // 先调用派生类析构函数，再调用基类析构函数
  delete snapshot[1];

  //基类析构函数中是从 queue 的中间删除该节点，没有释放内存的操作，释放内存操作是delete 进行的
  EXPECT_THAT(DeleteQueue(), ElementsAre(handle[2], snapshot[2], handle[1],
                                         handle[0], snapshot[0]));
  EXPECT_THAT(deleted, ElementsAre(false, false, false));

  delete snapshot[0];
  EXPECT_THAT(DeleteQueue(), ElementsAre(handle[2], snapshot[2]));
  EXPECT_THAT(deleted, ElementsAre(true, true, false));

  delete snapshot[2];
  EXPECT_THAT(DeleteQueue(), SizeIs(0));
  EXPECT_THAT(deleted, ElementsAre(true, true, deleted));
}

TEST(CordzHandleTest, DiagnosticsHandleIsSafeToInspect) {
  CordzSnapshot snapshot1;
  EXPECT_TRUE(snapshot1.DiagnosticsHandleIsSafeToInspect(nullptr));

  auto* handle1 = new CordzHandle();
  // 不是快照不会加入全局队列
  // 此时 全局队列 只有快照，没有 handle1 ,返回 true
  EXPECT_TRUE(snapshot1.DiagnosticsHandleIsSafeToInspect(handle1));

  CordzHandle::Delete(handle1); // 不能安全删除，会放入全局队列中
  EXPECT_TRUE(snapshot1.DiagnosticsHandleIsSafeToInspect(handle1));

  CordzSnapshot snapshot2;
  auto* handle2 = new CordzHandle();
  // 队列顺序 snapshot1 handle1 snapshot2 handle2
  EXPECT_TRUE(snapshot1.DiagnosticsHandleIsSafeToInspect(handle1));
  EXPECT_TRUE(snapshot1.DiagnosticsHandleIsSafeToInspect(handle2));

  // 为 FALSE 是因为 handle1 在 snapshot2 前面
  EXPECT_FALSE(snapshot2.DiagnosticsHandleIsSafeToInspect(handle1));
  EXPECT_TRUE(snapshot2.DiagnosticsHandleIsSafeToInspect(handle2));

  CordzHandle::Delete(handle2);
  EXPECT_TRUE(snapshot1.DiagnosticsHandleIsSafeToInspect(handle1));
}

TEST(CordzHandleTest, DiagnosticsGetSafeToInspectDeletedHandles) {
  EXPECT_THAT(DeleteQueue(), IsEmpty());

  auto* handle = new CordzHandle();
  auto* snapshot1 = new CordzSnapshot();

  // snapshot1 should be able to see handle.
  EXPECT_THAT(DeleteQueue(), ElementsAre(snapshot1));
  EXPECT_TRUE(snapshot1->DiagnosticsHandleIsSafeToInspect(handle));
  EXPECT_THAT(snapshot1->DiagnosticsGetSafeToInspectDeletedHandles(),
              IsEmpty());

  // This handle will be safe to inspect as long as snapshot1 is alive. However,
  // since only snapshot1 can prove that it's alive, it will be hidden from
  // snapshot2.
  CordzHandle::Delete(handle);

  // This snapshot shouldn't be able to see handle because handle was already
  // sent to Delete.
  auto* snapshot2 = new CordzSnapshot();

  // DeleteQueue elements are LIFO order.
  // 插入顺序是 snapshot1 handle snapshot2
  // 队列顺序是 snapshot2 handle snapshot1
  EXPECT_THAT(DeleteQueue(), ElementsAre(snapshot2, handle, snapshot1));

  EXPECT_TRUE(snapshot1->DiagnosticsHandleIsSafeToInspect(handle));
  EXPECT_FALSE(snapshot2->DiagnosticsHandleIsSafeToInspect(handle));

  EXPECT_THAT(snapshot1->DiagnosticsGetSafeToInspectDeletedHandles(),
              ElementsAre(handle));
  EXPECT_THAT(snapshot2->DiagnosticsGetSafeToInspectDeletedHandles(),
              IsEmpty());

  CordzHandle::Delete(snapshot1);
  EXPECT_THAT(DeleteQueue(), ElementsAre(snapshot2));

  // 如果是快照，直接删除
  CordzHandle::Delete(snapshot2);
  EXPECT_THAT(DeleteQueue(), IsEmpty());
}

// Create and delete CordzHandle and CordzSnapshot objects in multiple threads
// so that tsan has some time to chew on it and look for memory problems.
TEST(CordzHandleTest, MultiThreaded) {
  Notification stop;
  static constexpr int kNumThreads = 4;
  // Keep the number of handles relatively small so that the test will naturally
  // transition to an empty delete queue during the test. If there are, say, 100
  // handles, that will virtually never happen. With 10 handles and around 50k
  // iterations in each of 4 threads, the delete queue appears to become empty
  // around 200 times.
  static constexpr int kNumHandles = 10;

  // Each thread is going to pick a random index and atomically swap its
  // CordzHandle with one in handles. This way, each thread can avoid
  // manipulating a CordzHandle that might be operated upon in another thread.
  // 定义了一个名为handles的std::vector容器，其中包含kNumHandles个元素，
  // 每个元素都是一个std::atomic<CordzHandle*>类型
  std::vector<std::atomic<CordzHandle*>> handles(kNumHandles);

  // global bool which is set when any thread did get some 'safe to inspect'
  // handles. On some platforms and OSS tests, we might risk that some pool
  // threads are starved, stalled, or just got a few unlikely random 'handle'
  // coin tosses, so we satisfy this test with simply observing 'some' thread
  // did something meaningful, which should minimize the potential for flakes.
  std::atomic<bool> found_safe_to_inspect(false);

  {
    absl::synchronization_internal::ThreadPool pool(kNumThreads);
    for (int i = 0; i < kNumThreads; ++i) {
      // 每次循环都将一个匿名lambda函数提交给线程池执行。
      // Lambda捕获了外部的stop、handles和found_safe_to_inspect变量
      pool.Schedule([&stop, &handles, &found_safe_to_inspect]() {
        std::minstd_rand gen;
        // 创建 0 2 之间的均匀分布
        std::uniform_int_distribution<int> dist_type(0, 2);
        std::uniform_int_distribution<int> dist_handle(0, kNumHandles - 1);

        // 没有被通知则执行 while 代码
        while (!stop.HasBeenNotified()) {
          CordzHandle* handle;
          switch (dist_type(gen)) {
            case 0:
              handle = new CordzHandle();
              break;
            case 1:
              handle = new CordzSnapshot();
              break;
            default:
              handle = nullptr;
              break;
          }
          // 通过原子交换操作更新handles数组中的句柄，
          // 同时获取旧句柄。接着，对旧句柄执行安全检查和删除操作。

          // exchange方法原子性地将原子变量的当前值与给定的新值进行交换，并返回交换前的原始值。
          CordzHandle* old_handle = handles[dist_handle(gen)].exchange(handle);
          if (old_handle != nullptr) {
            std::vector<const CordzHandle*> safe_to_inspect =
                old_handle->DiagnosticsGetSafeToInspectDeletedHandles();
            for (const CordzHandle* handle : safe_to_inspect) {
              // We're in a tight loop, so don't generate too many error
              // messages.
              // handle 里的都不是快照
              ASSERT_FALSE(handle->is_snapshot());
            }
            if (!safe_to_inspect.empty()) {
              found_safe_to_inspect.store(true);
            }
            // 是快照直接删除
            // 不是快照，加入删除队列
            CordzHandle::Delete(old_handle);
          }
        }

        // 确保线程池中的每个线程在结束前尝试清理分配的资源，以避免内存泄漏和资源占用。
        // 某个线程将会是最后一个到达此清理代码的线程
        // 因为它在执行清理操作时，可以确保没有其他线程会继续创建新的资源
        // Have each thread attempt to clean up everything. Some thread will be
        // the last to reach this cleanup code, and it will be guaranteed to
        // clean up everything because nothing remains to create new handles.
        for (auto& h : handles) {
          // 如果h原本不为nullptr（即存在有效的CordzHandle），
          // 则handle将获得这个旧的句柄值，条件判断为真。
          // 将handles中的某个位置的指针设置为nullptr

          // 一是原子性地标记当前处理的资源为待清理状态（通过置为nullptr），
          // 二是安全地获取待清理资源的原始指针以便进行后续的删除操作，
          // 整个过程在多线程环境下是安全的。
          if (CordzHandle* handle = h.exchange(nullptr)) {
            CordzHandle::Delete(handle);
          }
        }
      });
    }

    // The threads will hammer away.  Give it a little bit of time for tsan to
    // spot errors.
    // 它使当前执行线程暂停执行指定的时间，这里是3秒。暂停线程的目的是给予分析工具足够的
    // 时间来检测和报告可能存在的并发问题。在多线程应用中，如果存在竞态条件等并发错误，
    // 它们可能不会立即显现，而是需要一定的时间和执行路径组合才会触发。
    // 因此，人为增加延迟有时能帮助诊断工具捕捉到这些问题。
    absl::SleepFor(absl::Seconds(3));
    stop.Notify();
  }

  // Confirm that the test did *something*. This check will be satisfied as
  // long as any thread has deleted a CordzSnapshot object and a non-snapshot
  // CordzHandle was deleted after the CordzSnapshot was created.
  // See also comments on `found_safe_to_inspect`
  EXPECT_TRUE(found_safe_to_inspect.load());
}

}  // namespace
}  // namespace cord_internal
ABSL_NAMESPACE_END
}  // namespace absl
