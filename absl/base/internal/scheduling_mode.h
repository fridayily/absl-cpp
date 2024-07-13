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
//
// Core interfaces and definitions used by by low-level interfaces such as
// SpinLock.

#ifndef ABSL_BASE_INTERNAL_SCHEDULING_MODE_H_
#define ABSL_BASE_INTERNAL_SCHEDULING_MODE_H_

#include "absl/base/config.h"

namespace absl {
ABSL_NAMESPACE_BEGIN
namespace base_internal {

// Used to describe how a thread may be scheduled.  Typically associated with
// the declaration of a resource supporting synchronized access.
//
// SCHEDULE_COOPERATIVE_AND_KERNEL:
// Specifies that when waiting, a cooperative thread (e.g. a Fiber) may
// reschedule (using base::scheduling semantics); allowing other cooperative
// threads to proceed.
//
// SCHEDULE_KERNEL_ONLY: (Also described as "non-cooperative")
// Specifies that no cooperative scheduling semantics may be used, even if the
// current thread is itself cooperatively scheduled.  This means that
// cooperative threads will NOT allow other cooperative threads to execute in
// their place while waiting for a resource of this type.  Host operating system
// semantics (e.g. a futex) may still be used.
//
// When optional, clients should strongly prefer SCHEDULE_COOPERATIVE_AND_KERNEL
// by default.  SCHEDULE_KERNEL_ONLY should only be used for resources on which
// base::scheduling (e.g. the implementation of a Scheduler) may depend.
//
// NOTE: Cooperative resources may not be nested below non-cooperative ones.
// This means that it is invalid to to acquire a SCHEDULE_COOPERATIVE_AND_KERNEL
// resource if a SCHEDULE_KERNEL_ONLY resource is already held.

// 在多线程环境中，线程调度通常分为两种主要类型：
// 抢先式调度（Preemptive Scheduling）：
// 在这种模型中，操作系统可以在任何时候暂停当前线程，转而执行另一个就绪线程。
// 这通常是操作系统级别的行为，线程本身无法控制何时让出CPU。
// 合作式调度（Cooperative Scheduling）：
// 在合作调度中，线程必须自愿放弃执行权，才能让其他线程运行。
// 线程通过调用特定的函数（如yield或sleep）来表明它们愿意让出CPU。
// SCHEDULE_COOPERATIVE_AND_KERNEL结合了这两种策略，
//     意味着线程既可以是合作式的，也可以在必要时由操作系统进行抢占。这提供了灵活性，
//     可以平衡响应时间和资源利用率，特别是在需要精细控制线程执行顺序和优先级的场景下。

// 在SCHEDULE_KERNEL_ONLY模式下，线程等待资源时不会主动放弃执行权，而是由操作系统负责调度。
//     这可能会导致线程阻塞，直到资源可用或者被操作系统调度。
//     相比于SCHEDULE_COOPERATIVE_AND_KERNEL，这种策略可能提供更好的资源隔离和更确定的行为，
//     但可能会牺牲一些响应时间，因为线程的执行完全取决于操作系统的调度决策。
enum SchedulingMode {
  // non-cooperative
  SCHEDULE_KERNEL_ONLY = 0,         // Allow scheduling only the host OS.
  // Cooperative
  SCHEDULE_COOPERATIVE_AND_KERNEL,  // Also allow cooperative scheduling.
};

}  // namespace base_internal
ABSL_NAMESPACE_END
}  // namespace absl

#endif  // ABSL_BASE_INTERNAL_SCHEDULING_MODE_H_
