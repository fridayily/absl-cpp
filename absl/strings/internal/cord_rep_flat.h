// Copyright 2020 The Abseil Authors
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

#ifndef ABSL_STRINGS_INTERNAL_CORD_REP_FLAT_H_
#define ABSL_STRINGS_INTERNAL_CORD_REP_FLAT_H_

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <memory>

#include "absl/base/config.h"
#include "absl/base/macros.h"
#include "absl/strings/internal/cord_internal.h"

namespace absl {
ABSL_NAMESPACE_BEGIN
namespace cord_internal {

// Note: all constants below are never ODR used and internal to cord, we define
// these as static constexpr to avoid 'in struct' definition and usage clutter.

// Largest and smallest flat node lengths we are willing to allocate
// Flat allocation size is stored in tag, which currently can encode sizes up
// to 4K, encoded as multiple of either 8 or 32 bytes.
// If we allow for larger sizes, we need to change this to 8/64, 16/128, etc.
// kMinFlatSize is bounded by tag needing to be at least FLAT * 8 bytes, and
// ideally a 'nice' size aligning with allocation and cacheline sizes like 32.
// kMaxFlatSize is bounded by the size resulting in a computed tag no greater
// than MAX_FLAT_TAG. MAX_FLAT_TAG provides for additional 'high' tag values.

// CordRep.storage 之前定义了一些成员变量，之后用来存储数据
// 即在一个类中，成员变量和数据是紧密的存在一起

// storage[3] 在不同的派生类中作用不同
static constexpr size_t kFlatOverhead = offsetof(CordRep, storage);
// 允许分配的最小 flat node 大小，设置为32字节
static constexpr size_t kMinFlatSize = 32;
// 允许分配的最大 flat node 大小，设置为4K字节
static constexpr size_t kMaxFlatSize = 4096;

// 分别代表了在扣除 kFlatOverhead 后，实际可用于数据存储的最大和最小 flat node 长度。
// 这是考虑了结构体开销后的有效数据容量。
static constexpr size_t kMaxFlatLength = kMaxFlatSize - kFlatOverhead;
static constexpr size_t kMinFlatLength = kMinFlatSize - kFlatOverhead;

static constexpr size_t kMaxLargeFlatSize = 256 * 1024;
static constexpr size_t kMaxLargeFlatLength = kMaxLargeFlatSize - kFlatOverhead;

// kTagBase should make the Size <--> Tag computation resilient
// against changes to the value of FLAT when we add a new tag..
static constexpr uint8_t kTagBase = FLAT - 4;

// Converts the provided rounded size to the corresponding tag
constexpr uint8_t AllocatedSizeToTagUnchecked(size_t size) {
  return static_cast<uint8_t>(size <= 512 ? kTagBase + size / 8
                              : size <= 8192
                                  ? kTagBase + 512 / 8 + size / 64 - 512 / 64
                                  : kTagBase + 512 / 8 + ((8192 - 512) / 64) +
                                        size / 4096 - 8192 / 4096);
}

// Converts the provided tag to the corresponding allocated size
constexpr size_t TagToAllocatedSize(uint8_t tag) {
  return (tag <= kTagBase + 512 / 8) ? tag * 8 - kTagBase * 8
         : (tag <= kTagBase + (512 / 8) + ((8192 - 512) / 64))
             ? 512 + tag * 64 - kTagBase * 64 - 512 / 8 * 64
             : 8192 + tag * 4096 - kTagBase * 4096 -
                   ((512 / 8) + ((8192 - 512) / 64)) * 4096;
}

static_assert(AllocatedSizeToTagUnchecked(kMinFlatSize) == FLAT, "");
static_assert(AllocatedSizeToTagUnchecked(kMaxLargeFlatSize) == MAX_FLAT_TAG,
              "");

// RoundUp logically performs `((n + m - 1) / m) * m` to round up to the nearest
// multiple of `m`, optimized for the invariant that `m` is a power of 2.
// n向上取整至最接近m的倍数的结果
constexpr size_t RoundUp(size_t n, size_t m) { return (n + m - 1) & (0 - m); }

// Returns the size to the nearest equal or larger value that can be
// expressed exactly as a tag value.
inline size_t RoundUpForTag(size_t size) {
  return RoundUp(size, (size <= 512) ? 8 : (size <= 8192 ? 64 : 4096));
}

// Converts the allocated size to a tag, rounding down if the size
// does not exactly match a 'tag expressible' size value. The result is
// undefined if the size exceeds the maximum size that can be encoded in
// a tag, i.e., if size is larger than TagToAllocatedSize(<max tag>).
inline uint8_t AllocatedSizeToTag(size_t size) {
  const uint8_t tag = AllocatedSizeToTagUnchecked(size);
  assert(tag <= MAX_FLAT_TAG);
  return tag;
}

// Converts the provided tag to the corresponding available data length
constexpr size_t TagToLength(uint8_t tag) {
  return TagToAllocatedSize(tag) - kFlatOverhead;
}

// Enforce that kMaxFlatSize maps to a well-known exact tag value.
static_assert(TagToAllocatedSize(MAX_FLAT_TAG) == kMaxLargeFlatSize,
              "Bad tag logic");

// CordRepFlat 继承 CordRep
// CordRepFlat本身没有任何成员函数，只有自定义的函数来操作 CordRep 的成员
struct CordRepFlat : public CordRep {
  // Tag for explicit 'large flat' allocation
  struct Large {};

  // Creates a new flat node.
  template <size_t max_flat_size, typename... Args>
  static CordRepFlat* NewImpl(size_t len, Args... args ABSL_ATTRIBUTE_UNUSED) {
    // 根据传入的模板参数限制len 的最大值
    if (len <= kMinFlatLength) {
      len = kMinFlatLength;
    } else if (len > max_flat_size - kFlatOverhead) {
      len = max_flat_size - kFlatOverhead;
    }

    // Round size up so it matches a size we can exactly express in a tag.
    // 将长度len加上固定开销kFlatOverhead后向上取整，确保结果大小能够精确地用
    // 一个“标签”表示。这通常涉及到某种形式的内存管理或对齐优化。
    const size_t size = RoundUpForTag(len + kFlatOverhead);
    // 使用全局的::operator
    // new动态分配计算出的大小的内存，这包括了数据本身和可能的头部开销
    void* const raw_rep = ::operator new(size);
// GCC 13 has a false-positive -Wstringop-overflow warning here.
#if ABSL_INTERNAL_HAVE_MIN_GNUC_VERSION(13, 0)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wstringop-overflow"
#endif
    // 在刚分配的内存上直接放置一个新的CordRepFlat对象（使用placement new语法）
    // new 是一个运算符， A* a = new A 的过程
    //   1.调用operator new (重载或者全局的)
    //   2.调用构造函数生成类对象
    //   3.返回相应指针
    //  operator new 是一个函数，
    //      ::operator new 指调用全局的 operator new
    //  A a; 静态建立，栈中分配内存
    //  A* a = new A 动态建立 堆中分配内存  rep 和 raw_rep 指向一个地址
    CordRepFlat* rep = new (raw_rep) CordRepFlat();
    // 通过分配的 size 计算 tag
    rep->tag = AllocatedSizeToTag(size);
#if ABSL_INTERNAL_HAVE_MIN_GNUC_VERSION(13, 0)
#pragma GCC diagnostic pop
#endif
    return rep;
  }
  // 在堆上分配 size 的大小的内存，并返回一个指向该内存的指针，没有赋值
  static CordRepFlat* New(size_t len) { return NewImpl<kMaxFlatSize>(len); }

  static CordRepFlat* New(Large, size_t len) {
    return NewImpl<kMaxLargeFlatSize>(len);
  }

  // Deletes a CordRepFlat instance created previously through a call to New().
  // Flat CordReps are allocated and constructed with raw ::operator new and
  // placement new, and must be destructed and deallocated accordingly.
  static void Delete(CordRep* rep) {
    assert(rep->tag >= FLAT && rep->tag <= MAX_FLAT_TAG);

#if defined(__cpp_sized_deallocation)
    size_t size = TagToAllocatedSize(rep->tag);
    rep->~CordRep();
    ::operator delete(rep, size);
#else
    rep->~CordRep(); // 父析构函数不是虚函数，子类析构时父类析构函数不会主动调用
    ::operator delete(rep);
    // rep 是::operator new(size) 返回的指针
#endif
  }

  // Create a CordRepFlat containing `data`, with an optional additional
  // extra capacity of up to `extra` bytes. Requires that `data.size()`
  // is less than kMaxFlatLength.
  static CordRepFlat* Create(absl::string_view data, size_t extra = 0) {
    assert(data.size() <= kMaxFlatLength);
    CordRepFlat* flat = New(data.size() + (std::min)(extra, kMaxFlatLength));
    memcpy(flat->Data(), data.data(), data.size());
    flat->length = data.size();
    return flat;
  }

  // Returns a pointer to the data inside this flat rep.
  // 返回存储数据的指针
  char* Data() { return reinterpret_cast<char*>(storage); }
  const char* Data() const { return reinterpret_cast<const char*>(storage); }

  // Returns the maximum capacity (payload size) of this instance.
  size_t Capacity() const { return TagToLength(tag); }

  // Returns the allocated size (payload + overhead) of this instance.
  size_t AllocatedSize() const { return TagToAllocatedSize(tag); }
};

// Now that CordRepFlat is defined, we can define CordRep's helper casts:
inline CordRepFlat* CordRep::flat() {
  assert(tag >= FLAT && tag <= MAX_FLAT_TAG);
  return reinterpret_cast<CordRepFlat*>(this);
}

inline const CordRepFlat* CordRep::flat() const {
  assert(tag >= FLAT && tag <= MAX_FLAT_TAG);
  return reinterpret_cast<const CordRepFlat*>(this);
}

}  // namespace cord_internal
ABSL_NAMESPACE_END
}  // namespace absl

#endif  // ABSL_STRINGS_INTERNAL_CORD_REP_FLAT_H_
