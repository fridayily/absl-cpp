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

#include "absl/strings/string_view.h"

#ifndef ABSL_USES_STD_STRING_VIEW

#include <algorithm>
#include <climits>
#include <cstring>
#include <ostream>

#include "absl/base/nullability.h"

namespace absl {
ABSL_NAMESPACE_BEGIN

namespace {

// This is significantly faster for case-sensitive matches with very
// few possible matches.
// haystack 草堆 needle 针
absl::Nullable<const char*> memmatch(absl::Nullable<const char*> phaystack,
                                     size_t haylen,
                                     absl::Nullable<const char*> pneedle,
                                     size_t neelen) {
  if (0 == neelen) {
    return phaystack;  // even if haylen is 0
  }
  if (haylen < neelen) return nullptr;

  const char* match;
  // 确保循环在比较最后一个可能的子字符串时不会访问haystack之外的内存。
  const char* hayend = phaystack + haylen - neelen + 1;
  // A static cast is used here as memchr returns a const void *, and pointer
  // arithmetic is not allowed on pointers to void.
  while (
      // 使用 memchr 找到当前haystack搜索范围内第一个字符pneedle[0]的出现位置
      // memchr 用于在一块内存区域中查找指定的字符第一次出现的位置
      // hayend - phaystack 能查找的长度
      (match = static_cast<const char*>(memchr(
           phaystack, pneedle[0], static_cast<size_t>(hayend - phaystack))))) {
    if (memcmp(match, pneedle, neelen) == 0)
      return match;
    else
      phaystack = match + 1;
  }
  return nullptr;
}

void WritePadding(std::ostream& o, size_t pad) {
  char fill_buf[32];
  // 使用 o.fill() 返回的字符值来填充 fill_buf 这个缓冲区的每个字节
  memset(fill_buf, o.fill(), sizeof(fill_buf));
  while (pad) {
    // 一次性填充 n 个字符
    size_t n = std::min(pad, sizeof(fill_buf));
    o.write(fill_buf, static_cast<std::streamsize>(n));
    pad -= n;
  }
}

class LookupTable {
 public:
  // For each character in wanted, sets the index corresponding
  // to the ASCII code of that character. This is used by
  // the find_.*_of methods below to tell whether or not a character is in
  // the lookup table in constant time.
  explicit LookupTable(string_view wanted) {
    for (char c : wanted) {
      table_[Index(c)] = true;
    }
  }
  bool operator[](char c) const { return table_[Index(c)]; }

 private:
  static unsigned char Index(char c) { return static_cast<unsigned char>(c); }
  // 布尔型数组，所有元素默认初始化为false
  bool table_[UCHAR_MAX + 1] = {};
};

}  // namespace

// 用于将string_view类型的piece输出到std::ostream对象o
std::ostream& operator<<(std::ostream& o, string_view piece) {
  // 用于检查o是否准备好进行输出操作。如果sentry构造成功，说明可以安全地进行输出
  std::ostream::sentry sentry(o);
  if (sentry) {
    size_t lpad = 0;
    size_t rpad = 0;
    if (static_cast<size_t>(o.width()) > piece.size()) {
      // 填充量
      size_t pad = static_cast<size_t>(o.width()) - piece.size();
      // 如果o的对齐方式是右对齐（std::ios_base::right），
      // o.flags() & o.adjustfield的结果将是一个非零值，
      // 因为o.adjustfield对应的位被设置。如果o的对齐方式是左对齐，
      // 那么结果也将是一个非零值，因为std::ios_base::left的位被设置
      if ((o.flags() & o.adjustfield) == o.left) {
        rpad = pad;
      } else {
        lpad = pad;
      }
    }
    // lpad 先填充，再写 piece
    if (lpad) WritePadding(o, lpad);
    o.write(piece.data(), static_cast<std::streamsize>(piece.size()));
    // rpad 为真，说明上面没有填充
    if (rpad) WritePadding(o, rpad);
    // 将字段宽度设置为0意味着取消之前设置的宽度限制。
    // 这通常用于重置输出流的宽度，以便后续的输出不受先前设置的宽度影响。
    o.width(0);
  }
  return o;
}

string_view::size_type string_view::find(string_view s,
                                         size_type pos) const noexcept {
  if (empty() || pos > length_) {
    if (empty() && pos == 0 && s.empty()) return 0;
    return npos;
  }
  const char* result = memmatch(ptr_ + pos, length_ - pos, s.ptr_, s.length_);
  return result ? static_cast<size_type>(result - ptr_) : npos;
}

string_view::size_type string_view::find(char c, size_type pos) const noexcept {
  if (empty() || pos >= length_) {
    return npos;
  }
  const char* result =
      static_cast<const char*>(memchr(ptr_ + pos, c, length_ - pos));
  return result != nullptr ? static_cast<size_type>(result - ptr_) : npos;
}

string_view::size_type string_view::rfind(string_view s,
                                          size_type pos) const noexcept {
  if (length_ < s.length_) return npos;
  if (s.empty()) return std::min(length_, pos);
  const char* last = ptr_ + std::min(length_ - s.length_, pos) + s.length_;
  const char* result = std::find_end(ptr_, last, s.ptr_, s.ptr_ + s.length_);
  return result != last ? static_cast<size_type>(result - ptr_) : npos;
}

// Search range is [0..pos] inclusive.  If pos == npos, search everything.
string_view::size_type string_view::rfind(char c,
                                          size_type pos) const noexcept {
  // Note: memrchr() is not available on Windows.
  if (empty()) return npos;
  for (size_type i = std::min(pos, length_ - 1);; --i) {
    if (ptr_[i] == c) {
      return i;
    }
    if (i == 0) break;
  }
  return npos;
}

string_view::size_type string_view::find_first_of(
    string_view s, size_type pos) const noexcept {
  if (empty() || s.empty()) {
    return npos;
  }
  // Avoid the cost of LookupTable() for a single-character search.
  if (s.length_ == 1) return find_first_of(s.ptr_[0], pos);
  LookupTable tbl(s);
  for (size_type i = pos; i < length_; ++i) {
    if (tbl[ptr_[i]]) {
      return i;
    }
  }
  return npos;
}

string_view::size_type string_view::find_first_not_of(
    string_view s, size_type pos) const noexcept {
  if (empty()) return npos;
  // Avoid the cost of LookupTable() for a single-character search.
  if (s.length_ == 1) return find_first_not_of(s.ptr_[0], pos);
  LookupTable tbl(s);
  for (size_type i = pos; i < length_; ++i) {
    if (!tbl[ptr_[i]]) {
      return i;
    }
  }
  return npos;
}

string_view::size_type string_view::find_first_not_of(
    char c, size_type pos) const noexcept {
  if (empty()) return npos;
  for (; pos < length_; ++pos) {
    if (ptr_[pos] != c) {
      return pos;
    }
  }
  return npos;
}

string_view::size_type string_view::find_last_of(string_view s,
                                                 size_type pos) const noexcept {
  if (empty() || s.empty()) return npos;
  // Avoid the cost of LookupTable() for a single-character search.
  if (s.length_ == 1) return find_last_of(s.ptr_[0], pos);
  LookupTable tbl(s);
  for (size_type i = std::min(pos, length_ - 1);; --i) {
    if (tbl[ptr_[i]]) {
      return i;
    }
    if (i == 0) break;
  }
  return npos;
}

string_view::size_type string_view::find_last_not_of(
    string_view s, size_type pos) const noexcept {
  if (empty()) return npos;
  size_type i = std::min(pos, length_ - 1);
  if (s.empty()) return i;
  // Avoid the cost of LookupTable() for a single-character search.
  if (s.length_ == 1) return find_last_not_of(s.ptr_[0], pos);
  LookupTable tbl(s);
  for (;; --i) {
    if (!tbl[ptr_[i]]) {
      return i;
    }
    if (i == 0) break;
  }
  return npos;
}

string_view::size_type string_view::find_last_not_of(
    char c, size_type pos) const noexcept {
  if (empty()) return npos;
  size_type i = std::min(pos, length_ - 1);
  for (;; --i) {
    if (ptr_[i] != c) {
      return i;
    }
    if (i == 0) break;
  }
  return npos;
}

#ifdef ABSL_INTERNAL_NEED_REDUNDANT_CONSTEXPR_DECL
constexpr string_view::size_type string_view::npos;
constexpr string_view::size_type string_view::kMaxSize;
#endif

ABSL_NAMESPACE_END
}  // namespace absl

#else

// https://github.com/abseil/abseil-cpp/issues/1465
// CMake builds on Apple platforms error when libraries are empty.
// Our CMake configuration can avoid this error on header-only libraries,
// but since this library is conditionally empty, including a single
// variable is an easy workaround.
#ifdef __APPLE__
namespace absl {
ABSL_NAMESPACE_BEGIN
namespace strings_internal {
extern const char kAvoidEmptyStringViewLibraryWarning;
const char kAvoidEmptyStringViewLibraryWarning = 0;
}  // namespace strings_internal
ABSL_NAMESPACE_END
}  // namespace absl
#endif  // __APPLE__

#endif  // ABSL_USES_STD_STRING_VIEW
