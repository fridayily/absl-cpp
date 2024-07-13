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

#include <atomic>

#include "absl/base/internal/raw_logging.h"  // For ABSL_RAW_CHECK
#include "absl/base/no_destructor.h"
#include "absl/synchronization/mutex.h"

namespace absl {
ABSL_NAMESPACE_BEGIN
namespace cord_internal {

namespace {

struct Queue {
  Queue() = default;

  absl::Mutex mutex;
  // 用原子变量修饰一个指针，并初始化为 nullptr
  std::atomic<CordzHandle*> dq_tail ABSL_GUARDED_BY(mutex){nullptr};

  // Returns true if this delete queue is empty. This method does not acquire
  // the lock, but does a 'load acquire' observation on the delete queue tail.
  // It is used inside Delete() to check for the presence of a delete queue
  // without holding the lock. The assumption is that the caller is in the
  // state of 'being deleted', and can not be newly discovered by a concurrent
  // 'being constructed' snapshot instance. Practically, this means that any
  // such discovery (`find`, 'first' or 'next', etc) must have proper 'happens
  // before / after' semantics and atomic fences.
  bool IsEmpty() const ABSL_NO_THREAD_SAFETY_ANALYSIS {
    return dq_tail.load(std::memory_order_acquire) == nullptr;
  }
};

// absl::NoDestructor保证避免了静态对象销毁顺序的问题，
// 因为Queue的析构函数不会被显式调用；相反，资源通过内部管理或进程结束时隐式释放
static Queue& GlobalQueue() {
  static absl::NoDestructor<Queue> global_queue;
  return *global_queue;
}

}  // namespace

// 向 queue 的尾部插入节点
CordzHandle::CordzHandle(bool is_snapshot) : is_snapshot_(is_snapshot) {
  Queue& global_queue = GlobalQueue();
  if (is_snapshot) {
    MutexLock lock(&global_queue.mutex);
    CordzHandle* dq_tail = global_queue.dq_tail.load(std::memory_order_acquire);
    if (dq_tail != nullptr) {
      // 将当前 CordzHandle 插入到队列的尾部
      // 当前节点的 dq_prev_ 指向原来的尾节点
      // 将当前尾节点的 dq_next_ 指向当前节点
      dq_prev_ = dq_tail;
      dq_tail->dq_next_ = this;
    }
    // 更新尾节点
    global_queue.dq_tail.store(this, std::memory_order_release);
  }
}

CordzHandle::~CordzHandle() {
  Queue& global_queue = GlobalQueue();
  if (is_snapshot_) {
    std::vector<CordzHandle*> to_delete;
    {
      MutexLock lock(&global_queue.mutex);
      // 脱离代码块后自动释放锁
      CordzHandle* next = dq_next_;
      if (dq_prev_ == nullptr) {
        // 当前节点的前向节点指向 nullptr, 则说明当前节点是队列的头部
        // We were head of the queue, delete every CordzHandle until we reach
        // either the end of the list, or a snapshot handle.
        // 如果当前销毁的CordzHandle是队列的头部（dq_prev_ == nullptr），
        // 则遍历队列，将所有非快照的CordzHandle添加到to_delete向量中，

        // 下一个节点非空且不是快照，则添加到删除队列
        while (next && !next->is_snapshot_) {
          to_delete.push_back(next);
          next = next->dq_next_;
        }
      } else {
        // Another CordzHandle existed before this one, don't delete anything.
        dq_prev_->dq_next_ = next; // 删除 中间的节点，即自己
      }
      // next 非空说明 this 非空，this 可以被删除
      if (next) {
        // 配合 dq_prev_->dq_next_ = next;  删除中间的节点，即自身
        // 如果 this 在队首，会先按照上面 while 循环将要删除的节点放入删除队列
        // 于此同时会更新 next 节点
        next->dq_prev_ = dq_prev_;
      } else {
        // 如果 next 为空，this 节点要被删除，所以更新尾节点尾 this 的上一个节点
        global_queue.dq_tail.store(dq_prev_, std::memory_order_release);
      }
    }
    for (CordzHandle* handle : to_delete) {
      delete handle;
    }
  }
}

bool CordzHandle::SafeToDelete() const {
  // 首先检查成员变量is_snapshot_。如果该对象标记为快照(is_snapshot_为true)，
  // 则认为它是安全的可被删除的
  // 如果全局队列为空，表明没有其他活动的引用或依赖，因此当前对象也是安全的可被删除。
  return is_snapshot_ || GlobalQueue().IsEmpty();
}

void CordzHandle::Delete(CordzHandle* handle) {
  assert(handle);
  if (handle) {
    Queue& queue = GlobalQueue();
    if (!handle->SafeToDelete()) {
      // 不能安全删除，即如果当前对象不是快照，并且全局队列不为空，则将 handle 节点插入到队列的尾部
      MutexLock lock(&queue.mutex);
      CordzHandle* dq_tail = queue.dq_tail.load(std::memory_order_acquire);
      if (dq_tail != nullptr) {
        handle->dq_prev_ = dq_tail;
        dq_tail->dq_next_ = handle;  // 将 handle 节点插入到队列的尾部
        queue.dq_tail.store(handle, std::memory_order_release);
        return;
      }
    }
    // 如果删除的对象是快照或者队列为空，则直接删除该对象
    // 会先调用析构函数，再释放空间
    delete handle;
  }
}

std::vector<const CordzHandle*> CordzHandle::DiagnosticsGetDeleteQueue() {
  std::vector<const CordzHandle*> handles;
  Queue& global_queue = GlobalQueue();
  MutexLock lock(&global_queue.mutex);
  // 从队列的尾部开始迭代
  // 所以是先进后出
  CordzHandle* dq_tail = global_queue.dq_tail.load(std::memory_order_acquire);
  for (const CordzHandle* p = dq_tail; p; p = p->dq_prev_) {
    handles.push_back(p);
  }
  return handles;
}

bool CordzHandle::DiagnosticsHandleIsSafeToInspect(
    const CordzHandle* handle) const {
  // 调用者不是快照不可以安全检查
  if (!is_snapshot_) return false;
  if (handle == nullptr) return true;
  // handle 是快照，则不能安全检查
  if (handle->is_snapshot_) return false;
  bool snapshot_found = false;
  Queue& global_queue = GlobalQueue();
  MutexLock lock(&global_queue.mutex);
  for (const CordzHandle* p = global_queue.dq_tail; p; p = p->dq_prev_) {
    // 从尾部开始查找，先碰到 handle，就会返回 true
    if (p == handle) return !snapshot_found;
    if (p == this) snapshot_found = true;
  }
  ABSL_ASSERT(snapshot_found);  // Assert that 'this' is in delete queue.
  return true;
}

std::vector<const CordzHandle*>
CordzHandle::DiagnosticsGetSafeToInspectDeletedHandles() {
  std::vector<const CordzHandle*> handles;
  if (!is_snapshot()) {
    // 调用者不是快照，返回空的 handles，说明没有可以安全删除的 handles
    return handles;
  }

  // 调用者是快照
  Queue& global_queue = GlobalQueue();
  MutexLock lock(&global_queue.mutex);
  for (const CordzHandle* p = dq_next_; p != nullptr; p = p->dq_next_) {
    if (!p->is_snapshot()) {
      // 不是快照的就放入 handles容器中
      handles.push_back(p);
    }
  }
  return handles;
}

}  // namespace cord_internal
ABSL_NAMESPACE_END
}  // namespace absl
