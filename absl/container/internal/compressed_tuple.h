// Copyright 2018 The Abseil Authors.
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
// Helper class to perform the Empty Base Optimization.
// Ts can contain classes and non-classes, empty or not. For the ones that
// are empty classes, we perform the optimization. If all types in Ts are empty
// classes, then CompressedTuple<Ts...> is itself an empty class.
//
// To access the members, use member get<N>() function.
//
// Eg:
//   absl::container_internal::CompressedTuple<int, T1, T2, T3> value(7, t1, t2,
//                                                                    t3);
//   assert(value.get<0>() == 7);
//   T1& t1 = value.get<1>();
//   const T2& t2 = value.get<2>();
//   ...
//
// https://en.cppreference.com/w/cpp/language/ebo

#ifndef ABSL_CONTAINER_INTERNAL_COMPRESSED_TUPLE_H_
#define ABSL_CONTAINER_INTERNAL_COMPRESSED_TUPLE_H_

#include <initializer_list>
#include <tuple>
#include <type_traits>
#include <utility>

#include "absl/utility/utility.h"

#if defined(_MSC_VER) && !defined(__NVCC__)
// We need to mark these classes with this declspec to ensure that
// CompressedTuple happens.
#define ABSL_INTERNAL_COMPRESSED_TUPLE_DECLSPEC __declspec(empty_bases)
#else
#define ABSL_INTERNAL_COMPRESSED_TUPLE_DECLSPEC
#endif

namespace absl {
ABSL_NAMESPACE_BEGIN
namespace container_internal {

template <typename... Ts>
class CompressedTuple;

namespace internal_compressed_tuple {

// std::tuple<int, double, std::string> my_tuple;
// using FirstType = typename std::tuple_element<0, my_tuple>::type;
// std::cout << "First element type: " << typeid(FirstType).name() << std::endl;

// D is the CompressedTuple<...> type.
// I 是 D 的索引
template <typename D, size_t I>
struct Elem;
template <typename... B, size_t I>
struct Elem<CompressedTuple<B...>, I>
    : std::tuple_element<I, std::tuple<B...>> {};
template <typename D, size_t I>
using ElemT = typename Elem<D, I>::type;

// EBCO 是 "Empty Base Optimization" 的缩写，在 C++ 中是一种编译器优化技术，
// 用于处理空基类的情况。当一个类从一个或多个空基类派生时，
// EBCO 允许编译器避免为这些空基类分配额外的内存。这有助于减少派生类对象的总内存占用，
// 特别是在多重继承中，多个空基类可能会导致不必要的内存开销
// 但会存在特殊情况
// 1. 多重继承同一空基类：如果 CompressedTuples 从多个 Storage<> 实例继承，
// 且这些实例具有相同的 I 参数，那么尝试使用 EBCO 可能会导致编译器无法区分这些基类实例，
// 因为它们本质上是相同的。这可能导致对象布局混乱，破坏了继承体系的正确性。
// 2. 多份相同基类实例：即使 I 参数不同，但如果多个 Storage<> 实例实际上是
// 相同类型或具有相同的布局，EBCO 可能会导致编译器错误地合并这些基类，
// 从而破坏了对象的内存布局和多继承的语义。
// We can't use EBCO on other CompressedTuples because that would mean that we
// derive from multiple Storage<> instantiations with the same I parameter,
// and potentially from multiple identical Storage<> instantiations.  So anytime
// we use type inheritance rather than encapsulation（封装）, we mark
// CompressedTupleImpl, to make this easy to detect.
struct uses_inheritance {};

// !std::is_base_of<uses_inheritance, T>::value：检查 T
// 是否不是从 uses_inheritance 继承的
// 如果 ShouldUseBase 返回 true ，则 Storage 会继承 T
// 否则 Storage 会把 T 当成一个成员函数
template <typename T>
constexpr bool ShouldUseBase() {
  return std::is_class<T>::value && std::is_empty<T>::value &&
         !std::is_final<T>::value &&
         !std::is_base_of<uses_inheritance, T>::value;
}

// The storage class provides two specializations:
//  - For empty classes, it stores T as a base class.
//  - For everything else, it stores T as a member.
// 当 UseBase 为 False, T 作为一个成员变量
// 当 UseBase 为 True, T 作为一个基类
// Storage 模板的这两种特化体现了 C++ 中的多态性和模板元编程的强大。
// 通过选择不同的存储策略（作为成员或作为基类），Storage 能够在保证类型安全的同时，
// 优化内存使用和性能。对于非空类，它作为成员变量存储，提供直接访问和控制；
// 对于空类，则利用 EBO 优化，将其作为基类存储，避免了不必要的内存开销。
// 这种设计使得压缩元组能够高效地处理各种类型的数据，特别是在涉及空类和小对象的场景中。
template <typename T, size_t I, bool UseBase = ShouldUseBase<T>()>
struct Storage {
  T value;
  constexpr Storage() = default;
  template <typename V>
  explicit constexpr Storage(absl::in_place_t, V&& v)
      : value(std::forward<V>(v)) {}
  // constexpr：表明此函数可以在编译时被求值
  // const T&：返回一个指向 T 类型的常量引用，不允许通过返回的引用修改 T
  // const&：表明这个函数是 const 成员函数，即它不会修改调用它的对象。
  // 同时，返回值是一个左值引用，表示返回的对象存在于内存中，可以被多次引用。
  // 最后的 & 后缀进一步限制了调用者必须是一个左值。
  constexpr const T& get() const& { return value; }
  // T&：返回一个指向 T 类型的引用，允许通过返回的引用修改 T。
  // &：这个函数只能被左值调用
  // 左值引用通常用于引用已经存在的对象，而不是创建新的对象副本。
  // 这意味着 get() 函数返回的引用是指向 Storage 内部 T 类型成员 value 的一个直接引用。
  // 两个作用：
  // 1. 返回内部成员的引用，减少复制
  // 2. 调用者可以使用返回的引用来修改 value 的内容
  T& get() & { return value; }
  // const T&&：这表示函数返回一个 T 类型的常量右值引用。
  // 右值引用通常用于表示临时对象或即将被销毁的对象，
  // 它们可以被移动而非复制，以减少开销。const 关键字则限制了通过这个引用对对象的修改。
  // const&&：这里的 const 表明这是一个 const 成员函数，
  // 意味着它不会修改 Storage 类的任何成员变量。&& 表明调用者是临时对象
  constexpr const T&& get() const&& { return std::move(*this).value; }
  // 第一个 && 在返回类型后面，表示返回值是一个右值引用
  // 第二个 && 在括号后面，表示这是一个可调用的临时对象的上下文，
  // 意味着这个函数只能被右值调用，例如临时对象或通过移动语义传递的参数
  T&& get() && { return std::move(*this).value; }
};

// 这里是 Storage 的一个特化版本，ShouldUseBase 为 true，就继承自 T
template <typename T, size_t I>
struct ABSL_INTERNAL_COMPRESSED_TUPLE_DECLSPEC Storage<T, I, true> : T {
  constexpr Storage() = default;

  template <typename V>
  explicit constexpr Storage(absl::in_place_t, V&& v) : T(std::forward<V>(v)) {}

  constexpr const T& get() const& { return *this; }
  T& get() & { return *this; }
  constexpr const T&& get() const&& { return std::move(*this); }
  T&& get() && { return std::move(*this); }
};

template <typename D, typename I, bool ShouldAnyUseBase>
struct ABSL_INTERNAL_COMPRESSED_TUPLE_DECLSPEC CompressedTupleImpl;

// template <std::size_t... Is>
// void foo(std::index_sequence<Is...>) {
//    // Is... 将会是 0, 1, 2, ..., N-1
//   }
// foo(std::make_index_sequence<5>{});

// bool ShouldAnyUseBase：这个布尔标志用于指示是否任何类型 Ts 应该使用 EBCO
// uses_inheritance：这是之前定义的标记类型，用于指示当前类使用了类型继承而非封装
// 类继承列表后面的三个点（...）代表的是参数包（parameter pack）
// 即 几个多个相同类型或不同类型的 Storage
// template<typename T, typename... Args>
// class Derived : public Base<T>, public Base<Args>...
// {
//  // class definition
// };
// Derived<int, double, char> myDerived;
// 那么实际上 myDerived 将会从 Base<int>, Base<double>, 和 Base<char> 这三个基类继承。
template <typename... Ts, size_t... I, bool ShouldAnyUseBase>
struct ABSL_INTERNAL_COMPRESSED_TUPLE_DECLSPEC CompressedTupleImpl<
    CompressedTuple<Ts...>, absl::index_sequence<I...>, ShouldAnyUseBase>
    // We use the dummy identity function through std::integral_constant to
    // convince MSVC of accepting and expanding I in that context. Without it
    // you would get:
    //   error C3548: 'I': parameter pack cannot be used in this context
    : uses_inheritance,
      Storage<Ts, std::integral_constant<size_t, I>::value>... {
  constexpr CompressedTupleImpl() = default;
  // std::in_place 是C++17 标准中引入的一个构造标记，主要用于在容器或复合类型中直接构造对象，
  // 而不是先构造一个临时对象再进行移动或复制。
  // 最后的 ... 把 args 参数包中的每一个参数都分别传递给 Storage<Ts, I> 的各个实例的构造函数。
  // 例如 CompressedTuple<int, Empty<0>, S> x(7, {}, S{"ABC"});
  //  会调用三次 Storage
  template <typename... Vs>
  explicit constexpr CompressedTupleImpl(absl::in_place_t, Vs&&... args)
      : Storage<Ts, I>(absl::in_place, std::forward<Vs>(args))... {}
  friend CompressedTuple<Ts...>;
};

// 特化版本
template <typename... Ts, size_t... I>
struct ABSL_INTERNAL_COMPRESSED_TUPLE_DECLSPEC CompressedTupleImpl<
    CompressedTuple<Ts...>, absl::index_sequence<I...>, false>
    // We use the dummy identity function as above...
    : Storage<Ts, std::integral_constant<size_t, I>::value, false>... {
  constexpr CompressedTupleImpl() = default;
  // 在编译期计算
  template <typename... Vs>
  explicit constexpr CompressedTupleImpl(absl::in_place_t, Vs&&... args)
      : Storage<Ts, I, false>(absl::in_place, std::forward<Vs>(args))... {}
  friend CompressedTuple<Ts...>;
};

// 这里的定义什么意思？
// 符合语法规范吗？
std::false_type Or(std::initializer_list<std::false_type>);
std::true_type Or(std::initializer_list<bool>);

// MSVC requires this to be done separately rather than within the declaration
// of CompressedTuple below.
// ... 运算符将这一系列的 std::integral_constant 类型展开成一个初始化列表。这意味着
// 如果你有多个 Ts 参数，你将得到一个 std::integral_constant 对象的列表，
// 每个对象表示一个 Ts 类型是否满足 ShouldUseBase 的条件
template <typename... Ts>
constexpr bool ShouldAnyUseBase() {
  // decltype 返回一个类型
  //  {} 调用返回类型的默认构造函数
  return decltype(
      Or({std::integral_constant<bool, ShouldUseBase<Ts>()>()...})){};
}

// std::is_reference<T>::value：首先检查T是否是一个引用类型。如果是引用类型，
// 那么我们关心的是V是否可以直接赋值给T
// std::is_convertible<V, T>：如果T是引用类型，那么使用std::is_convertible
// 检查V是否可以隐式转换为T，即V是否可以直接赋值给T
// 当std::is_convertible<T&&, U&>::value为true时，这意味着T&&可以被转换为U&，
// 也就是说，T&& 可以绑定到 U& 上
// 如果 U 有移动构造函数，可以从 T&& 构造

// std::is_constructible<T, V&&>：如果T不是引用类型，
// 那么使用std::is_constructible检查V是否可以通过右值引用V&&构造T。
// 这意味着T可以从V的移动版本构造

// std::conditional：根据T是否是引用类型，选择使用std::is_convertible
// 或std::is_constructible。如果T是引用类型，使用std::is_convertible；否则，使用std::is_constructible
template <typename T, typename V>
using TupleElementMoveConstructible =
    typename std::conditional<std::is_reference<T>::value,
                              std::is_convertible<V, T>,
                              std::is_constructible<T, V&&>>::type;

// 其默认行为是返回std::false_type，表示元组不可从给定类型移动构造
template <bool SizeMatches, class T, class... Vs>
struct TupleMoveConstructible : std::false_type {};

// 当 T 是 CompressedTuple<Ts...> 且 sizeof...(Ts) == sizeof...(Vs) 特化才生效
// 此时可以检查 CompressedTuple 的每个元素类型 Ts 是否能够从对应的Vs类型移动构造
// 如果 T 不是CompressedTuple，那么这个特化就不适用
// 最后一个 ... 会解包 Ts,Vs, 并用 TupleElementMoveConstructible 检查
template <class... Ts, class... Vs>
struct TupleMoveConstructible<true, CompressedTuple<Ts...>, Vs...>
    : std::integral_constant<
          bool, absl::conjunction<
                    TupleElementMoveConstructible<Ts, Vs&&>...>::value> {};

template <typename T>
struct compressed_tuple_size;

// std::integral_constant<int, 42> my_const;
// std::cout << my_const.value << std::endl;  // 输出 42
// sizeof..(Es) 在编译时计算参数数量
// 只有在 T 是 CompressedTuple<Es...> 时生效
template <typename... Es>
struct compressed_tuple_size<CompressedTuple<Es...>>
    : public std::integral_constant<std::size_t, sizeof...(Es)> {};

template <class T, class... Vs>
struct TupleItemsMoveConstructible
    : std::integral_constant<
          bool, TupleMoveConstructible<compressed_tuple_size<T>::value ==
                                           sizeof...(Vs),
                                       T, Vs...>::value> {};

}  // namespace internal_compressed_tuple

// Helper class to perform the Empty Base Class Optimization.
// Ts can contain classes and non-classes, empty or not. For the ones that
// are empty classes, we perform the CompressedTuple. If all types in Ts are
// empty classes, then CompressedTuple<Ts...> is itself an empty class.  (This
// does not apply when one or more of those empty classes is itself an empty
// CompressedTuple.)
//
// To access the members, use member .get<N>() function.
//
// Eg:
//   absl::container_internal::CompressedTuple<int, T1, T2, T3> value(7, t1, t2,
//                                                                    t3);
//   assert(value.get<0>() == 7);
//   T1& t1 = value.get<1>();
//   const T2& t2 = value.get<2>();
//   ...
//
// https://en.cppreference.com/w/cpp/language/ebo
template <typename... Ts>
class ABSL_INTERNAL_COMPRESSED_TUPLE_DECLSPEC CompressedTuple
    : private internal_compressed_tuple::CompressedTupleImpl<
          CompressedTuple<Ts...>, absl::index_sequence_for<Ts...>,
          internal_compressed_tuple::ShouldAnyUseBase<Ts...>()> {
 private:
  // 这里又定义了个模板 ElemT，因为在一个类的内部
  // 可以省略D参数，这里用 CompressedTuple 替换了
  template <int I>
  using ElemT = internal_compressed_tuple::ElemT<CompressedTuple, I>;

  template <int I>
  using StorageT = internal_compressed_tuple::Storage<ElemT<I>, I>;

 public:
  // There seems to be a bug in MSVC dealing in which using '=default' here will
  // cause the compiler to ignore the body of other constructors. The work-
  // around is to explicitly implement the default constructor.
#if defined(_MSC_VER)
  constexpr CompressedTuple() : CompressedTuple::CompressedTupleImpl() {}
#else
  constexpr CompressedTuple() = default;
#endif
  // 这里用 const Ts& 修饰，说明每个参数都会被拷贝构造或引用，而不会被 move
  explicit constexpr CompressedTuple(const Ts&... base)
      : CompressedTuple::CompressedTupleImpl(absl::in_place, base...) {}

  // 下面构造函数可用于移动构造
  // First：第一个模板参数，代表CompressedTuple中第一个元素的类型
  // Vs...：后续可变参数包，代表CompressedTuple中剩余元素的类型。
  template <typename First, typename... Vs,
            absl::enable_if_t<
                absl::conjunction<
                    // Ensure we are not hiding default copy/move constructors.
                    absl::negation<std::is_same<void(CompressedTuple),
                                                void(absl::decay_t<First>)>>,
                    internal_compressed_tuple::TupleItemsMoveConstructible<
                        CompressedTuple<Ts...>, First, Vs...>>::value,
                bool> = true>
  explicit constexpr CompressedTuple(First&& first, Vs&&... base)
      : CompressedTuple::CompressedTupleImpl(absl::in_place,
                                             std::forward<First>(first),
                                             std::forward<Vs>(base)...) {}

  template <int I>
  ElemT<I>& get() & {
    return StorageT<I>::get();
  }

  // constexpr：这是一个关键字，表明此函数的结果可以在编译时计算
  // 第一个 const 是返回类型的组成部分，它修饰 ElemT<I>&，表明返回的是一个常量引用
  template <int I>
  constexpr const ElemT<I>& get() const& {
    return StorageT<I>::get();
  }

  template <int I>
  ElemT<I>&& get() && {
    return std::move(*this).StorageT<I>::get();
  }

  template <int I>
  constexpr const ElemT<I>&& get() const&& {
    return std::move(*this).StorageT<I>::get();
  }
};

// Explicit specialization for a zero-element tuple
// (needed to avoid ambiguous overloads for the default constructor).
template <>
class ABSL_INTERNAL_COMPRESSED_TUPLE_DECLSPEC CompressedTuple<> {};

}  // namespace container_internal
ABSL_NAMESPACE_END
}  // namespace absl

#undef ABSL_INTERNAL_COMPRESSED_TUPLE_DECLSPEC

#endif  // ABSL_CONTAINER_INTERNAL_COMPRESSED_TUPLE_H_
