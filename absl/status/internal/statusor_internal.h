// Copyright 2020 The Abseil Authors.
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
#ifndef ABSL_STATUS_INTERNAL_STATUSOR_INTERNAL_H_
#define ABSL_STATUS_INTERNAL_STATUSOR_INTERNAL_H_

#include <cstdint>
#include <type_traits>
#include <utility>

#include "absl/base/attributes.h"
#include "absl/base/nullability.h"
#include "absl/meta/type_traits.h"
#include "absl/status/status.h"
#include "absl/strings/string_view.h"
#include "absl/utility/utility.h"

namespace absl {
ABSL_NAMESPACE_BEGIN

template <typename T>
class ABSL_MUST_USE_RESULT StatusOr;

namespace internal_statusor {

// Detects whether `U` has conversion operator to `StatusOr<T>`, i.e. `operator
// StatusOr<T>()`.
// 用于检测类型U是否定义了一个转换到StatusOr<T>类型的转换操作符。
// 在这个默认版本中，该特质继承自std::false_type，意味着默认情况下假定类型U没有这样一个转换操作符。
template <typename T, typename U, typename = void>
struct HasConversionOperatorToStatusOr : std::false_type {};

// 作为一个编译时检查，用来验证类型U是否定义了一个到absl::StatusOr<T>的转换操作符

// char (*)[sizeof(...)]: 这是一个数组指针类型的参数，其大小由括号内的表达式决定。
// 这种类型的参数通常并不实际使用，而是作为SFINAE的一部分，用于检测表达式的有效性。
// 数组的大小在这里并不重要，关键在于表达式是否能成功求值。

// operator absl::StatusOr<T>() 尝试调用类型U到absl::StatusOr<T>的转换操作符。
// 如果这样的转换操作符存在，这个表达式就是合法的；如果不存在，则会导致编译错误。

// 该函数模板的存在主要用于在编译阶段检查类型U是否可以转换为absl::StatusOr<T>。
// 如果可以，那么整个表达式sizeof(std::declval<U>().operator absl::StatusOr<T>())
// 是有效的，函数模板实例化成功，尽管这个函数实例可能永远不会被调用。

// sizeof是一个编译时运算符，用于获取给定类型或表达式的大小（以字节为单位）。
// 在这里，它并不是用来获取实际的大小值（因为转换操作符的结果大小对我们来说通常不重要），
// 而是利用了sizeof表达式在编译时求值的特性。即使sizeof的结果未被使用，
// 编译器也会评估其内部表达式，从而触发对转换操作符存在的检查。
// 如果前面提到的转换操作符存在且有效，编译器将能够完成这个sizeof计算；
// 如果不存在，则会导致编译错误。
template <typename T, typename U>
void test(char (*)[sizeof(std::declval<U>().operator absl::StatusOr<T>())]);

// decltype表达式能够成功计算出一个类型（即检测到转换操作符存在时），
// 这个偏特化的HasConversionOperatorToStatusOr将继承自std::true_type，
// 表示类型U确实有转换到absl::StatusOr<T>的转换操作符。

// test<T, U>(0) 传入一个整数字面量0作为占位符参数（实际参数类型并不重要，因为不会真正调用该函数)
// test<T, U>(0)的类型将是一个有效的类型（尽管具体的类型是什么并不重要，因为我们只关心它是否能编译通过）
template <typename T, typename U>
struct HasConversionOperatorToStatusOr<T, U, decltype(test<T, U>(0))>
    : std::true_type {};

// Detects whether `T` is constructible or convertible from `StatusOr<U>`.

// std::is_constructible 用于检查类型T是否可以直接使用给定的参数类型列表Args...进行构造
// std::is_convertible<From, To>是C++标准库中的一个类型特征模板，
// 用于检查类型From是否可以隐式转换为类型To

// absl::disjunction: 这个元函数接受多个类型特质作为参数，
// 如果其中至少有一个特质为真（即返回std::true_type），
// 那么整个disjunction的结果也是真（std::true_type）。
// 这意味着只要T能通过任何形式（构造或转换）从任意形式的StatusOr<U>引用获得，
// IsConstructibleOrConvertibleFromStatusOr<T, U>就被定义为true_type。
template <typename T, typename U>
using IsConstructibleOrConvertibleFromStatusOr =
    absl::disjunction<std::is_constructible<T, StatusOr<U>&>,
                      std::is_constructible<T, const StatusOr<U>&>,
                      std::is_constructible<T, StatusOr<U>&&>,
                      std::is_constructible<T, const StatusOr<U>&&>,
                      std::is_convertible<StatusOr<U>&, T>,
                      std::is_convertible<const StatusOr<U>&, T>,
                      std::is_convertible<StatusOr<U>&&, T>,
                      std::is_convertible<const StatusOr<U>&&, T>>;

// Detects whether `T` is constructible or convertible or assignable from
// `StatusOr<U>`.
template <typename T, typename U>
using IsConstructibleOrConvertibleOrAssignableFromStatusOr =
    absl::disjunction<IsConstructibleOrConvertibleFromStatusOr<T, U>,
                      std::is_assignable<T&, StatusOr<U>&>,
                      std::is_assignable<T&, const StatusOr<U>&>,
                      std::is_assignable<T&, StatusOr<U>&&>,
                      std::is_assignable<T&, const StatusOr<U>&&>>;

// Detects whether direct initializing `StatusOr<T>` from `U` is ambiguous, i.e.
// when `U` is `StatusOr<V>` and `T` is constructible or convertible from `V`.
// 这段代码定义了一个递归模板元编程结构IsDirectInitializationAmbiguous，
// 旨在检测直接初始化StatusOr<T>从类型U是否会引起歧义，
// 特别是当U本身是一个StatusOr<V>且类型T可由V构造或转换时。

template <typename T, typename U>
struct IsDirectInitializationAmbiguous
    : public absl::conditional_t<
          // std::is_same<absl::remove_cvref_t<U>, U>::value:
          // 检查去除cvref后的类型是否与原始类型U相同。
          // 这是递归终止条件，意味着已经去除了所有可能的引用和cv限定符
          std::is_same<absl::remove_cvref_t<U>, U>::value, std::false_type,
          IsDirectInitializationAmbiguous<T, absl::remove_cvref_t<U>>> {};

template <typename T, typename V>
struct IsDirectInitializationAmbiguous<T, absl::StatusOr<V>>
    : public IsConstructibleOrConvertibleFromStatusOr<T, V> {};

// Checks against the constraints of the direction initialization, i.e. when
// `StatusOr<T>::StatusOr(U&&)` should participate in overload resolution.
template <typename T, typename U>
using IsDirectInitializationValid = absl::disjunction<
    // Short circuits if T is basically U.
    std::is_same<T, absl::remove_cvref_t<U>>,
    absl::negation<absl::disjunction<
        std::is_same<absl::StatusOr<T>, absl::remove_cvref_t<U>>,
        std::is_same<absl::Status, absl::remove_cvref_t<U>>,
        std::is_same<absl::in_place_t, absl::remove_cvref_t<U>>,
        IsDirectInitializationAmbiguous<T, U>>>>;

// This trait detects whether `StatusOr<T>::operator=(U&&)` is ambiguous, which
// is equivalent to whether all the following conditions are met:
// 1. `U` is `StatusOr<V>`.
// 2. `T` is constructible and assignable from `V`.
// 3. `T` is constructible and assignable from `U` (i.e. `StatusOr<V>`).
// For example, the following code is considered ambiguous:
// (`T` is `bool`, `U` is `StatusOr<bool>`, `V` is `bool`)
//   StatusOr<bool> s1 = true;  // s1.ok() && s1.ValueOrDie() == true
//   StatusOr<bool> s2 = false;  // s2.ok() && s2.ValueOrDie() == false
//   s1 = s2;  // ambiguous, `s1 = s2.ValueOrDie()` or `s1 = bool(s2)`?
template <typename T, typename U>
struct IsForwardingAssignmentAmbiguous
    : public absl::conditional_t<
          std::is_same<absl::remove_cvref_t<U>, U>::value, std::false_type,
          IsForwardingAssignmentAmbiguous<T, absl::remove_cvref_t<U>>> {};

template <typename T, typename U>
struct IsForwardingAssignmentAmbiguous<T, absl::StatusOr<U>>
    : public IsConstructibleOrConvertibleOrAssignableFromStatusOr<T, U> {};

// Checks against the constraints of the forwarding assignment, i.e. whether
// `StatusOr<T>::operator(U&&)` should participate in overload resolution.
template <typename T, typename U>
using IsForwardingAssignmentValid = absl::disjunction<
    // Short circuits if T is basically U.
    std::is_same<T, absl::remove_cvref_t<U>>,
    absl::negation<absl::disjunction<
        std::is_same<absl::StatusOr<T>, absl::remove_cvref_t<U>>,
        std::is_same<absl::Status, absl::remove_cvref_t<U>>,
        std::is_same<absl::in_place_t, absl::remove_cvref_t<U>>,
        IsForwardingAssignmentAmbiguous<T, U>>>>;

template <bool Value, typename T>
using Equality = std::conditional_t<Value, T, absl::negation<T>>;

// 用于在编译时验证类型T是否可以从类型U通过构造或转换得到，
// 同时考虑了显式/隐式转换、类型匹配、生命周期管理以及与
// 特定类型（如absl::Status和StatusOr）的兼容性限制
template <bool Explicit, typename T, typename U, bool Lifetimebound>
using IsConstructionValid = absl::conjunction<
    //  确保U到T的赋值是否满足指定的生命周期绑定要求
    Equality<Lifetimebound,
             type_traits_internal::IsLifetimeBoundAssignment<T, U>>,
    //  检查直接初始化的有效性，即使用右值引用U&&直接初始化T是否可行
    IsDirectInitializationValid<T, U&&>, std::is_constructible<T, U&&>,
    // 如果Explicit为false，则检查U&&是否可隐式转换为T；反之，则此条件不参与约束
    Equality<!Explicit, std::is_convertible<U&&, T>>,
    absl::disjunction<
        // 检查T是否与去除了cv限定符和引用的U相同，即是否是同一类型
        std::is_same<T, absl::remove_cvref_t<U>>,
        absl::conjunction<
            // 根据Explicit决定是否检查U&&不能构造或转换为absl::Status
            std::conditional_t<
                Explicit,
                absl::negation<std::is_constructible<absl::Status, U&&>>,
                absl::negation<std::is_convertible<U&&, absl::Status>>>,
            absl::negation<
                internal_statusor::HasConversionOperatorToStatusOr<T, U&&>>>>>;

template <typename T, typename U, bool Lifetimebound>
using IsAssignmentValid = absl::conjunction<
    Equality<Lifetimebound,
             type_traits_internal::IsLifetimeBoundAssignment<T, U>>,
    std::is_constructible<T, U&&>, std::is_assignable<T&, U&&>,
    absl::disjunction<
        std::is_same<T, absl::remove_cvref_t<U>>,
        absl::conjunction<
            absl::negation<std::is_convertible<U&&, absl::Status>>,
            absl::negation<HasConversionOperatorToStatusOr<T, U&&>>>>,
    IsForwardingAssignmentValid<T, U&&>>;

template <bool Explicit, typename T, typename U>
using IsConstructionFromStatusValid = absl::conjunction<
    absl::negation<std::is_same<absl::StatusOr<T>, absl::remove_cvref_t<U>>>,
    absl::negation<std::is_same<T, absl::remove_cvref_t<U>>>,
    absl::negation<std::is_same<absl::in_place_t, absl::remove_cvref_t<U>>>,
    Equality<!Explicit, std::is_convertible<U, absl::Status>>,
    std::is_constructible<absl::Status, U>,
    absl::negation<HasConversionOperatorToStatusOr<T, U>>>;


// std::conjunction是C++标准库中的一个元编程工具，它用于逻辑“与”操作。
// 这个模板元函数检查一系列类型特质表达式，并且只有当所有表达式都为真
// （即所有类型特质判断为真）时，它才返回std::true_type，否则返回std::false_type
template <bool Explicit, typename T, typename U, bool Lifetimebound,
          typename UQ>
using IsConstructionFromStatusOrValid = absl::conjunction<
    absl::negation<std::is_same<T, U>>,
    Equality<Lifetimebound,
             type_traits_internal::IsLifetimeBoundAssignment<T, U>>,
    std::is_constructible<T, UQ>,
    Equality<!Explicit, std::is_convertible<UQ, T>>,
    absl::negation<IsConstructibleOrConvertibleFromStatusOr<T, U>>>;

template <typename T, typename U, bool Lifetimebound>
using IsStatusOrAssignmentValid = absl::conjunction<
    absl::negation<std::is_same<T, absl::remove_cvref_t<U>>>,
    Equality<Lifetimebound,
             type_traits_internal::IsLifetimeBoundAssignment<T, U>>,
    std::is_constructible<T, U>, std::is_assignable<T, U>,
    absl::negation<IsConstructibleOrConvertibleOrAssignableFromStatusOr<
        T, absl::remove_cvref_t<U>>>>;

class Helper {
 public:
  // Move type-agnostic error handling to the .cc.
  static void HandleInvalidStatusCtorArg(absl::Nonnull<Status*>);
  ABSL_ATTRIBUTE_NORETURN static void Crash(const absl::Status& status);
};

// Construct an instance of T in `p` through placement new, passing Args... to
// the constructor.
// This abstraction is here mostly for the gcc performance fix.
template <typename T, typename... Args>
ABSL_ATTRIBUTE_NONNULL(1)
void PlacementNew(absl::Nonnull<void*> p, Args&&... args) {
  new (p) T(std::forward<Args>(args)...);
}

// Helper base class to hold the data and all operations.
// We move all this to a base class to allow mixing with the appropriate
// TraitsBase specialization.
template <typename T>
class StatusOrData {
  // 这意味着不同泛型参数的StatusOr类之间可以访问彼此的私有和保护成员，
  // 这对于实现类型转换和一致性操作非常有用，如在类型U可转换为类型T时，
  // 允许StatusOr<U>到StatusOr<T>的转换
  template <typename U>
  friend class StatusOrData;

 public:
  StatusOrData() = delete;

  StatusOrData(const StatusOrData& other) {
    if (other.ok()) {
      MakeValue(other.data_);
      MakeStatus();
    } else {
      MakeStatus(other.status_);
    }
  }

  StatusOrData(StatusOrData&& other) noexcept {
    if (other.ok()) {
      MakeValue(std::move(other.data_));
      MakeStatus();
    } else {
      MakeStatus(std::move(other.status_));
    }
  }

  template <typename U>
  explicit StatusOrData(const StatusOrData<U>& other) {
    if (other.ok()) {
      MakeValue(other.data_);
      MakeStatus();
    } else {
      MakeStatus(other.status_);
    }
  }

  template <typename U>
  explicit StatusOrData(StatusOrData<U>&& other) {
    if (other.ok()) {
      MakeValue(std::move(other.data_));
      MakeStatus();
    } else {
      MakeStatus(std::move(other.status_));
    }
  }
  // 构造函数，absl::StatusOr<std::unique_ptr<int>> 最终会在这里构造，StatusOr 继承自StatusOrData
  // 用智能指针构造，这 data_ 时一个智能指针
  template <typename... Args>
  explicit StatusOrData(absl::in_place_t, Args&&... args)
      : data_(std::forward<Args>(args)...) {
    MakeStatus();
  }

  explicit StatusOrData(const T& value) : data_(value) {
    MakeStatus();
  }
  explicit StatusOrData(T&& value) : data_(std::move(value)) {
    MakeStatus();
  }

  template <typename U,
            absl::enable_if_t<std::is_constructible<absl::Status, U&&>::value,
                              int> = 0>
  explicit StatusOrData(U&& v) : status_(std::forward<U>(v)) {
    EnsureNotOk();
  }

  StatusOrData& operator=(const StatusOrData& other) {
    if (this == &other) return *this;
    if (other.ok())
      Assign(other.data_);
    else
      AssignStatus(other.status_);
    return *this;
  }

  StatusOrData& operator=(StatusOrData&& other) {
    if (this == &other) return *this;
    if (other.ok())
      Assign(std::move(other.data_));
    else
      AssignStatus(std::move(other.status_));
    return *this;
  }

  ~StatusOrData() {
    if (ok()) {
      status_.~Status();
      data_.~T();
    } else {
      status_.~Status();
    }
  }

  template <typename U>
  void Assign(U&& value) {
    if (ok()) {
      data_ = std::forward<U>(value);
    } else {
      MakeValue(std::forward<U>(value));
      status_ = OkStatus();
    }
  }

  template <typename U>
  void AssignStatus(U&& v) {
    Clear();
    status_ = static_cast<absl::Status>(std::forward<U>(v));
    EnsureNotOk();
  }

  bool ok() const { return status_.ok(); }

  // StatusOrData 有两个成员函数，status_，dummy_
 protected:
  // status_ will always be active after the constructor.
  // We make it a union to be able to initialize exactly how we need without
  // waste.
  // Eg. in the copy constructor we use the default constructor of Status in
  // the ok() path to avoid an extra Ref call.
  // 可以根据不同的构造路径选择最适合的初始化方式。例如，在某些情况下，
  // 直接使用默认构造可能是最优选择，可以避免额外的引用计数操作（如Ref调用），
  // 这在处理智能指针或引用计数类型时很常见。
  union {
    Status status_;
  };

  // data_ is active iff status_.ok()==true
  struct Dummy {};
  union {
    // When T is const, we need some non-const object we can cast to void* for
    // the placement new. dummy_ is that object.
    // 当模板类型T是const类型时，由于const对象不能直接转换为void*进行放置新对象的
    // 内存分配（placement new），这个非const的dummy_就可以作为中介，
    // 先转换为void*，然后再用来在相同位置放置一个新的T类型的对象。
    Dummy dummy_;
    // 这里的T是一个模板参数，意味着这个联合体可以用来持有任何类型的对象。
    // 结合上面的dummy_成员，这种设计允许在需要时通过类型转换和
    // 放置新对象的方式来管理这块内存上的数据类型
    T data_;
  };

  void Clear() {
    if (ok()) data_.~T();
  }

  void EnsureOk() const {
    // ok 返回true 执行
    if (ABSL_PREDICT_FALSE(!ok())) Helper::Crash(status_);
  }

  void EnsureNotOk() {
    // ok 返回false 执行
    if (ABSL_PREDICT_FALSE(ok())) Helper::HandleInvalidStatusCtorArg(&status_);
  }

  // Construct the value (ie. data_) through placement new with the passed
  // argument.
  template <typename... Arg>
  void MakeValue(Arg&&... arg) {
    internal_statusor::PlacementNew<T>(&dummy_, std::forward<Arg>(arg)...);
  }

  // Construct the status (ie. status_) through placement new with the passed
  // argument.
  template <typename... Args>
  void MakeStatus(Args&&... args) {
    internal_statusor::PlacementNew<Status>(&status_,
                                            std::forward<Args>(args)...);
  }
};

// Helper base classes to allow implicitly deleted constructors and assignment
// operators in `StatusOr`. For example, `CopyCtorBase` will explicitly delete
// the copy constructor when T is not copy constructible and `StatusOr` will
// inherit that behavior implicitly.
template <typename T, bool = std::is_copy_constructible<T>::value>
struct CopyCtorBase {
  CopyCtorBase() = default;
  CopyCtorBase(const CopyCtorBase&) = default;
  CopyCtorBase(CopyCtorBase&&) = default;
  CopyCtorBase& operator=(const CopyCtorBase&) = default;
  CopyCtorBase& operator=(CopyCtorBase&&) = default;
};

template <typename T>
struct CopyCtorBase<T, false> {
  CopyCtorBase() = default;
  CopyCtorBase(const CopyCtorBase&) = delete;
  CopyCtorBase(CopyCtorBase&&) = default;
  CopyCtorBase& operator=(const CopyCtorBase&) = default;
  CopyCtorBase& operator=(CopyCtorBase&&) = default;
};

template <typename T, bool = std::is_move_constructible<T>::value>
struct MoveCtorBase {
  MoveCtorBase() = default;
  MoveCtorBase(const MoveCtorBase&) = default;
  MoveCtorBase(MoveCtorBase&&) = default;
  MoveCtorBase& operator=(const MoveCtorBase&) = default;
  MoveCtorBase& operator=(MoveCtorBase&&) = default;
};

template <typename T>
struct MoveCtorBase<T, false> {
  MoveCtorBase() = default;
  MoveCtorBase(const MoveCtorBase&) = default;
  MoveCtorBase(MoveCtorBase&&) = delete;
  MoveCtorBase& operator=(const MoveCtorBase&) = default;
  MoveCtorBase& operator=(MoveCtorBase&&) = default;
};

template <typename T, bool = std::is_copy_constructible<T>::value&&
                          std::is_copy_assignable<T>::value>
struct CopyAssignBase {
  CopyAssignBase() = default;
  CopyAssignBase(const CopyAssignBase&) = default;
  CopyAssignBase(CopyAssignBase&&) = default;
  CopyAssignBase& operator=(const CopyAssignBase&) = default;
  CopyAssignBase& operator=(CopyAssignBase&&) = default;
};

template <typename T>
struct CopyAssignBase<T, false> {
  CopyAssignBase() = default;
  CopyAssignBase(const CopyAssignBase&) = default;
  CopyAssignBase(CopyAssignBase&&) = default;
  CopyAssignBase& operator=(const CopyAssignBase&) = delete;
  CopyAssignBase& operator=(CopyAssignBase&&) = default;
};

template <typename T, bool = std::is_move_constructible<T>::value&&
                          std::is_move_assignable<T>::value>
struct MoveAssignBase {
  MoveAssignBase() = default;
  MoveAssignBase(const MoveAssignBase&) = default;
  MoveAssignBase(MoveAssignBase&&) = default;
  MoveAssignBase& operator=(const MoveAssignBase&) = default;
  MoveAssignBase& operator=(MoveAssignBase&&) = default;
};

template <typename T>
struct MoveAssignBase<T, false> {
  MoveAssignBase() = default;
  MoveAssignBase(const MoveAssignBase&) = default;
  MoveAssignBase(MoveAssignBase&&) = default;
  MoveAssignBase& operator=(const MoveAssignBase&) = default;
  MoveAssignBase& operator=(MoveAssignBase&&) = delete;
};

ABSL_ATTRIBUTE_NORETURN void ThrowBadStatusOrAccess(absl::Status status);

// Used to introduce jitter into the output of printing functions for
// `StatusOr` (i.e. `AbslStringify` and `operator<<`).
class StringifyRandom {
  enum BracesType {
    kBareParens = 0,
    kSpaceParens,
    kBareBrackets,
    kSpaceBrackets,
  };

  // Returns a random `BracesType` determined once per binary load.
  static BracesType RandomBraces() {
    static const BracesType kRandomBraces = static_cast<BracesType>(
        (reinterpret_cast<uintptr_t>(&kRandomBraces) >> 4) % 4);
    return kRandomBraces;
  }

 public:
  static inline absl::string_view OpenBrackets() {
    switch (RandomBraces()) {
      case kBareParens:
        return "(";
      case kSpaceParens:
        return "( ";
      case kBareBrackets:
        return "[";
      case kSpaceBrackets:
        return "[ ";
    }
    return "(";
  }

  static inline absl::string_view CloseBrackets() {
    switch (RandomBraces()) {
      case kBareParens:
        return ")";
      case kSpaceParens:
        return " )";
      case kBareBrackets:
        return "]";
      case kSpaceBrackets:
        return " ]";
    }
    return ")";
  }
};

}  // namespace internal_statusor
ABSL_NAMESPACE_END
}  // namespace absl

#endif  // ABSL_STATUS_INTERNAL_STATUSOR_INTERNAL_H_
