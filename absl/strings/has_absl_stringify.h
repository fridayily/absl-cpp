// Copyright 2022 The Abseil Authors
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

#ifndef ABSL_STRINGS_HAS_ABSL_STRINGIFY_H_
#define ABSL_STRINGS_HAS_ABSL_STRINGIFY_H_

#include <type_traits>
#include <utility>

#include "absl/strings/string_view.h"

namespace absl {
ABSL_NAMESPACE_BEGIN

namespace strings_internal {

// This is an empty class not intended to be used. It exists so that
// `HasAbslStringify` can reference a universal class rather than needing to be
// copied for each new sink.

// 作为一个通用的占位符或者基类，以便在不需要实现具体功能的情况下，
// 为其他部分代码（如模板或概念检查）提供一个统一的接口参考
class UnimplementedSink {
 public:
  void Append(size_t count, char ch);

  void Append(string_view v);

  // Support `absl::Format(&sink, format, args...)`.
  friend void AbslFormatFlush(UnimplementedSink* sink, absl::string_view v);
};

}  // namespace strings_internal

// `HasAbslStringify<T>` detects if type `T` supports the `AbslStringify()`
// customization point (see
// https://abseil.io/docs/cpp/guides/format#abslstringify for the
// documentation).
//
// Note that there are types that can be `StrCat`-ed that do not use the
// `AbslStringify` customization point (for example, `int`).

// 这是HasAbslStringify的基本模板定义，如果没有特殊化，
// 它默认为std::false_type，意味着默认情况下认为类型T不支持AbslStringify。
template <typename T, typename = void>
struct HasAbslStringify : std::false_type {};

// std::declval: 这是一个C++标准库中的函数，用于获取一个类型的临时右值引用。
// 在这里，它用来创建strings_internal::UnimplementedSink的临时引用和类型T的const引用，
// 而无需实际构造这些对象。

// decltype: 用于推导表达式的结果类型。这里用来检查AbslStringify函数调用的结果类型，
// 该函数接受一个未实施的sink（strings_internal::UnimplementedSink&）
// 和一个类型为T的常量引用作为参数

// std::is_void<decltype(...)>: 检查上述decltype表达式的结果是否为void类型,则特化被启用，
// HasAbslStringify<T>被定义为std::true_type，表示类型T支持AbslStringify。

// 形如
// template <typename T>
// struct HasMappedType<T, absl::void_t<typename T::mapped_type>>
//    : std::true_type {};

// std::enable_if_t<bool> 第二个参数默认为 void
// 这里里面的条件 用于检查 AbslStringify 函数对给定类型T的调用是否返回void类型
template <typename T>
struct HasAbslStringify<
    T, std::enable_if_t<std::is_void<decltype(AbslStringify(
           std::declval<strings_internal::UnimplementedSink&>(),
           std::declval<const T&>()))>::value>> : std::true_type {};

ABSL_NAMESPACE_END
}  // namespace absl

#endif  // ABSL_STRINGS_HAS_ABSL_STRINGIFY_H_
