// Copyright 2023 The Abseil Authors.
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

#ifndef ABSL_BASE_INTERNAL_NULLABILITY_IMPL_H_
#define ABSL_BASE_INTERNAL_NULLABILITY_IMPL_H_

#include <memory>
#include <type_traits>

#include "absl/base/attributes.h"
#include "absl/meta/type_traits.h"

namespace absl {

namespace nullability_internal {

// `IsNullabilityCompatible` checks whether its first argument is a class
// explicitly tagged as supporting nullability annotations. The tag is the type
// declaration `absl_nullability_compatible`.
template <typename, typename = void>
struct IsNullabilityCompatible : std::false_type {};

// 这是一个特化版本的模板，它试图检测类型T是否定义了一个名为
// absl_nullability_compatible的嵌套类型
template <typename T>
struct IsNullabilityCompatible<
    T, absl::void_t<typename T::absl_nullability_compatible>> : std::true_type {
};

// 如果类型T拥有absl_nullability_compatible成员类型，
// 则IsNullabilityCompatible<T>会被特化为继承自std::true_type，其value成员为true
template <typename T>
constexpr bool IsSupportedType = IsNullabilityCompatible<T>::value;

// 对于任何类型的指针T*，该模板变量都将认为它是受支持的类型
template <typename T>
constexpr bool IsSupportedType<T*> = true;

// 无论T是什么类型，只要它是指向U的成员的指针，这个组合就被认为是受支持的类型
template <typename T, typename U>
constexpr bool IsSupportedType<T U::*> = true;

// 无论T是什么类型，以及Deleter是哪些 deleter 函数或函数对象，
// std::unique_ptr<T, Deleter...>都被认为是支持的类型
template <typename T, typename... Deleter>
constexpr bool IsSupportedType<std::unique_ptr<T, Deleter...>> = true;

template <typename T>
constexpr bool IsSupportedType<std::shared_ptr<T>> = true;

// 确保传递给模板参数T的类型是一个原始指针（raw pointer）或者是被支持的智能指针类型
// std::remove_cv_t<T>: 这是一个类型修饰符，用于去除类型T的cv限定符（const和volatile），
// 确保在检查类型支持性时不受这些限定符的影响。
// 这里不同的 T 会调用不同的 IsSupportedType
// 如果 T 是原始指针/智能指针，返回 true
// 如果 T 没有 absl_nullability_compatible 返回 false
template <typename T>
struct EnableNullable {
  static_assert(nullability_internal::IsSupportedType<std::remove_cv_t<T>>,
                "Template argument must be a raw or supported smart pointer "
                "type. See absl/base/nullability.h.");
  using type = T;
};

template <typename T>
struct EnableNonnull {
  static_assert(nullability_internal::IsSupportedType<std::remove_cv_t<T>>,
                "Template argument must be a raw or supported smart pointer "
                "type. See absl/base/nullability.h.");
  using type = T;
};

template <typename T>
struct EnableNullabilityUnknown {
  static_assert(nullability_internal::IsSupportedType<std::remove_cv_t<T>>,
                "Template argument must be a raw or supported smart pointer "
                "type. See absl/base/nullability.h.");
  using type = T;
};

// Note: we do not apply Clang nullability attributes (e.g. _Nullable).  These
// only support raw pointers, and conditionally enabling them only for raw
// pointers inhibits template arg deduction.  Ideally, they would support all
// pointer-like types.
template <typename T, typename = typename EnableNullable<T>::type>
using NullableImpl
#if ABSL_HAVE_CPP_ATTRIBUTE(clang::annotate)
    [[clang::annotate("Nullable")]]
#endif
    = T;

template <typename T, typename = typename EnableNonnull<T>::type>
using NonnullImpl
#if ABSL_HAVE_CPP_ATTRIBUTE(clang::annotate)
    [[clang::annotate("Nonnull")]]
#endif
    = T;

template <typename T, typename = typename EnableNullabilityUnknown<T>::type>
using NullabilityUnknownImpl
#if ABSL_HAVE_CPP_ATTRIBUTE(clang::annotate)
    [[clang::annotate("Nullability_Unspecified")]]
#endif
    = T;

}  // namespace nullability_internal
}  // namespace absl

#endif  // ABSL_BASE_INTERNAL_NULLABILITY_IMPL_H_
