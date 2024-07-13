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

#include "absl/crc/internal/crc_cord_state.h"

#include <cassert>

#include "absl/base/config.h"
#include "absl/base/no_destructor.h"
#include "absl/numeric/bits.h"

namespace absl {
ABSL_NAMESPACE_BEGIN
namespace crc_internal {

// 通过移动构造对象，原对象都会指向同一个空的 RefcountedRep 对象
CrcCordState::RefcountedRep* CrcCordState::RefSharedEmptyRep() {
  static absl::NoDestructor<CrcCordState::RefcountedRep> empty;

  assert(empty->count.load(std::memory_order_relaxed) >= 1);
  assert(empty->rep.removed_prefix.length == 0);
  assert(empty->rep.prefix_crc.empty());

  Ref(empty.get());
  return empty.get();
}

CrcCordState::CrcCordState() : refcounted_rep_(new RefcountedRep) {}
// 拷贝构造函数创建出来的对象和传入的参数对象指向同一个引用计数
CrcCordState::CrcCordState(const CrcCordState& other)
    : refcounted_rep_(other.refcounted_rep_) {
  Ref(refcounted_rep_);
}

CrcCordState::CrcCordState(CrcCordState&& other)
    : refcounted_rep_(other.refcounted_rep_) {
  // Make `other` valid for use after move.
  other.refcounted_rep_ = RefSharedEmptyRep();
}

CrcCordState& CrcCordState::operator=(const CrcCordState& other) {
  if (this != &other) {
    // 拷贝赋值的前提是被赋值对象已经构造好了
    // 原来对象的引用计数减1
    Unref(refcounted_rep_);
    // 指向 other 对象的引用
    refcounted_rep_ = other.refcounted_rep_;
    // 被赋值对象引用计数加1
    Ref(refcounted_rep_);
  }
  return *this;
}

CrcCordState& CrcCordState::operator=(CrcCordState&& other) {
  if (this != &other) {
    Unref(refcounted_rep_);
    refcounted_rep_ = other.refcounted_rep_;
    // Make `other` valid for use after move.
    // 所有移动后的对象都指向同一个空的 RefcountedRep 对象
    other.refcounted_rep_ = RefSharedEmptyRep();
  }
  return *this;
}

// 多个实例指向同一个 RefcountedRep
// 每个实例析构时只减去一个引用计数
CrcCordState::~CrcCordState() {
  Unref(refcounted_rep_);
}

crc32c_t CrcCordState::Checksum() const {
  if (rep().prefix_crc.empty()) {
    return absl::crc32c_t{0};
  }
  if (IsNormalized()) {
    return rep().prefix_crc.back().crc;
  }
  return absl::RemoveCrc32cPrefix(
      rep().removed_prefix.crc, rep().prefix_crc.back().crc,
      rep().prefix_crc.back().length - rep().removed_prefix.length);
}

CrcCordState::PrefixCrc CrcCordState::NormalizedPrefixCrcAtNthChunk(
    size_t n) const {
  assert(n < NumChunks());
  if (IsNormalized()) {
    return rep().prefix_crc[n];
  }
  size_t length = rep().prefix_crc[n].length - rep().removed_prefix.length;
  return PrefixCrc(length,
                   absl::RemoveCrc32cPrefix(rep().removed_prefix.crc,
                                            rep().prefix_crc[n].crc, length));
}

void CrcCordState::Normalize() {
  if (IsNormalized() || rep().prefix_crc.empty()) {
    return;
  }

  Rep* r = mutable_rep();
  // prefix_crc 中存了多个包含前缀的 crc
  // 这里逐个移除前缀的crc，保留去除前缀后的crc
  // 最后将removed_prefix 初始化为空对象
  for (auto& prefix_crc : r->prefix_crc) {
    size_t remaining = prefix_crc.length - r->removed_prefix.length;
    prefix_crc.crc = absl::RemoveCrc32cPrefix(r->removed_prefix.crc,
                                              prefix_crc.crc, remaining);
    prefix_crc.length = remaining;
  }
  r->removed_prefix = PrefixCrc();
}

void CrcCordState::Poison() {
  Rep* rep = mutable_rep();
  if (NumChunks() > 0) {
    for (auto& prefix_crc : rep->prefix_crc) {
      // This is basically CRC32::Scramble().
      uint32_t crc = static_cast<uint32_t>(prefix_crc.crc);
      crc += 0x2e76e41b;
      crc = absl::rotr(crc, 17);
      prefix_crc.crc = crc32c_t{crc};
    }
  } else {
    // Add a fake corrupt chunk.
    rep->prefix_crc.emplace_back(0, crc32c_t{1});
  }
}

}  // namespace crc_internal
ABSL_NAMESPACE_END
}  // namespace absl
