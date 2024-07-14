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

// This file declares INTERNAL parts of the Split API that are inline/templated
// or otherwise need to be available at compile time. The main abstractions
// defined in here are
//
//   - ConvertibleToStringView
//   - SplitIterator<>
//   - Splitter<>
//
// DO NOT INCLUDE THIS FILE DIRECTLY. Use this file by including
// absl/strings/str_split.h.
//
// IWYU pragma: private, include "absl/strings/str_split.h"

#ifndef ABSL_STRINGS_INTERNAL_STR_SPLIT_INTERNAL_H_
#define ABSL_STRINGS_INTERNAL_STR_SPLIT_INTERNAL_H_

#include <array>
#include <cstddef>
#include <initializer_list>
#include <iterator>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

#include "absl/base/macros.h"
#include "absl/base/port.h"
#include "absl/meta/type_traits.h"
#include "absl/strings/string_view.h"

#ifdef _GLIBCXX_DEBUG
#include "absl/strings/internal/stl_type_traits.h"
#endif  // _GLIBCXX_DEBUG

namespace absl {
ABSL_NAMESPACE_BEGIN
namespace strings_internal {

// This class is implicitly constructible from everything that absl::string_view
// is implicitly constructible from, except for rvalue strings.  This means it
// can be used as a function parameter in places where passing a temporary
// string might cause memory lifetime issues.
class ConvertibleToStringView {
 public:
  ConvertibleToStringView(const char* s)  // NOLINT(runtime/explicit)
      : value_(s) {}
  ConvertibleToStringView(char* s) : value_(s) {}  // NOLINT(runtime/explicit)
  ConvertibleToStringView(absl::string_view s)     // NOLINT(runtime/explicit)
      : value_(s) {}
  ConvertibleToStringView(const std::string& s)  // NOLINT(runtime/explicit)
      : value_(s) {}

  // Disable conversion from rvalue strings.
  ConvertibleToStringView(std::string&& s) = delete;
  ConvertibleToStringView(const std::string&& s) = delete;

  absl::string_view value() const { return value_; }

 private:
  absl::string_view value_;
};

// An iterator that enumerates the parts of a string from a Splitter. The text
// to be split, the Delimiter, and the Predicate are all taken from the given
// Splitter object. Iterators may only be compared if they refer to the same
// Splitter instance.
//
// This class is NOT part of the public splitting API.
template <typename Splitter>
class SplitIterator {
 public:
  // 标记为输入迭代器
  // 输入迭代器：只能读取，不能修改
  // 至少支持
  //    通过解引用获取元素的值
  //    增量操作
  //    比较两个迭代器是否相等
  using iterator_category = std::input_iterator_tag;
  using value_type = absl::string_view;
  using difference_type = ptrdiff_t;  // 两个指针的距离
  using pointer = const value_type*;
  using reference = const value_type&;

  enum State { kInitState, kLastState, kEndState };
  SplitIterator(State state, const Splitter* splitter)
      : pos_(0),
        state_(state),
        splitter_(splitter),
        delimiter_(splitter->delimiter()),
        predicate_(splitter->predicate()) {
    // Hack to maintain backward compatibility. This one block makes it so an
    // empty absl::string_view whose .data() happens to be nullptr behaves
    // *differently* from an otherwise empty absl::string_view whose .data() is
    // not nullptr. This is an undesirable difference in general, but this
    // behavior is maintained to avoid breaking existing code that happens to
    // depend on this old behavior/bug. Perhaps it will be fixed one day. The
    // difference in behavior is as follows:
    //   Split(absl::string_view(""), '-');  // {""}
    //   Split(absl::string_view(), '-');    // {}
    if (splitter_->text().data() == nullptr) {
      state_ = kEndState;
      pos_ = splitter_->text().size();
      return;
    }
    // 构造函数中状态如果是 kEndState
    if (state_ == kEndState) {
      pos_ = splitter_->text().size();
    } else {
      ++(*this); // 调用 SplitIterator& operator++()
    }
  }

  bool at_end() const { return state_ == kEndState; }

  reference operator*() const { return curr_; }
  pointer operator->() const { return &curr_; }

  // 前缀自增返回迭代器本身
  SplitIterator& operator++() {
    do {
      if (state_ == kLastState) {
        state_ = kEndState;
        return *this;
      }
      const absl::string_view text = splitter_->text();
      const absl::string_view d = delimiter_.Find(text, pos_); // 返回第n个分隔符
      if (d.data() == text.data() + text.size()) state_ = kLastState;
      curr_ = text.substr(pos_,
                          // 第 n 个分隔符的起始位置 - 第 n-1 个分隔符的结束位置
                          static_cast<size_t>(d.data() - (text.data() + pos_)));
      pos_ += curr_.size() + d.size(); // 当前split后的字符串的长度 + 分隔符的长度
    } while (!predicate_(curr_));
    return *this;
  }

  // 后缀自增
  SplitIterator operator++(int) {
    SplitIterator old(*this);
    ++(*this);
    return old;
  }

  friend bool operator==(const SplitIterator& a, const SplitIterator& b) {
    return a.state_ == b.state_ && a.pos_ == b.pos_;
  }

  friend bool operator!=(const SplitIterator& a, const SplitIterator& b) {
    return !(a == b);
  }

 private:
  size_t pos_;
  State state_;
  absl::string_view curr_;
  const Splitter* splitter_;
  typename Splitter::DelimiterType delimiter_;
  typename Splitter::PredicateType predicate_;
};

// HasMappedType<T>::value is true iff there exists a type T::mapped_type.
// 其中 HasMappedType<T>::value 会根据 T 是否有 mapped_type 成员类型而分别返回
// std::true_type 或 std::false_type。

//  absl::void_t<typename T::mapped_type>
// 如果 T 类型拥有 mapped_type 成员类型，整个表达式就等同于 void，不会引起编译错误。
// 如果 T 类型没有 mapped_type 成员类型，表达式将因SFINAE而失效，
//      不会影响模板的其他可能特化版本的匹配。
template <typename T, typename = void>
struct HasMappedType : std::false_type {};
template <typename T>
struct HasMappedType<T, absl::void_t<typename T::mapped_type>>
    : std::true_type {};

// HasValueType<T>::value is true iff there exists a type T::value_type.
template <typename T, typename = void>
struct HasValueType : std::false_type {};
// 这个模板可以用来判断一个类型是否类似于容器（如std::vector或std::set），
// 这些容器通常都有value_type成员来表示它们存储的元素类型
template <typename T>
struct HasValueType<T, absl::void_t<typename T::value_type>> : std::true_type {
};

// HasConstIterator<T>::value is true iff there exists a type T::const_iterator.
template <typename T, typename = void>
struct HasConstIterator : std::false_type {};
template <typename T>
struct HasConstIterator<T, absl::void_t<typename T::const_iterator>>
    : std::true_type {};

// HasEmplace<T>::value is true iff there exists a method T::emplace().
template <typename T, typename = void>
struct HasEmplace : std::false_type {};

// std::declval 用于在编译期获取一个类型的右值引用，而无需实际构造该类型的对象
// 通常与 decltype 结合使用，以推导表达式或函数调用的类型信息
// std::declval<Example>()产生了一个Example类型的右值引用，接着调用.emplace()方法，
// 但这一切都在类型系统层面进行，没有实际的函数调用或对象构造发生。
// decltype 则用来捕获这个调用的返回类型

// decltype(std::declval<T>().emplace()) 会返回emplace() 函数的返回类型

// int n = 0;
// decltype(n) m = n; m 是 int 类型  n 可以是一个函数
template <typename T>
struct HasEmplace<T, absl::void_t<decltype(std::declval<T>().emplace())>>
    : std::true_type {};

// IsInitializerList<T>::value is true iff T is an std::initializer_list. More
// details below in Splitter<> where this is used.
std::false_type IsInitializerListDispatch(...);  // default: No
template <typename T>
std::true_type IsInitializerListDispatch(std::initializer_list<T>*);

// IsInitializerListDispatch 的返回类型要么是 std::true_type 或者是 std::false_type
// 所以 IsInitializerList<T>::value 是一个 bool 类型
//    (因为会继承 std::true_type/std::false_type)
// 如果T是std::initializer_list类型，
// 将匹配第二个函数模板  IsInitializerListDispatch(std::initializer_list<T>*)
// 否则将匹配第一个默认函数 IsInitializerListDispatch(...)
template <typename T>
struct IsInitializerList
    : decltype(IsInitializerListDispatch(static_cast<T*>(nullptr))){};

// static_cast<T*>(nullptr) 只要T 是指针类型就会转换成功
// 如果T是一个指针类型，static_cast<T*>(nullptr)会成功转换。nullptr是一个零指针常量，
// 它可以安全地转换为任何指针类型，无论这个类型是指向对象的指针还是指向函数的指针。
// 转换过程不会引发运行时错误，因为nullptr总是表示一个空指针。
// 然而，需要注意的是，转换后的指针仍然是空指针，不能用来访问任何对象。





// A SplitterIsConvertibleTo<C>::type alias exists iff the specified condition
// is true for type 'C'.
//
// Restricts conversion to container-like types (by testing for the presence of
// a const_iterator member type) and also to disable conversion to an
// std::initializer_list (which also has a const_iterator). Otherwise, code
// compiled in C++11 will get an error due to ambiguous conversion paths (in
// C++11 std::vector<T>::operator= is overloaded to take either a std::vector<T>
// or an std::initializer_list<T>).

// 结构体继承，std::false_type 是一个结构体
template <typename C, bool has_value_type, bool has_mapped_type>
struct SplitterIsConvertibleToImpl : std::false_type {};

// std::is_constructible 是 C++11 标准库中的类型特征，
// 用于判断类型是否可以使用给定的参数类型列表进行构造。
// 如果容器C有 value_type 没有mapped_type，检查value_type是否能用absl::string_view构造

// 如果一个类MyClass能用(int,string) 构造，std::is_constructible(MyClass, int, string)
// 返回 true

template <typename C>
struct SplitterIsConvertibleToImpl<C, true, false>
    : std::is_constructible<typename C::value_type, absl::string_view> {};

template <typename C>
struct SplitterIsConvertibleToImpl<C, true, true>
    : absl::conjunction<
          std::is_constructible<typename C::key_type, absl::string_view>,
          std::is_constructible<typename C::mapped_type, absl::string_view>> {};


// !IsInitializerList<typename std::remove_reference<C>::type>::value:
// 确保类型C不是std::initializer_list的实例。

// HasValueType<C>::value && HasConstIterator<C>::value:
// 检查类型C是否有value_type和const_iterator，这是容器类通常应具有的特性。

// HasMappedType<C>::value: 检查类型C是否有mapped_type，
// 这通常是关联容器（如map或unordered_map）的特征

template <typename C>
struct SplitterIsConvertibleTo
    : SplitterIsConvertibleToImpl<
          C,
#ifdef _GLIBCXX_DEBUG
          !IsStrictlyBaseOfAndConvertibleToSTLContainer<C>::value &&
#endif  // _GLIBCXX_DEBUG
              !IsInitializerList<
                  typename std::remove_reference<C>::type>::value &&
              HasValueType<C>::value && HasConstIterator<C>::value,
          HasMappedType<C>::value> {
};

// 基础模版定义，没有特化的情况下，对于任意的StringType和Container，
// 默认情况下ShouldUseLifetimeBound的结果为false_type，即默认不适用生命周期绑定
template <typename StringType, typename Container, typename = void>
struct ShouldUseLifetimeBound : std::false_type {};

// 当以下两个条件同时满足时，这个特化版本将被选用：
// StringType是std::string类型。
// Container的value_type（即容器内元素的类型）是absl::string_view类型。
// 在这种情况下，ShouldUseLifetimeBound的结果为true_type，

// 表明应该对这种配置的Container使用生命周期绑定
// std::enable_if_t<bool> 第二个参数默认为 void
template <typename StringType, typename Container>
struct ShouldUseLifetimeBound<
    StringType, Container,
    std::enable_if_t<
        std::is_same<StringType, std::string>::value &&
        std::is_same<typename Container::value_type, absl::string_view>::value>>
    : std::true_type {};

template <typename StringType, typename First, typename Second>
using ShouldUseLifetimeBoundForPair = std::integral_constant<
    bool, std::is_same<StringType, std::string>::value &&
              (std::is_same<First, absl::string_view>::value ||
               std::is_same<Second, absl::string_view>::value)>;

// This class implements the range that is returned by absl::StrSplit(). This
// class has templated conversion operators that allow it to be implicitly
// converted to a variety of types that the caller may have specified on the
// left-hand side of an assignment.
//
// The main interface for interacting with this class is through its implicit
// conversion operators. However, this class may also be used like a container
// in that it has .begin() and .end() member functions. It may also be used
// within a range-for loop.
//
// Output containers can be collections of any type that is constructible from
// an absl::string_view.
//
// An Predicate functor may be supplied. This predicate will be used to filter
// the split strings: only strings for which the predicate returns true will be
// kept. A Predicate object is any unary functor that takes an absl::string_view
// and returns bool.
//
// The StringType parameter can be either string_view or string, depending on
// whether the Splitter refers to a string stored elsewhere, or if the string
// resides inside the Splitter itself.
template <typename Delimiter, typename Predicate, typename StringType>
class Splitter {
 public:
  using DelimiterType = Delimiter;
  using PredicateType = Predicate;
  using const_iterator = strings_internal::SplitIterator<Splitter>;
  using value_type = typename std::iterator_traits<const_iterator>::value_type;

  Splitter(StringType input_text, Delimiter d, Predicate p)
      : text_(std::move(input_text)),
        delimiter_(std::move(d)),
        predicate_(std::move(p)) {}

  absl::string_view text() const { return text_; }
  // 常量成员函数，const 表明这些成员函数不会修改对象的状态
  // 引用类型的返回值
  const Delimiter& delimiter() const { return delimiter_; }
  const Predicate& predicate() const { return predicate_; }

  // Range functions that iterate the split substrings as absl::string_view
  // objects. These methods enable a Splitter to be used in a range-based for
  // loop.  返回一个新的迭代器，即 const_iterator(const_iterator::kInitState, this)
  const_iterator begin() const { return {const_iterator::kInitState, this}; }
  const_iterator end() const { return {const_iterator::kEndState, this}; }

  // An implicit conversion operator that is restricted to only those containers
  // that the splitter is convertible to.
  // 这段代码定义了一个模板转换运算符（conversion operator），允许 Splitter
  // 类型的对象隐式转换为 Container 类型。

  // std::enable_if_t<条件，类型> 条件为 true，则函数的返回类型被定义为该类型
  // 添加为 false,不编译该函数
  // 整个模板表达式的意思是：只有当Container类型满足“生命周期绑定”的条件
  // 并且可以转换为Splitter对象时，这个模板才会被实例化
  template <
      typename Container,
      std::enable_if_t<ShouldUseLifetimeBound<StringType, Container>::value &&
                           SplitterIsConvertibleTo<Container>::value,
                       std::nullptr_t> = nullptr>
  // NOLINTNEXTLINE(google-explicit-constructor)

  // NOLINTNEXTLINE(google-explicit-constructor): 这是一个编译器指令，
  // 告诉静态分析工具不要因为这个转换运算符不是显式构造器而发出警告。
  // 通常，C++ 鼓励显式构造器以避免意外的类型转换，但在这个情况下，
  // 可能是因为转换运算符是有意设计的。
  // ABSL_ATTRIBUTE_LIFETIME_BOUND: 这是 Abseil 库提供的一个属性，
  // 用于标记这个函数或变量与对象的生命周期有关。这意味着转换后的容器可能依赖于 Splitter
  // 的生命周期，如果 Splitter 被销毁，容器可能会成为悬挂引用。

  // operator + 类型 自定义类型转换
  // Container 就是要隐式转换的类型名字，如 std::vector
  operator Container() const ABSL_ATTRIBUTE_LIFETIME_BOUND {
    return ConvertToContainer<Container, typename Container::value_type,
                              HasMappedType<Container>::value>()(*this);
  }

  template <
      typename Container,
      std::enable_if_t<!ShouldUseLifetimeBound<StringType, Container>::value &&
                           SplitterIsConvertibleTo<Container>::value,
                       std::nullptr_t> = nullptr>
  // NOLINTNEXTLINE(google-explicit-constructor)
  operator Container() const {
    // 调用私有成员函数，这里传递了模版参数 ，这里是一个自定义隐式转换 ，把对象本身转换成Container类型
    return ConvertToContainer<Container, typename Container::value_type,
                              HasMappedType<Container>::value>()(*this);
  }

  // Returns a pair with its .first and .second members set to the first two
  // strings returned by the begin() iterator. Either/both of .first and .second
  // will be constructed with empty strings if the iterator doesn't have a
  // corresponding value.
  template <typename First, typename Second,
            std::enable_if_t<
                ShouldUseLifetimeBoundForPair<StringType, First, Second>::value,
                std::nullptr_t> = nullptr>
  // NOLINTNEXTLINE(google-explicit-constructor)
  operator std::pair<First, Second>() const ABSL_ATTRIBUTE_LIFETIME_BOUND {
    return ConvertToPair<First, Second>();
  }

  template <typename First, typename Second,
            std::enable_if_t<!ShouldUseLifetimeBoundForPair<StringType, First,
                                                            Second>::value,
                             std::nullptr_t> = nullptr>
  // NOLINTNEXTLINE(google-explicit-constructor)
  operator std::pair<First, Second>() const {
    return ConvertToPair<First, Second>();
  }

 private:
  template <typename First, typename Second>
  std::pair<First, Second> ConvertToPair() const {
    absl::string_view first, second;
    auto it = begin();
    if (it != end()) {
      first = *it;
      if (++it != end()) {
        second = *it;
      }
    }
    return {First(first), Second(second)};
  }

  // ConvertToContainer is a functor converting a Splitter to the requested
  // Container of ValueType. It is specialized below to optimize splitting to
  // certain combinations of Container and ValueType.
  //
  // This base template handles the generic case of storing the split results in
  // the requested non-map-like container and converting the split substrings to
  // the requested type.
  // Container: 指定目标容器的类型，如std::vector、std::set等。
  // ValueType: 每个元素的类型，通常是从Splitter的元素转换而来。
  // is_map: 默认为false，如果为true，可能意味着Container是映射类型，
  // 代码可能需要进行不同的操作
  template <typename Container, typename ValueType, bool is_map = false>
  struct ConvertToContainer {
    // 重载的函数调用运算符，使得ConvertToContainer对象可以像函数一样被调用
    Container operator()(const Splitter& splitter) const {
      Container c;
      // std::inserter(c, c.end())返回一个插入迭代器，用于向c的末尾添加元素
      auto it = std::inserter(c, c.end());
      for (const auto& sp : splitter) {
        // ValueType 转换后每个元素的类型
        *it++ = ValueType(sp);
      }
      return c;
    }
  };

  // Partial specialization for a std::vector<absl::string_view>.
  //
  // Optimized for the common case of splitting to a
  // std::vector<absl::string_view>. In this case we first split the results to
  // a small array of absl::string_view on the stack, to reduce reallocations.
  template <typename A>
  struct ConvertToContainer<std::vector<absl::string_view, A>,
                            absl::string_view, false> {
    std::vector<absl::string_view, A> operator()(
        const Splitter& splitter) const {
      struct raw_view {
        const char* data;
        size_t size;
        operator absl::string_view() const {  // NOLINT(runtime/explicit)
          return {data, size};
        }
      };
      std::vector<absl::string_view, A> v;
      std::array<raw_view, 16> ar;
      for (auto it = splitter.begin(); !it.at_end();) {
        size_t index = 0;
        do {
          ar[index].data = it->data();
          ar[index].size = it->size();
          ++it;
        } while (++index != ar.size() && !it.at_end());
        // We static_cast index to a signed type to work around overzealous
        // compiler warnings about signedness.
        v.insert(v.end(), ar.begin(),
                 ar.begin() + static_cast<ptrdiff_t>(index)); // 插入时进行类型转换
      }
      return v;
    }
  };

  // Partial specialization for a std::vector<std::string>.
  //
  // Optimized for the common case of splitting to a std::vector<std::string>.
  // In this case we first split the results to a std::vector<absl::string_view>
  // so the returned std::vector<std::string> can have space reserved to avoid
  // std::string moves.
  template <typename A>
  struct ConvertToContainer<std::vector<std::string, A>, std::string, false> {
    std::vector<std::string, A> operator()(const Splitter& splitter) const {
      const std::vector<absl::string_view> v = splitter;
      return std::vector<std::string, A>(v.begin(), v.end());
    }
  };

  // Partial specialization for containers of pairs (e.g., maps).
  //
  // The algorithm is to insert a new pair into the map for each even-numbered
  // item, with the even-numbered item as the key with a default-constructed
  // value. Each odd-numbered item will then be assigned to the last pair's
  // value.
  template <typename Container, typename First, typename Second>
  struct ConvertToContainer<Container, std::pair<const First, Second>, true> {
    using iterator = typename Container::iterator;

    Container operator()(const Splitter& splitter) const {
      Container m;
      iterator it;
      bool insert = true;
      for (const absl::string_view sv : splitter) {
        // 一次向map 容器插入 k,v
        // absl::StrSplit("a,1,b,2,a,3", ',') => (a,3),(b,2)
        if (insert) {
          // 插入 key
          it = InsertOrEmplace(&m, sv);
        } else {
          // 插入 value
          it->second = Second(sv);
        }
        insert = !insert;
      }
      return m;
    }

    // Inserts the key and an empty value into the map, returning an iterator to
    // the inserted item. We use emplace() if available, otherwise insert().
    // 如果M 类型有 Emplace 方法，返回类型是迭代器
    template <typename M>
    static absl::enable_if_t<HasEmplace<M>::value, iterator> InsertOrEmplace(
        M* m, absl::string_view key) {
      // Use piecewise_construct to support old versions of gcc in which pair
      // constructor can't otherwise construct string from string_view.

      // std::piecewise_construct 这是一个标签，指示 emplace 函数使用分片构造的方式。
      // 这意味着键和值将分别从两个元组中构造，而不是从一个单一的参数列表中构造。
      // std::map<std::string,int> m;
      // m.emplace("key",42);

      // std::make_tuple(key) 将元素key 存于tuple中
      // std::tuple<>() 可以存任意类型数据

      // std::map 或 std::unordered_map 的 emplace 方法的
      // 返回类型是一个 std::pair<iterator, bool>
      // 迭代器 (iterator)：如果插入成功，迭代器指向容器中刚刚插入的新元素的位置。
      // 如果插入失败（因为该键已经存在），迭代器同样指向容器中已存在的那个键值对的位置。
      // 布尔值 (bool)：表示插入是否成功。
      // true 表示插入了一个新元素，false 表示没有插入新元素，因为容器中已经有相同的键
      return ToIter(m->emplace(std::piecewise_construct, std::make_tuple(key),
                               std::tuple<>()));
    }

    // 如果M没有 Emplace 方法，返回类型是迭代器
    template <typename M>
    static absl::enable_if_t<!HasEmplace<M>::value, iterator> InsertOrEmplace(
        M* m, absl::string_view key) {
      return ToIter(m->insert(std::make_pair(First(key), Second(""))));
    }

    static iterator ToIter(std::pair<iterator, bool> pair) {
      return pair.first;
    }
    static iterator ToIter(iterator iter) { return iter; }
  };

  StringType text_;
  Delimiter delimiter_;
  Predicate predicate_;
};

}  // namespace strings_internal
ABSL_NAMESPACE_END
}  // namespace absl

#endif  // ABSL_STRINGS_INTERNAL_STR_SPLIT_INTERNAL_H_
