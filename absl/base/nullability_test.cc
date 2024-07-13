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

#include "absl/base/nullability.h"

#include <cassert>
#include <memory>
#include <utility>

#include "gtest/gtest.h"
#include "absl/base/attributes.h"

namespace {
using ::absl::Nonnull;
using ::absl::NullabilityUnknown;
using ::absl::Nullable;

// 此函数明确地声明了一个Nonnull<int*>类型的参数，
// 意味着调用时传入的指针必须指向一个有效的int对象，不能为nullptr
void funcWithNonnullArg(Nonnull<int*> /*arg*/) {}
template <typename T>
void funcWithDeducedNonnullArg(Nonnull<T*> /*arg*/) {}

TEST(NonnullTest, NonnullArgument) {
  int var = 0;
  funcWithNonnullArg(&var);
  funcWithDeducedNonnullArg(&var);

  int * p= nullptr;
  funcWithNonnullArg(p);
}

Nonnull<int*> funcWithNonnullReturn() {
  static int var = 0;
  return &var;
}

TEST(NonnullTest, NonnullReturn) {
  auto var = funcWithNonnullReturn();
  (void)var;
}

// static_assert(std::is_same<int, int>::value, "Error: Types are not the same");
// static_assert(!std::is_same<int, double>::value,
//        "Error: Types are the same but should be different");

// ./absl_nullability_test --gtest_filter=PassThroughTest::PassesThroughRawPointerToInt
TEST(PassThroughTest, PassesThroughRawPointerToInt) {
  EXPECT_TRUE((std::is_same<Nonnull<int*>, int*>::value));
  EXPECT_TRUE((std::is_same<Nullable<int*>, int*>::value));
  EXPECT_TRUE((std::is_same<NullabilityUnknown<int*>, int*>::value));
}

TEST(PassThroughTest, PassesThroughRawPointerToVoid) {
  EXPECT_TRUE((std::is_same<Nonnull<void*>, void*>::value));
  EXPECT_TRUE((std::is_same<Nullable<void*>, void*>::value));
  EXPECT_TRUE((std::is_same<NullabilityUnknown<void*>, void*>::value));
}

TEST(PassThroughTest, PassesThroughUniquePointerToInt) {
  using T = std::unique_ptr<int>;
  EXPECT_TRUE((std::is_same<Nonnull<T>, T>::value));
  EXPECT_TRUE((std::is_same<Nullable<T>, T>::value));
  EXPECT_TRUE((std::is_same<NullabilityUnknown<T>, T>::value));
}

TEST(PassThroughTest, PassesThroughSharedPointerToInt) {
  using T = std::shared_ptr<int>;
  EXPECT_TRUE((std::is_same<Nonnull<T>, T>::value));
  EXPECT_TRUE((std::is_same<Nullable<T>, T>::value));
  EXPECT_TRUE((std::is_same<NullabilityUnknown<T>, T>::value));
}

TEST(PassThroughTest, PassesThroughSharedPointerToVoid) {
  using T = std::shared_ptr<void>;
  EXPECT_TRUE((std::is_same<Nonnull<T>, T>::value));
  EXPECT_TRUE((std::is_same<Nullable<T>, T>::value));
  EXPECT_TRUE((std::is_same<NullabilityUnknown<T>, T>::value));
}

TEST(PassThroughTest, PassesThroughPointerToMemberObject) {
  using T = decltype(&std::pair<int, int>::first);
  EXPECT_TRUE((std::is_same<Nonnull<T>, T>::value));
  EXPECT_TRUE((std::is_same<Nullable<T>, T>::value));
  EXPECT_TRUE((std::is_same<NullabilityUnknown<T>, T>::value));
}

TEST(PassThroughTest, PassesThroughPointerToMemberFunction) {
  using T = decltype(&std::unique_ptr<int>::reset);
  EXPECT_TRUE((std::is_same<Nonnull<T>, T>::value));
  EXPECT_TRUE((std::is_same<Nullable<T>, T>::value));
  EXPECT_TRUE((std::is_same<NullabilityUnknown<T>, T>::value));
}

}  // namespace

// “Argument-Dependent Lookup (ADL)”（基于参数的查找，也称作Koenig查找）
// ADL是一种编译器查找函数或函数模板的方式，它发生在当函数调用是无资格的
// （unqualified，即没有指定作用域解析符::）并且至少有一个参数是
// 某个类的类型或者指针/引用到某个类的类型时。
// 在这种情况下，除了在常规的作用域中查找外，
// 编译器还会在该参数类型的命名空间中查找函数或函数模板

// Nullable ADL lookup test
namespace util {
// Helper for NullableAdlTest.  Returns true, denoting that argument-dependent
// lookup found this implementation of DidAdlWin.  Must be in namespace
// util itself, not a nested anonymous namespace.
template <typename T>
bool DidAdlWin(T*) {
  return true;
}

// Because this type is defined in namespace util, an unqualified call to
// DidAdlWin with a pointer to MakeAdlWin will find the above implementation.
struct MakeAdlWin {};
}  // namespace util

namespace {
// 这表示如果ADL（Argument-Dependent Lookup）在调用时考虑了util命名空间，
// 那么这个全局的DidAdlWin函数就不会被选中，
// 因为util命名空间内的DidAdlWin模板函数（接受一个T*参数）更匹配

// 这段代码的目的是为了测试ADL机制是否能够正确地优先选择util命名空间内的DidAdlWin模板函数。
// 如果ADL生效，那么当传入util::MakeAdlWin类型的指针调用DidAdlWin时，
// 应该会选择util命名空间内的模板版本，返回true。相反，如果ADL没有生效，
// 编译器可能会找到全局作用域的DidAdlWin函数（接受变长参数），
// 导致返回false。通过比较返回值，可以验证ADL的行为。

// Returns false, denoting that ADL did not inspect namespace util.  If it
// had, the better match (T*) above would have won out over the (...) here.
bool DidAdlWin(...) { return false; }

TEST(NullableAdlTest, NullableAddsNothingToArgumentDependentLookup) {
  // Treatment: util::Nullable<int*> contributes nothing to ADL because
  // int* itself doesn't.
  EXPECT_FALSE(DidAdlWin((int*)nullptr));
  EXPECT_FALSE(DidAdlWin((Nullable<int*>)nullptr));

  // Control: Argument-dependent lookup does find the implementation in
  // namespace util when the underlying pointer type resides there.
  EXPECT_TRUE(DidAdlWin((util::MakeAdlWin*)nullptr));
  EXPECT_TRUE(DidAdlWin((Nullable<util::MakeAdlWin*>)nullptr));
}
}  // namespace
