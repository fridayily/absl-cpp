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
//
// -----------------------------------------------------------------------------
// File: no_destructor.h
// -----------------------------------------------------------------------------
//
// This header file defines the absl::NoDestructor<T> wrapper for defining a
// static type that does not need to be destructed upon program exit. Instead,
// such an object survives during program exit (and can be safely accessed at
// any time).
//
// Objects of such type, if constructed safely and under the right conditions,
// provide two main benefits over other alternatives:
//
//   * Global objects not normally allowed due to concerns of destruction order
//     (i.e. no "complex globals") can be safely allowed, provided that such
//     objects can be constant initialized.
//   * Function scope static objects can be optimized to avoid heap allocation,
//     pointer chasing, and allow lazy construction.
//
// See below for complete details.


#ifndef ABSL_BASE_NO_DESTRUCTOR_H_
#define ABSL_BASE_NO_DESTRUCTOR_H_

#include <new>
#include <type_traits>
#include <utility>

#include "absl/base/config.h"
#include "absl/base/nullability.h"

namespace absl {
ABSL_NAMESPACE_BEGIN

// absl::NoDestructor<T>
//
// NoDestructor<T> is a wrapper around an object of type T that behaves as an
// object of type T but never calls T's destructor. NoDestructor<T> makes it
// safer and/or more efficient to use such objects in static storage contexts:
// as global or function scope static variables.
//
// An instance of absl::NoDestructor<T> has similar type semantics to an
// instance of T:
//
// * Constructs in the same manner as an object of type T through perfect
//   forwarding.
// * Provides pointer/reference semantic access to the object of type T via
//   `->`, `*`, and `get()`.
//   (Note that `const NoDestructor<T>` works like a pointer to const `T`.)
//
// An object of type NoDestructor<T> should be defined in static storage:
// as either a global static object, or as a function scope static variable.
//
// Additionally, NoDestructor<T> provides the following benefits:
//
// * Never calls T's destructor for the object
// * If the object is a function-local static variable, the type can be
//   lazily constructed.
//
// An object of type NoDestructor<T> is "trivially destructible" in the notion
// that its destructor is never run. Provided that an object of this type can be
// safely initialized and does not need to be cleaned up on program shutdown,
// NoDestructor<T> allows you to define global static variables, since Google's
// C++ style guide ban on such objects doesn't apply to objects that are
// trivially destructible.
//
// Usage as Global Static Variables
//
// NoDestructor<T> allows declaration of a global object with a non-trivial
// constructor in static storage without needing to add a destructor.
// However, such objects still need to worry about initialization order, so
// such objects should be const initialized:
//
//    // Global or namespace scope.
//    constinit absl::NoDestructor<MyRegistry> reg{"foo", "bar", 8008};
//
// Note that if your object already has a trivial destructor, you don't need to
// use NoDestructor<T>.
//
// Usage as Function Scope Static Variables
//
// Function static objects will be lazily initialized within static storage:
//
//    // Function scope.
//    const std::string& MyString() {
//      static const absl::NoDestructor<std::string> x("foo");
//      return *x;
//    }
//
// For function static variables, NoDestructor avoids heap allocation and can be
// inlined in static storage, resulting in exactly-once, thread-safe
// construction of an object, and very fast access thereafter (the cost is a few
// extra cycles).
//
// Using NoDestructor<T> in this manner is generally better than other patterns
// which require pointer chasing:
//
//   // Prefer using absl::NoDestructor<T> instead for the static variable.
//   const std::string& MyString() {
//     static const std::string* x = new std::string("foo");
//     return *x;
//   }
//

// void(std::decay_t<Ts>&...) 表示一个接受多个参数（通过参数包Ts...展开）的函数类型，
// 每个参数都是去除了顶层cv限定的Ts类型的引用。
// void(NoDestructor&) 则表示一个接受NoDestructor类型引用的函数类型。
template <typename T>
class NoDestructor {
 public:
  // Forwards arguments to the T's constructor: calls T(args...).
  template <typename... Ts,
            // Disable this overload when it might collide with copy/move.
            typename std::enable_if<!std::is_same<void(std::decay_t<Ts>&...),
                                                  void(NoDestructor&)>::value,
                                    int>::type = 0>
  explicit constexpr NoDestructor(Ts&&... args)
      : impl_(std::forward<Ts>(args)...) {}

  // Forwards copy and move construction for T. Enables usage like this:
  //   static NoDestructor<std::array<string, 3>> x{{{"1", "2", "3"}}};
  //   static NoDestructor<std::vector<int>> x{{1, 2, 3}};
  // 。explicit关键字阻止了隐式转换，确保只有明确的构造调用才会创建NoDestructor对象。
  // constexpr表明如果T的构造也是constexpr，则此构造函数也可以在编译期执行

  // 移动构造函数，它接受一个T&&（类型T的右值引用，即移动语义）作为参数
  explicit constexpr NoDestructor(const T& x) : impl_(x) {}
  explicit constexpr NoDestructor(T&& x)
      : impl_(std::move(x)) {}

  // No copying.
  NoDestructor(const NoDestructor&) = delete;
  NoDestructor& operator=(const NoDestructor&) = delete;

  // Pretend to be a smart pointer to T with deep constness.
  // Never returns a null pointer.
  T& operator*() { return *get(); }
  absl::Nonnull<T*> operator->() { return get(); }
  absl::Nonnull<T*> get() { return impl_.get(); }
  const T& operator*() const { return *get(); }
  absl::Nonnull<const T*> operator->() const { return get(); }
  absl::Nonnull<const T*> get() const { return impl_.get(); }

 private:
  class DirectImpl {
   public:
    template <typename... Args>
    explicit constexpr DirectImpl(Args&&... args)
        : value_(std::forward<Args>(args)...) {}
    absl::Nonnull<const T*> get() const { return &value_; }
    absl::Nonnull<T*> get() { return &value_; }

   private:
    T value_;
  };

  class PlacementImpl {
   public:
    template <typename... Args>
    explicit PlacementImpl(Args&&... args) {
      new (&space_) T(std::forward<Args>(args)...);
    }
    absl::Nonnull<const T*> get() const {
      return Launder(reinterpret_cast<const T*>(&space_));
    }
    absl::Nonnull<T*> get() { return Launder(reinterpret_cast<T*>(&space_)); }

   private:
    // Launder 的函数定义
    // Launder函数主要用于在某些特定场景下，当对象的类型或生命周期发生改变后，确保指针仍然
    // 合法且能够安全地使用。例如，在placement new之后，
    // 或者在使用了类型别名技巧访问了同一内存区域的不同类型实例时。
    template <typename P>
    static absl::Nonnull<P*> Launder(absl::Nonnull<P*> p) {
#if defined(__cpp_lib_launder) && __cpp_lib_launder >= 201606L
      return std::launder(p);
#elif ABSL_HAVE_BUILTIN(__builtin_launder)
      return __builtin_launder(p);
#else
      // When `std::launder` or equivalent are not available, we rely on
      // undefined behavior, which works as intended on Abseil's officially
      // supported platforms as of Q3 2023.
// #pragma GCC diagnostic push: 这条指令保存当前的警告设置，相当于创建了一个警告设置
// 的堆栈。这样，在之后改变警告设置之后，
// 可以使用#pragma GCC diagnostic pop恢复到这个保存的状态。

// #pragma GCC diagnostic ignored "-Wstrict-aliasing": 这条指令告诉编译器忽略特定的
// 警告。在这个例子中，它忽略了-Wstrict-aliasing警告。
// -Wstrict-aliasing警告与C/C++的严格别名规则有关，该规则限制了如何通过指针访问对象。
// 违反这一规则会导致未定义行为，但有时在特定的低层次编程或性能优化场景中，
// 程序员可能故意这样做并且知道风险，此时可以通过忽略这个警告来避免编译器报错或警告。
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wstrict-aliasing"
#endif
      return p;
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic pop
#endif
#endif
    }

    // lignas(T): 这是一个C++11引入的对齐属性说明符，用于指定变量或类型的数据对齐要求。
    // 这里表示数组space_中的元素将会按照类型T的对齐要求进行对齐。

    // 使用unsigned char是因为它是C++标准中唯一保证没有填充字节（padding）的基本类型，
    // 适合用来存储任何类型的数据而不改变其位表示
    alignas(T) unsigned char space_[sizeof(T)];
  };

  // 平凡析构（Trivial Destructor）是指在C++中，当一个类的析构函数没有显式定义，
  // 或者即使定义了但什么操作也不执行（即为空的析构函数），这时该类被认为具有平凡析构性质。
  // 具体来说，平凡析构具有以下几个特征：

  // 自动合成：如果一个类没有用户自定义的析构函数，编译器会自动为它合成（generate）
  //      一个默认的析构函数。如果类中的所有非静态数据成员也都是平凡析构的（递归定义），
  //      那么这个合成的析构函数就是平凡的。
  // 不执行任何操作：平凡析构函数不会执行任何实际的清理工作，它只是简单地通知编译器对象的
  //        生命周期结束，相关的资源（如果有的话）已经由其他机制管理或不再需要特殊处理。
  // 性能优势：由于不需要执行具体的函数体，平凡析构函数在运行时是非常高效的，几乎不产生额外开销。
  // 内存管理：对于具有平凡析构的类型，对象占用的内存可以简单地被视为不再使用而直接释放，
  //      无需调用析构函数进行资源清理。与标准布局和 triviality 相关：
  //      具有平凡析构的类型往往也满足标准布局（Standard Layout）的要求，这意味着它们可以
  //      和C语言中的结构体兼容，易于跨系统和跨语言交互。此外，这样的类型也被称为
  //      trivial type，具有更多编译器可以进行优化的特性，
  //      比如允许memcpy/memmove等操作直接用于对象的复制或移动。

  // If the object is trivially destructible we use a member directly to avoid
  // potential once-init runtime initialization. It somewhat defeats the
  // purpose of NoDestructor in this case, but this makes the class more
  // friendly to generic code.
  std::conditional_t<std::is_trivially_destructible<T>::value, DirectImpl,
                     PlacementImpl>
      impl_;
};

// C++17引入的类模板实参推导（Class Template Argument Deduction, CTAD）的一个应用示例。
// CTAD允许编译器自动推断模板参数的类型，从而简化模板类的实例化语法。

// 在没有CTAD或推导指南的情况下，实例化NoDestructor可能需要明确写出模板参数，
//    如NoDestructor<int> nd(42);
// 有了这段代码，用户可以省略模板参数，直接写成NoDestructor nd(42);
//  编译器会自动推断出T为int类型。
#ifdef ABSL_HAVE_CLASS_TEMPLATE_ARGUMENT_DEDUCTION
// Provide 'Class Template Argument Deduction': the type of NoDestructor's T
// will be the same type as the argument passed to NoDestructor's constructor.
// 它告诉编译器如何从构造函数的参数类型推断出模板参数T的类型
template <typename T>
NoDestructor(T) -> NoDestructor<T>;
#endif  // ABSL_HAVE_CLASS_TEMPLATE_ARGUMENT_DEDUCTION

ABSL_NAMESPACE_END
}  // namespace absl

#endif  // ABSL_BASE_NO_DESTRUCTOR_H_
