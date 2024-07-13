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

#include "absl/base/no_destructor.h"

#include <array>
#include <initializer_list>
#include <string>
#include <type_traits>
#include <vector>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/base/config.h"
#include "absl/base/internal/raw_logging.h"

namespace {

struct Blob {
  Blob() : val(42) {}
  Blob(int x, int y) : val(x + y) {}
  Blob(std::initializer_list<int> xs) {
    val = 0;
    for (auto& x : xs) val += x;
  }

  Blob(const Blob& /*b*/) = delete;
  Blob(Blob&& b) noexcept : val(b.val) {
    b.moved_out = true;
  }  // moving is fine

  // no crash: NoDestructor indeed does not destruct (the moved-out Blob
  // temporaries do get destroyed though)
  // 如果表达式求值为false，宏可能会记录错误信息、终止程序或采取其他开发者定义的错误处理动作。
  // 在这里，它用来确保moved_out成员变量的值为true。
  // 比如当对象通过移动构造函数或移动赋值操作符被转移后，原始对象的资源会被接管，
  // 此时原始对象应被标记为已移动（moved_out = true），以防止后续的错误访问。

  // 这段代码的目的是确保当Blob对象被销毁时，它要么没有被移动过
  // （即moved_out == false的情况应该在之前的操作中被处理，不应该到达析构阶段），
  // 要么已经被正确标记为已移动（moved_out == true），以此来避免潜在的资源管理
  // 错误或逻辑不一致。这是一种保护措施，有助于提升代码的健壮性和可靠性。
  ~Blob() { ABSL_INTERNAL_CHECK(moved_out, "~Blob"); }

  int val;
  bool moved_out = false;
};

struct TypeWithDeletedDestructor {
  ~TypeWithDeletedDestructor() = delete;
};

TEST(NoDestructorTest, DestructorNeverCalled) {
  absl::NoDestructor<TypeWithDeletedDestructor> a;
  (void)a;
}

TEST(NoDestructorTest, Noncopyable) {
  using T = absl::NoDestructor<int>;
  // 检查类型T是否可以直接从另一个同类型的对象构造
  // 这些断言检验了absl::NoDestructor<int>类型的不可构造性，确保了通过复制、引用
  // 或const引用等方式无法创建新的同类实例，这符合NoDestructor设计用于持有单一、
  // 不可变对象的初衷。
  EXPECT_FALSE((std::is_constructible<T, T>::value));
  EXPECT_FALSE((std::is_constructible<T, const T>::value));
  EXPECT_FALSE((std::is_constructible<T, T&>::value));
  EXPECT_FALSE((std::is_constructible<T, const T&>::value));

  // 检查是否可以将一个T类型的对象直接赋值给T类型的引用
  // 这些断言共同验证了absl::NoDestructor<int>类型的不可赋值性，无论是直接对象、
  // 常量对象还是对象的引用，都不能用来改变现有实例的值。这符合NoDestructor设计的初衷，
  // 即确保封装的对象在其生命周期内既不会被销毁，也不会被修改，
  // 提供了严格的不变性和内存管理控制。
  EXPECT_FALSE((std::is_assignable<T&, T>::value));
  EXPECT_FALSE((std::is_assignable<T&, const T>::value));
  EXPECT_FALSE((std::is_assignable<T&, T&>::value));
  EXPECT_FALSE((std::is_assignable<T&, const T&>::value));
}

TEST(NoDestructorTest, Interface) {
  EXPECT_TRUE(std::is_trivially_destructible<absl::NoDestructor<Blob>>::value);
  EXPECT_TRUE(
      std::is_trivially_destructible<absl::NoDestructor<const Blob>>::value);
  {
    absl::NoDestructor<Blob> b;  // default c-tor
    // access: *, ->, get()
    EXPECT_EQ(42, (*b).val);
    (*b).val = 55;
    EXPECT_EQ(55, b->val);
    b->val = 66;
    EXPECT_EQ(66, b.get()->val);
    b.get()->val = 42;  // NOLINT
    EXPECT_EQ(42, (*b).val);
  }
  {
    absl::NoDestructor<const Blob> b(70, 7);  // regular c-tor, const
    EXPECT_EQ(77, (*b).val);
    EXPECT_EQ(77, b->val);
    EXPECT_EQ(77, b.get()->val);
  }
  {
    const absl::NoDestructor<Blob> b{
        {20, 28, 40}};  // init-list c-tor, deep const
    // This only works in clang, not in gcc:
    // const absl::NoDestructor<Blob> b({20, 28, 40});
    EXPECT_EQ(88, (*b).val);
    EXPECT_EQ(88, b->val);
    EXPECT_EQ(88, b.get()->val);
  }
}

TEST(NoDestructorTest, SfinaeRegressionAbstractArg) {
  struct Abstract {
    virtual ~Abstract() = default;
    virtual int foo() const = 0;
  };

  struct Concrete : Abstract {
    int foo() const override { return 17; }
  };

  struct UsesAbstractInConstructor {
    explicit UsesAbstractInConstructor(const Abstract& abstract)
        : i(abstract.foo()) {}
    int i;
  };

  Concrete input;
  absl::NoDestructor<UsesAbstractInConstructor> foo1(input);
  EXPECT_EQ(foo1->i, 17);
  absl::NoDestructor<UsesAbstractInConstructor> foo2(
      static_cast<const Abstract&>(input));
  EXPECT_EQ(foo2->i, 17);
}

// ========================================================================= //

std::string* Str0() {
  static absl::NoDestructor<std::string> x;
  return x.get();
}

extern const std::string& Str2();

const char* Str1() {
  static absl::NoDestructor<std::string> x(Str2() + "_Str1");
  return x->c_str();
}

const std::string& Str2() {
  static absl::NoDestructor<std::string> x("Str2");
  return *x;
}

const std::string& Str2Copy() {
  // Exercise copy construction
  static absl::NoDestructor<std::string> x(Str2());
  return *x;
}

typedef std::array<std::string, 3> MyArray;
const MyArray& Array() {
  static absl::NoDestructor<MyArray> x{{{"foo", "bar", "baz"}}};
  // This only works in clang, not in gcc:
  // static absl::NoDestructor<MyArray> x({{"foo", "bar", "baz"}});
  return *x;
}

typedef std::vector<int> MyVector;
const MyVector& Vector() {
  static absl::NoDestructor<MyVector> x{{1, 2, 3}};
  return *x;
}

const int& Int() {
  static absl::NoDestructor<int> x;
  return *x;
}

TEST(NoDestructorTest, StaticPattern) {
  EXPECT_TRUE(
      std::is_trivially_destructible<absl::NoDestructor<std::string>>::value);
  EXPECT_TRUE(
      std::is_trivially_destructible<absl::NoDestructor<MyArray>>::value);
  EXPECT_TRUE(
      std::is_trivially_destructible<absl::NoDestructor<MyVector>>::value);
  EXPECT_TRUE(std::is_trivially_destructible<absl::NoDestructor<int>>::value);

  EXPECT_EQ(*Str0(), "");
  Str0()->append("foo");
  EXPECT_EQ(*Str0(), "foo");

  EXPECT_EQ(std::string(Str1()), "Str2_Str1");

  EXPECT_EQ(Str2(), "Str2");
  EXPECT_EQ(Str2Copy(), "Str2");

  EXPECT_THAT(Array(), testing::ElementsAre("foo", "bar", "baz"));

  EXPECT_THAT(Vector(), testing::ElementsAre(1, 2, 3));

  EXPECT_EQ(0, Int());  // should get zero-initialized
}

#ifdef ABSL_HAVE_CLASS_TEMPLATE_ARGUMENT_DEDUCTION
// This would fail to compile if Class Template Argument Deduction was not
// provided for absl::NoDestructor.
TEST(NoDestructorTest, ClassTemplateArgumentDeduction) {
  absl::NoDestructor i(1);
  static_assert(std::is_same<decltype(i), absl::NoDestructor<int>>::value,
                "Expected deduced type to be int.");
}
#endif

}  // namespace
