//
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

#ifndef ABSL_STRINGS_INTERNAL_RESIZE_UNINITIALIZED_H_
#define ABSL_STRINGS_INTERNAL_RESIZE_UNINITIALIZED_H_

#include <algorithm>
#include <string>
#include <type_traits>
#include <utility>

#include "absl/base/port.h"
#include "absl/meta/type_traits.h"  //  for void_t

namespace absl {
ABSL_NAMESPACE_BEGIN
namespace strings_internal {

// In this type trait, we look for a __resize_default_init member function, and
// we use it if available, otherwise, we use resize. We provide HasMember to
// indicate whether __resize_default_init is present.
template <typename string_type, typename = void>
struct ResizeUninitializedTraits {
  using HasMember = std::false_type;
  static void Resize(string_type* s, size_t new_size) { s->resize(new_size); }
};

// __resize_default_init is provided by libc++ >= 8.0
template <typename string_type>
struct ResizeUninitializedTraits<
    string_type, absl::void_t<decltype(std::declval<string_type&>()
                                           .__resize_default_init(237))> > {
  using HasMember = std::true_type;
  static void Resize(string_type* s, size_t new_size) {
    // 用自定义的 resize 函数初始化 s
    s->__resize_default_init(new_size);
  }
};

// 用于检测给定的string_type（一个字符串类型，可能是std::string或其自定义实现）
// 是否支持一种特定的resize操作，即在调整字符串大小时，
// 新增加的字符空间不进行初始化（"left untouched"），
// 这在某些场景下可以提升性能，特别是当这些字符空间随后会被覆盖写入时。
// Returns true if the std::string implementation supports a resize where
// the new characters added to the std::string are left untouched.
//
// (A better name might be "STLStringSupportsUninitializedResize", alluding to
// the previous function.)
template <typename string_type>
inline constexpr bool STLStringSupportsNontrashingResize(string_type*) {
  // 判断逻辑依赖于一个名为ResizeUninitializedTraits<string_type>的
  // 特质类（trait class），这个特质类应该包含了反映string_type是否支持非初始化resize操作的信息
  return ResizeUninitializedTraits<string_type>::HasMember::value;
}

// 用于对std::string类型的字符串进行重新调整大小，但与标准的resize方法不同，
// 它不保证新添加的字符会被初始化为任何特定值（比如默认的'\0'），而是保持未初始化状态。
// Like str->resize(new_size), except any new characters added to "*str" as a
// result of resizing may be left uninitialized, rather than being filled with
// '0' bytes. Typically used when code is then going to overwrite the backing
// store of the std::string with known data.
template <typename string_type, typename = void>
inline void STLStringResizeUninitialized(string_type* s, size_t new_size) {
  ResizeUninitializedTraits<string_type>::Resize(s, new_size);
}

// Used to ensure exponential growth so that the amortized complexity of
// increasing the string size by a small amount is O(1), in contrast to
// O(str->size()) in the case of precise growth.
template <typename string_type>
void STLStringReserveAmortized(string_type* s, size_t new_size) {
  const size_t cap = s->capacity();
  if (new_size > cap) {
    // Make sure to always grow by at least a factor of 2x.
    s->reserve((std::max)(new_size, 2 * cap));
  }
}

// In this type trait, we look for an __append_default_init member function, and
// we use it if available, otherwise, we use append.
// 在这个类型trait中，我们寻找__append_default_init成员函数，
// 如果可用就使用它，否则使用append。
template <typename string_type, typename = void>
struct AppendUninitializedTraits {
  static void Append(string_type* s, size_t n) {
    // 使用string_type的append成员函数，传入n和typename string_type::value_type()。
    // typename string_type::value_type是string_type中字符的类型，
    // 通常为char或wchar_t。append方法会将n个这样的字符（默认初始化为0或空字符）
    // 追加到字符串s的末尾。
    s->append(n, typename string_type::value_type());
  }
};

// decltype(std::declval<string_type&>().__append_default_init(237))尝试构造
// 一个临时的string_type引用，并调用其__append_default_init成员函数，
// 传入237作为参数。这个表达式用于检查__append_default_init函数的存在性。
// 如果函数存在，decltype会得到该函数的返回类型；
// 如果不存在，则整个模板实例化会被SFINAE机制忽略
// std::declval通常用于在模板元编程或类型推导中模拟对某个类型进行操作，而不需要实际创建或初始化对象。
template <typename string_type>
struct AppendUninitializedTraits<
    string_type, absl::void_t<decltype(std::declval<string_type&>()
                                           .__append_default_init(237))> > {
  static void Append(string_type* s, size_t n) {
    s->__append_default_init(n);
  }
};

// Like STLStringResizeUninitialized(str, new_size), except guaranteed to use
// exponential growth so that the amortized complexity of increasing the string
// size by a small amount is O(1), in contrast to O(str->size()) in the case of
// precise growth.
template <typename string_type>
void STLStringResizeUninitializedAmortized(string_type* s, size_t new_size) {
  const size_t size = s->size();
  if (new_size > size) {
    AppendUninitializedTraits<string_type>::Append(s, new_size - size);
  } else {
    // 调用s->erase(new_size)，从字符串s中删除超出new_size位置的所有字符，
    // 将其大小调整到new_size。
    s->erase(new_size);
  }
}

}  // namespace strings_internal
ABSL_NAMESPACE_END
}  // namespace absl

#endif  // ABSL_STRINGS_INTERNAL_RESIZE_UNINITIALIZED_H_
