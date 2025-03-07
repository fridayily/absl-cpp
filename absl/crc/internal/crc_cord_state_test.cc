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

#include <algorithm>
#include <cstdint>
#include <string>
#include <utility>

#include "gtest/gtest.h"
#include "absl/crc/crc32c.h"

namespace {

TEST(CrcCordState, Default) {
  absl::crc_internal::CrcCordState state;
  EXPECT_TRUE(state.IsNormalized());
  EXPECT_EQ(state.Checksum(), absl::crc32c_t{0});
  state.Normalize();
  EXPECT_EQ(state.Checksum(), absl::crc32c_t{0});
}

TEST(CrcCordState, Ref){
  absl::crc_internal::CrcCordState state;
  auto* rep = state.mutable_rep();
  rep->prefix_crc.push_back(
      absl::crc_internal::CrcCordState::PrefixCrc(1000, absl::crc32c_t{1000}));
  absl::crc_internal::CrcCordState state1 = state;
  absl::crc_internal::CrcCordState state2 = state;
  EXPECT_EQ(state.NumChunks(), 1);
  EXPECT_EQ(state1.NumChunks(), 1);
  EXPECT_EQ(state2.NumChunks(), 1);
  rep->prefix_crc.push_back(
      absl::crc_internal::CrcCordState::PrefixCrc(2000, absl::crc32c_t{2000}));

  EXPECT_EQ(state.NumChunks(), 2);
  EXPECT_EQ(state1.NumChunks(), 2);
  EXPECT_EQ(state2.NumChunks(), 2);


  auto* rep1 = state1.mutable_rep();

  rep1->prefix_crc.push_back(
      absl::crc_internal::CrcCordState::PrefixCrc(3000, absl::crc32c_t{3000}));

  EXPECT_EQ(state.NumChunks(), 2);
  EXPECT_EQ(state1.NumChunks(), 3);
  EXPECT_EQ(state2.NumChunks(), 2);


  // &state.refcounted_rep_->rep
  // 移动构造函数
  absl::crc_internal::CrcCordState moved1 = std::move(state1);
  absl::crc_internal::CrcCordState moved2 = std::move(state2);
  std::cout << "state1.Checksum() = " << state1.Checksum() << std::endl;
  std::cout << "state2.Checksum() = " << state2.Checksum() << std::endl;


}

TEST(CrcCordState, Normalize) {
  absl::crc_internal::CrcCordState state;
  auto* rep = state.mutable_rep();
  rep->prefix_crc.push_back(
      absl::crc_internal::CrcCordState::PrefixCrc(1000, absl::crc32c_t{1000}));
  rep->prefix_crc.push_back(
      absl::crc_internal::CrcCordState::PrefixCrc(2000, absl::crc32c_t{2000}));
  rep->removed_prefix =
      absl::crc_internal::CrcCordState::PrefixCrc(500, absl::crc32c_t{500});

  // The removed_prefix means state is not normalized.
  EXPECT_FALSE(state.IsNormalized());

  absl::crc32c_t crc = state.Checksum();
  state.Normalize();
  EXPECT_TRUE(state.IsNormalized());

  // The checksum should not change as a result of calling Normalize().
  EXPECT_EQ(state.Checksum(), crc);
  EXPECT_EQ(rep->removed_prefix.length, 0);
}

TEST(CrcCordState, Copy) {
  absl::crc_internal::CrcCordState state;
  auto* rep = state.mutable_rep();
  rep->prefix_crc.push_back(
      absl::crc_internal::CrcCordState::PrefixCrc(1000, absl::crc32c_t{1000}));
  // 拷贝构造函数
  absl::crc_internal::CrcCordState copy = state;

  EXPECT_EQ(state.Checksum(), absl::crc32c_t{1000});
  EXPECT_EQ(copy.Checksum(), absl::crc32c_t{1000});
}

TEST(CrcCordState, UnsharedSelfCopy) {
  absl::crc_internal::CrcCordState state;
  auto* rep = state.mutable_rep();
  rep->prefix_crc.push_back(
      absl::crc_internal::CrcCordState::PrefixCrc(1000, absl::crc32c_t{1000}));
  // 定义的引用，而不是一个构造函数
  const absl::crc_internal::CrcCordState& ref = state;
  state = ref;

  EXPECT_EQ(state.Checksum(), absl::crc32c_t{1000});
}

TEST(CrcCordState, Move) {
  absl::crc_internal::CrcCordState state;
  auto* rep = state.mutable_rep();
  rep->prefix_crc.push_back(
      absl::crc_internal::CrcCordState::PrefixCrc(1000, absl::crc32c_t{1000}));
  // 移动构造函数
  absl::crc_internal::CrcCordState moved = std::move(state);
  EXPECT_EQ(moved.Checksum(), absl::crc32c_t{1000});
}

TEST(CrcCordState, UnsharedSelfMove) {
  absl::crc_internal::CrcCordState state;
  auto* rep = state.mutable_rep();
  rep->prefix_crc.push_back(
      absl::crc_internal::CrcCordState::PrefixCrc(1000, absl::crc32c_t{1000}));
  // 定义的引用，而不是一个构造函数
  absl::crc_internal::CrcCordState& ref = state;
  state = std::move(ref);

  EXPECT_EQ(state.Checksum(), absl::crc32c_t{1000});
}

TEST(CrcCordState, PoisonDefault) {
  absl::crc_internal::CrcCordState state;
  state.Poison();
  EXPECT_NE(state.Checksum(), absl::crc32c_t{0});
}

TEST(CrcCordState, PoisonData) {
  absl::crc_internal::CrcCordState state;
  auto* rep = state.mutable_rep();
  rep->prefix_crc.push_back(
      absl::crc_internal::CrcCordState::PrefixCrc(1000, absl::crc32c_t{1000}));
  rep->prefix_crc.push_back(
      absl::crc_internal::CrcCordState::PrefixCrc(2000, absl::crc32c_t{2000}));
  rep->removed_prefix =
      absl::crc_internal::CrcCordState::PrefixCrc(500, absl::crc32c_t{500});

  absl::crc32c_t crc = state.Checksum();
  state.Poison();
  EXPECT_NE(state.Checksum(), crc);
}

}  // namespace
