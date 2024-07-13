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

#ifndef ABSL_STRINGS_INTERNAL_CORDZ_HANDLE_H_
#define ABSL_STRINGS_INTERNAL_CORDZ_HANDLE_H_

#include <atomic>
#include <vector>

#include "absl/base/config.h"
#include "absl/base/internal/raw_logging.h"

namespace absl {
ABSL_NAMESPACE_BEGIN
namespace cord_internal {

// This base class allows multiple types of object (CordzInfo and
// CordzSampleToken) to exist simultaneously on the delete queue (pointed to by
// global_dq_tail and traversed using dq_prev_ and dq_next_). The
// delete queue guarantees that once a profiler creates a CordzSampleToken and
// has gained visibility into a CordzInfo object, that CordzInfo object will not
// be deleted prematurely. This allows the profiler to inspect all CordzInfo
// objects that are alive without needing to hold a global lock.

// 基类使得不同类型的对象（ CordzInfo 和 CordzSampleToken ）能够同时存在于同一个删除队列中。
// 删除队列通过全局指针 global_dq_tail 指向队尾，
// 并利用对象内的指针 dq_prev _和 dq_next_ 来遍历队列中的元素。

// 一旦分析器（profiler）创建了CordzSampleToken并观察到（获得访问权限）某个CordzInfo对象，
// 该 CordzInfo 对象将不会被提前删除。这意味着，只要分析器对 CordzSampleToken 有引用，
// 对应的 CordzInfo 就会被保护起来，防止在分析期间意外释放。
// 这样的设计使得分析器能够在无需持有全局锁的情况下，安全地检查所有当前存活的CordzInfo对象。
// 这对于性能监控和分析特别重要，因为它减少了锁竞争，提高了多线程环境下的执行效率和可伸缩性。
class ABSL_DLL CordzHandle {
 public:
  CordzHandle() : CordzHandle(false) {}

  bool is_snapshot() const { return is_snapshot_; }

  // Returns true if this instance is safe to be deleted because it is either a
  // snapshot, which is always safe to delete, or not included in the global
  // delete queue and thus not included in any snapshot.
  // Callers are responsible for making sure this instance can not be newly
  // discovered by other threads. For example, CordzInfo instances first de-list
  // themselves from the global CordzInfo list before determining if they are
  // safe to be deleted directly.
  // If SafeToDelete returns false, callers MUST use the Delete() method to
  // safely queue CordzHandle instances for deletion.
  bool SafeToDelete() const;

  // 如条件允许，它会直接删除 handle 引用的实例。
  // 否则，它会将实例加入到删除队列中，延迟实际的删除操作，
  // 直到所有可能引用该 CordzHandle的 “样本令牌”（可能是快照实例）都被清除。
  // Deletes the provided instance, or puts it on the delete queue to be deleted
  // once there are no more sample tokens (snapshot) instances potentially
  // referencing the instance. `handle` should not be null.
  static void Delete(CordzHandle* handle);

  // Returns the current entries in the delete queue in LIFO order.
   // 此静态成员函数用于获取当前删除队列中的所有条目，
  //  并以后进先出（Last In, First Out, LIFO）的顺序返回这些条目
  // 函数名称中的Diagnostics暗示这个功能可能是为了诊断或监控目的而设计的，
  // 帮助开发者或系统管理员了解当前待删除资源的状态。
  static std::vector<const CordzHandle*> DiagnosticsGetDeleteQueue();

  // Returns true if the provided handle is nullptr or guarded by this handle.
  // Since the CordzSnapshot token is itself a CordzHandle, this method will
  // allow tests to check if that token is keeping an arbitrary CordzHandle
  // alive.
  // 如果 handle 为 nullptr 或者被当前的句柄保护，则返回true，
  // 表明该句柄可以安全地进行检查或访问，否则返回false。
  // 在并发和资源管理严格的系统中，此函数可以帮助开发者确保在访问或操作一个CordzHandle之前，
  // 该句柄是有效的且不会引发未定义行为或并发问题，尤其是在进行系统诊断、测试或调试期间。
  bool DiagnosticsHandleIsSafeToInspect(const CordzHandle* handle) const;

  // Returns the current entries in the delete queue, in LIFO order, that are
  // protected by this. CordzHandle objects are only placed on the delete queue
  // after CordzHandle::Delete is called with them as an argument. Only
  // CordzHandle objects that are not also CordzSnapshot objects will be
  // included in the return vector. For each of the handles in the return
  // vector, the earliest that their memory can be freed is when this
  // CordzSnapshot object is deleted.
  std::vector<const CordzHandle*> DiagnosticsGetSafeToInspectDeletedHandles();

  // 将其设置为受保护意味着它不能在类的外部直接被调用，但子类可以访问和使用这个构造函数。
 protected:
  explicit CordzHandle(bool is_snapshot);

  // 虚析构函数的使用确保了当通过基类指针删除派生类对象时，能够正确调用派生类的析构函数，
  // 从而正确释放派生类特有的资源。将其声明为受保护意味着只有类本身及其子类
  // 可以在其作用域内直接删除该类的对象，外部用户则不能直接操作
  virtual ~CordzHandle();

 private:
  const bool is_snapshot_;

  // dq_prev_ and dq_next_ require the global queue mutex to be held.
  // Unfortunately we can't use thread annotations such that the thread safety
  // analysis understands that queue_ and global_queue_ are one and the same.

  // 我们不能使用线程注解（thread annotations）使线程安全性分析工具理解
  // queue_和global_queue_实际上是同一个 。这意味着虽然在代码层面
  // queue_和global_queue_概念上是同一队列，但由于技术限制或工具理解能力的局限，
  // 无法直接通过注解告知静态分析工具这一点，可能导致分析结果不够精确，
  // 无法自动验证所有涉及它们的线程安全操作
  CordzHandle* dq_prev_  = nullptr;
  CordzHandle* dq_next_ = nullptr;
};

class CordzSnapshot : public CordzHandle {
 public:
  CordzSnapshot() : CordzHandle(true) {}
};

}  // namespace cord_internal
ABSL_NAMESPACE_END
}  // namespace absl

#endif  // ABSL_STRINGS_INTERNAL_CORDZ_HANDLE_H_
