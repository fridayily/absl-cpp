// Copyright 2021 The Abseil Authors.
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

#ifndef ABSL_STRINGS_INTERNAL_CORD_INTERNAL_H_
#define ABSL_STRINGS_INTERNAL_CORD_INTERNAL_H_

#include <atomic>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <type_traits>

#include "absl/base/attributes.h"
#include "absl/base/config.h"
#include "absl/base/internal/endian.h"
#include "absl/base/internal/invoke.h"
#include "absl/base/optimization.h"
#include "absl/container/internal/compressed_tuple.h"
#include "absl/container/internal/container_memory.h"
#include "absl/meta/type_traits.h"
#include "absl/strings/string_view.h"

// We can only add poisoning if we can detect consteval executions.
#if defined(ABSL_HAVE_CONSTANT_EVALUATED) && \
    (defined(ABSL_HAVE_ADDRESS_SANITIZER) || \
     defined(ABSL_HAVE_MEMORY_SANITIZER))
#define ABSL_INTERNAL_CORD_HAVE_SANITIZER 1
#endif

#define ABSL_CORD_INTERNAL_NO_SANITIZE \
  ABSL_ATTRIBUTE_NO_SANITIZE_ADDRESS ABSL_ATTRIBUTE_NO_SANITIZE_MEMORY

namespace absl {
ABSL_NAMESPACE_BEGIN
namespace cord_internal {

// The overhead of a vtable is too much for Cord, so we roll our own subclasses
// using only a single byte to differentiate classes from each other - the "tag"
// byte.  Define the subclasses first so we can provide downcasting helper
// functions in the base class.
struct CordRep;
struct CordRepConcat;
struct CordRepExternal;
struct CordRepFlat;
struct CordRepSubstring;
struct CordRepCrc;
class CordRepBtree;

class CordzInfo;

// Default feature enable states for cord ring buffers
enum CordFeatureDefaults { kCordShallowSubcordsDefault = false };

extern std::atomic<bool> shallow_subcords_enabled;

inline void enable_shallow_subcords(bool enable) {
  shallow_subcords_enabled.store(enable, std::memory_order_relaxed);
}

// absl::InlinedVector是一个来自Abseil库的容器，它试图结合数组（
// 在小数据集时使用内联存储）和动态数组（大数据集时自动转换到堆上分配）的优点

// 在当前文件及cord.h头文件中使用的absl::InlinedVector实例，
// 它们各自的内联大小（inlined
// size）并不强制要求一致。尽管目前可能出于历史原因采用了
// 相同大小，但实际上每个InlinedVector根据其特定的使用场景和性能需求，
// 可以选择一个独立优化的内联大小值。
enum Constants {
  // The inlined size to use with absl::InlinedVector.

  // Note: The InlinedVectors in this file (and in cord.h) do not need to use
  // the same value for their inlined size. The fact that they do is historical.
  // It may be desirable for each to use a different inlined size optimized for
  // that InlinedVector's usage.
  //
  // TODO(jgm): Benchmark to see if there's a more optimal value than 47 for
  // the inlined vector size (47 exists for backward compatibility).
  kInlinedVectorSize = 47,

  // 此常量定义了数据复制的偏好阈值。当需要处理的数据块大小不超过这个值时
  // （即小于或等于511字节），倾向于直接复制数据；超过这个阈值时，
  // 则更倾向于使用引用计数或其他高效内存管理策略来避免直接复制带来的开销。
  // Prefer copying blocks of at most this size, otherwise reference count.
  kMaxBytesToCopy = 511
};

// Emits a fatal error "Unexpected node type: xyz" and aborts the program.
ABSL_ATTRIBUTE_NORETURN void LogFatalNodeType(CordRep* rep);

// Fast implementation of memmove for up to 15 bytes. This implementation is
// safe for overlapping regions. If nullify_tail is true, the destination is
// padded with '\0' up to 15 bytes.
template <bool nullify_tail = false>
inline void SmallMemmove(char* dst, const char* src, size_t n) {
  if (n >= 8) {
    assert(n <= 15);
    uint64_t buf1;
    uint64_t buf2;
    memcpy(&buf1, src, 8);
    memcpy(&buf2, src + n - 8, 8);
    if (nullify_tail) {
      memset(dst + 7, 0, 8);
    }
    // GCC 12 has a false-positive -Wstringop-overflow warning here.
#if ABSL_INTERNAL_HAVE_MIN_GNUC_VERSION(12, 0)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wstringop-overflow"
#endif
    memcpy(dst, &buf1, 8);
    memcpy(dst + n - 8, &buf2, 8);
#if ABSL_INTERNAL_HAVE_MIN_GNUC_VERSION(12, 0)
#pragma GCC diagnostic pop
#endif
  } else if (n >= 4) {
    uint32_t buf1;
    uint32_t buf2;
    memcpy(&buf1, src, 4);
    memcpy(&buf2, src + n - 4, 4);
    if (nullify_tail) {
      memset(dst + 4, 0, 4);
      memset(dst + 7, 0, 8);
    }
    memcpy(dst, &buf1, 4);
    memcpy(dst + n - 4, &buf2, 4);
  } else {
    if (n != 0) {
      dst[0] = src[0];
      dst[n / 2] = src[n / 2];
      dst[n - 1] = src[n - 1];
    }
    if (nullify_tail) {
      memset(dst + 7, 0, 8);
      memset(dst + n, 0, 8);
    }
  }
}

// Compact class for tracking the reference count and state flags for CordRep
// instances.  Data is stored in an atomic int32_t for compactness and speed.
class RefcountAndFlags {
 public:
  constexpr RefcountAndFlags() : count_{kRefIncrement} {}  // count_ 初始化为2
  struct Immortal {};  // 不朽的;长生的;流芳百世的;名垂千古的;永世的
  // 有参构造函数，指定引用计数不可修改
  explicit constexpr RefcountAndFlags(Immortal) : count_(kImmortalFlag) {}

  // Increments the reference count. Imposes no memory ordering.
  // Increment 函数通过原子操作增加对象的引用计数，保证了多线程环境下的安全，
  // 但不强制任何特定的内存顺序
  inline void Increment() {
    // 每次加 2，保障不修改最低位
    count_.fetch_add(kRefIncrement, std::memory_order_relaxed);
  }

  // Asserts that the current refcount is greater than 0. If the refcount is
  // greater than 1, decrements the reference count.
  //
  // Returns false if there are no references outstanding; true otherwise.
  // Inserts barriers to ensure that state written before this method returns
  // false will be visible to a thread that just observed this method returning
  // false.  Always returns false when the immortal bit is set.
  // immortal 位设置时永远返回false ?? 为什么 代码看是返回 true

  // 从代码看 kImmortalFlag 对象引用计数值为 1,3,5
  // 非kImmortalFlag 对象引用计数值为 2,4,6
  // count_ 刚初始化，值为2，返回 false
  inline bool Decrement() {
    int32_t refcount = count_.load(std::memory_order_acquire);
    // refcount & kImmortalFlag 说明是不可改变对象
    assert(refcount > 0 || refcount & kImmortalFlag);
    // 如果是 kImmortalFlag 类型，refcount= 1,3,5
    return refcount != kRefIncrement &&
           // fetch_sub 返回减法操作前 count 的值
           // 这里判断减法操作之前 count_!=kRefIncrement
           //  如果类型是 kImmortalFlag 类型  --> 1!=kRefIncrement 则返回 true
           // 如果是刚初始化的 kRefIncrement 类型，2!=kRefIncrement 则返回 false
           // 如果是进行了 Increment 操作的 kRefIncrement 类型，则返回
           // true，说明减成功
           count_.fetch_sub(kRefIncrement, std::memory_order_acq_rel) !=
               kRefIncrement;
  }

  // Same as Decrement but expect that refcount is greater than 1.
  inline bool DecrementExpectHighRefcount() {
    int32_t refcount =
        count_.fetch_sub(kRefIncrement, std::memory_order_acq_rel);
    assert(refcount > 0 || refcount & kImmortalFlag);
    return refcount != kRefIncrement;
  }

  // Returns the current reference count using acquire semantics.
  // 0010 count_ 每次加2
  // 0100
  // 0110

  // 0001
  // 0011
  // 0101
  inline size_t Get() const {
    // 最低位是标记位，右移1位后就是真实的计数
    return static_cast<size_t>(count_.load(std::memory_order_acquire) >>
                               kNumFlags);
  }

  // Returns whether the atomic integer is 1.
  // If the reference count is used in the conventional way, a
  // reference count of 1 implies that the current thread owns the
  // reference and no other thread shares it.
  // This call performs the test for a reference count of one, and
  // performs the memory barrier needed for the owning thread
  // to act on the object, knowing that it has exclusive access to the
  // object. Always returns false when the immortal bit is set.
  // IsOne 函数通过原子操作判断对象的引用计数是否为 1
  // immortal 被设置时总返回 false
  // 引用计数为1意味着当前线程独占该对象的引用，没有其他线程共享。
  inline bool IsOne() {
    // kImmortalFlag = 0001 ，表示不会被回收的对象
    // kRefIncrement = 0010 ，表示有引用计数器，会被回收的对象
    // 最后1位是标志位，引用计数器增加时，总数加 2，因为加1就表示对象不会被回收
    // 因此 引用计数等于 kRefIncrement 就表示该对象只有一个引用

    // 使用std::memory_order_acquire内存顺序加载 count_ 的值。
    // 这确保了在此加载操作之前的所有写操作对当前线程来说都是可见的。
    // 在多线程环境中，这作为一道屏障，确保了如果函数返回true，
    // 当前线程可以安全地假设自己拥有对象的独占访问权，
    // 因为在此之前的所有对对象状态的修改都已被观察到
    return count_.load(std::memory_order_acquire) == kRefIncrement;
  }

  bool IsImmortal() const {
    // 是否设置了 kImmortalFlag 位
    return (count_.load(std::memory_order_relaxed) & kImmortalFlag) != 0;
  }

 private:
  // We reserve the bottom bit for flag.
  // 保留最低有效位作为 flag
  // kImmortalBit indicates that this entity should never be collected; it is
  // used for the StringConstant constructor to avoid collecting immutable
  // constant cords.
  enum Flags {
    // 0001
    kNumFlags = 1,

    // 0001 表示资源永远不会被回收
    // 当这个标志被设置时，表明该实体是“永生”的，比如字符串常量，
    // 这样的设计可以确保这类不变的常量数据不会因为垃圾回收而被错误地释放。
    kImmortalFlag = 0x1,
    // 0010
    kRefIncrement = (1 << kNumFlags),
  };

  std::atomic<int32_t> count_;
};

// Various representations that we allow
enum CordRepKind {
  UNUSED_0 = 0,
  SUBSTRING = 1,
  CRC = 2,
  BTREE = 3,
  UNUSED_4 = 4,
  EXTERNAL = 5,

  // We have different tags for different sized flat arrays,
  // starting with FLAT, and limited to MAX_FLAT_TAG. The below values map to an
  // allocated range of 32 bytes to 256 KB. The current granularity is:
  // - 8 byte granularity for flat sizes in [32 - 512]
  // - 64 byte granularity for flat sizes in (512 - 8KiB]
  // - 4KiB byte granularity for flat sizes in (8KiB, 256 KiB]
  // If a new tag is needed in the future, then 'FLAT' and 'MAX_FLAT_TAG' should
  // be adjusted as well as the Tag <---> Size mapping logic so that FLAT still
  // represents the minimum flat allocation size. (32 bytes as of now).
  FLAT = 6,
  MAX_FLAT_TAG = 248
};

// There are various locations where we want to check if some rep is a 'plain'
// data edge, i.e. an external or flat rep. By having FLAT == EXTERNAL + 1, we
// can perform this check in a single branch as 'tag >= EXTERNAL'
// Note that we can leave this optimization to the compiler. The compiler will
// DTRT when it sees a condition like `tag == EXTERNAL || tag >= FLAT`.
// 这里的静态断言确保了两个常量FLAT和EXTERNAL之间满足特定的关系，
// 即FLAT的值必须正好比EXTERNAL的值大1。
// 具体来说，就是检查它是否是外部（EXTERNAL）类型或扁平（FLAT）类型。
// 通过设定FLAT等于EXTERNAL + 1，可以在单个条件分支中高效地完成这一检查，
// 即只需判断tag >= EXTERNAL即可同时覆盖EXTERNAL和FLAT两种情况。
static_assert(FLAT == EXTERNAL + 1, "EXTERNAL and FLAT not consecutive");

struct CordRep {
  // Result from an `extract edge` operation. Contains the (possibly changed)
  // tree node as well as the extracted edge, or {tree, nullptr} if no edge
  // could be extracted.
  // On success, the returned `tree` value is null if `extracted` was the only
  // data edge inside the tree, a data edge if there were only two data edges in
  // the tree, or the (possibly new / smaller) remaining tree with the extracted
  // data edge removed.
  struct ExtractResult {
    CordRep* tree;
    CordRep* extracted;
  };

  CordRep() = default;
  constexpr CordRep(RefcountAndFlags::Immortal immortal, size_t l)
      : length(l), refcount(immortal), tag(EXTERNAL), storage{} {}

  // 注释强调这三个字段的总大小需小于32字节，这是支持的最小平坦（flat）节点尺寸。
  // 这种紧凑布局对于性能至关重要，尤其是在内存敏感和频繁复制的场景中。
  // The following three fields have to be less than 32 bytes since
  // that is the smallest supported flat node size. Some code optimizations rely
  // on the specific layout of these fields. Notably: the non-trivial field
  // `refcount` being preceded by `length`, and being tailed by POD data
  // members only.
  // refcount 字段 在 length 和 tag(POD类型) 中间，
  // 这种布局有利于特定的代码优化，比如对齐和快速访问。
  // refcount 之后定义的成员变量一应该全部是 POD 类型
  // # LINT.IfChange
  size_t length;
  RefcountAndFlags refcount;
  // If tag < FLAT, it represents CordRepKind and indicates the type of node.
  // Otherwise, the node type is CordRepFlat and the tag is the encoded size.
  // 如果 tag < FLAT，则表示 CordRepKind 并且指示节点类型。
  // 如果 tag >= FLAT，则表示 CordRepFlat 并且标记是编码大小。
  uint8_t tag;

  // `storage` provides two main purposes: 下面应该是 CordRepFlat.Data()
  // - the starting point for FlatCordRep.Data() [flexible-array-member]
  // - 3 bytes of additional storage for use by derived classes.
  // The latter is used by CordrepConcat and CordRepBtree. CordRepConcat stores
  // a 'depth' value in storage[0], and the (future) CordRepBtree class stores
  // `height`, `begin` and `end` in the 3 entries. Otherwise we would need to
  // allocate room for these in the derived class, as not all compilers reuse
  // padding space from the base class (clang and gcc do, MSVC does not, etc)
  // 否则，我们需要在派生类中为它们分配空间，因为并非所有编译器都重用基类中的填充空间

  // 对于 CordRepFlat，storage 存储 FlatCordRep.Data() 的开始指针
  // 对于 CordrepConcat 在 storage[0] 存储深度
  // 对于 CordRepBtree 存储  `height`, `begin` and `end`
  uint8_t storage[3];
  // # LINT.ThenChange(cord_rep_btree.h:copy_raw)

  // Returns true if this instance's tag matches the requested type.
  constexpr bool IsSubstring() const { return tag == SUBSTRING; }
  constexpr bool IsCrc() const { return tag == CRC; }
  constexpr bool IsExternal() const { return tag == EXTERNAL; }
  constexpr bool IsFlat() const { return tag >= FLAT; }
  constexpr bool IsBtree() const { return tag == BTREE; }

  inline CordRepSubstring* substring();
  inline const CordRepSubstring* substring() const;
  inline CordRepCrc* crc();
  inline const CordRepCrc* crc() const;
  inline CordRepExternal* external();
  inline const CordRepExternal* external() const;
  inline CordRepFlat* flat();
  inline const CordRepFlat* flat() const;
  inline CordRepBtree* btree();
  inline const CordRepBtree* btree() const;

  // --------------------------------------------------------------------
  // Memory management

  // Destroys the provided `rep`.
  static void Destroy(CordRep* rep);

  // Increments the reference count of `rep`.
  // Requires `rep` to be a non-null pointer value.
  static inline CordRep* Ref(CordRep* rep);

  // Decrements the reference count of `rep`. Destroys rep if count reaches
  // zero. Requires `rep` to be a non-null pointer value.
  static inline void Unref(CordRep* rep);
};

struct CordRepSubstring : public CordRep {
  size_t start;  // Starting offset of substring in child
  CordRep* child;

  // Creates a substring on `child`, adopting a reference on `child`.
  // Requires `child` to be either a flat or external node, and `pos` and `n` to
  // form a non-empty partial sub range of `'child`, i.e.:
  // `n > 0 && n < length && n + pos <= length`
  static inline CordRepSubstring* Create(CordRep* child, size_t pos, size_t n);

  // Creates a substring of `rep`. Does not adopt a reference on `rep`.
  // Requires `IsDataEdge(rep) && n > 0 && pos + n <= rep->length`.
  // If `n == rep->length` then this method returns `CordRep::Ref(rep)`
  // If `rep` is a substring of a flat or external node, then this method will
  // return a new substring of that flat or external node with `pos` adjusted
  // with the original `start` position.
  static inline CordRep* Substring(CordRep* rep, size_t pos, size_t n);
};

// Type for function pointer that will invoke the releaser function and also
// delete the `CordRepExternalImpl` corresponding to the passed in
// `CordRepExternal`.
using ExternalReleaserInvoker = void (*)(CordRepExternal*);

// External CordReps are allocated together with a type erased releaser. The
// releaser is stored in the memory directly following the CordRepExternal.

// CordRepExternal 实例与其类型擦除的释放器（releaser）一起分配内存，
// 释放器存储在CordRepExternal实例之后的内存中。
// 这意味着每个外部数据节点都内嵌了其数据的生命周期管理逻辑
struct CordRepExternal : public CordRep {
  CordRepExternal() = default;
  explicit constexpr CordRepExternal(absl::string_view str)
      : CordRep(RefcountAndFlags::Immortal{}, str.size()),
        base(str.data()),
        releaser_invoker(nullptr) {}

  const char* base;
  // Pointer to function that knows how to call and destroy the releaser.
  ExternalReleaserInvoker releaser_invoker;

  // Deletes (releases) the external rep.
  // Requires rep != nullptr and rep->IsExternal()
  static void Delete(CordRep* rep);
};

// C++的函数重载解析遵循一定的规则来决定哪个函数是最合适的候选。
// 其中一个规则涉及最佳匹配原则，即当多个重载函数都能匹配调用时，
// 编译器会选择最具体匹配的那个。在使用Rank0和Rank1作为函数参数的情况下，
// 如果一个函数模板期待Rank1，而另一个可以接受Rank0（因为Rank1是Rank0的子类），
// 那么传入Rank1实例时，优先匹配期待Rank1的版本，因为它提供了更精确的匹配。

// Use go/ranked-overloads for dispatching.
struct Rank0 {};
struct Rank1 : Rank0 {};

// 期望Releaser类型能够接受一个absl::string_view类型的参数。
// 这通过typename = ::absl::base_internal::invoke_result_t<Releaser,
// absl::string_view>实现
// typename =语法用于模板参数列表中，为类型模板参数提供一个默认值
template <typename Releaser, typename = ::absl::base_internal::invoke_result_t<
                                 Releaser, absl::string_view>>
void InvokeReleaser(Rank1, Releaser&& releaser, absl::string_view data) {
  // 函数调用
  ::absl::base_internal::invoke(std::forward<Releaser>(releaser), data);
}

template <typename Releaser,
          typename = ::absl::base_internal::invoke_result_t<Releaser>>
void InvokeReleaser(Rank0, Releaser&& releaser, absl::string_view) {
  ::absl::base_internal::invoke(std::forward<Releaser>(releaser));
}

// We use CompressedTuple so that we can benefit from EBCO.
// CompressedTuple 是一个空类
// 额外的 int 参数是一个技巧，用于避免与潜在的复制或移动构造函数冲突，
// 同时仍能完美转发releaser参数

template <typename Releaser>
struct CordRepExternalImpl
    : public CordRepExternal,
      public ::absl::container_internal::CompressedTuple<Releaser> {
  // The extra int arg is so that we can avoid interfering with copy/move
  // constructors while still benefitting from perfect forwarding.

  // 这里继承了 CompressedTuple，其元素就存在一个 tuple 中
  // 即 release 函数的指针存在 tuple 中，可以用 get 取其中的值
  template <typename T>
  CordRepExternalImpl(T&& releaser, int)
      // 初始化一个类
      : CordRepExternalImpl::CompressedTuple(std::forward<T>(releaser)) {
    this->releaser_invoker = &Release;
  }

  // template关键字出现在这里是为了告诉编译器，接下来的get<0>()是一个模板成员函数的调用。
  // 这是因为当你在模板类的内部或者模板函数内部尝试访问
  // 另一个模板（如基类或成员中的模板）时，编译器需要额外的信息来解析这个调用。
  //  get(0) 从 CompressedTuple 取第一个元素
  ~CordRepExternalImpl() {
    InvokeReleaser(Rank1{}, std::move(this->template get<0>()),
                   absl::string_view(base, length));
  }

  static void Release(CordRepExternal* rep) {
    delete static_cast<CordRepExternalImpl*>(rep);
  }
};

inline CordRepSubstring* CordRepSubstring::Create(CordRep* child, size_t pos,
                                                  size_t n) {
  assert(child != nullptr);
  assert(n > 0);
  assert(n < child->length);
  assert(pos < child->length);
  assert(n <= child->length - pos);

  // TODO(b/217376272): Harden internal logic.
  // Move to strategical places inside the Cord logic and make this an assert.
  if (ABSL_PREDICT_FALSE(!(child->IsExternal() || child->IsFlat()))) {
    LogFatalNodeType(child);
  }

  CordRepSubstring* rep = new CordRepSubstring();
  rep->length = n;
  rep->tag = SUBSTRING;
  rep->start = pos;
  rep->child = child;
  return rep;
}

inline CordRep* CordRepSubstring::Substring(CordRep* rep, size_t pos,
                                            size_t n) {
  assert(rep != nullptr);
  assert(n != 0);
  assert(pos < rep->length);
  assert(n <= rep->length - pos);
  if (n == rep->length) return CordRep::Ref(rep);
  if (rep->IsSubstring()) {
    pos += rep->substring()->start;
    rep = rep->substring()->child;
  }
  CordRepSubstring* substr = new CordRepSubstring();
  substr->length = n;
  substr->tag = SUBSTRING;
  substr->start = pos;
  substr->child = CordRep::Ref(rep);
  return substr;
}

inline void CordRepExternal::Delete(CordRep* rep) {
  assert(rep != nullptr && rep->IsExternal());
  auto* rep_external = static_cast<CordRepExternal*>(rep);
  assert(rep_external->releaser_invoker != nullptr);
  rep_external->releaser_invoker(rep_external);
}

template <typename Str>
struct ConstInitExternalStorage {
  ABSL_CONST_INIT static CordRepExternal value;
};

// 初始化外部静态成员变量: 在模板结构体ConstInitExternalStorage外部，
// 通过显式地提供初始化表达式来定义并初始化静态成员变量value。
// 这一步是必要的，因为C++标准要求静态数据成员在类定义之外进行定义
template <typename Str>
ABSL_CONST_INIT CordRepExternal
    ConstInitExternalStorage<Str>::value(Str::value);

enum {
  kMaxInline = 15,
};

constexpr char GetOrNull(absl::string_view data, size_t pos) {
  return pos < data.size() ? data[pos] : '\0';
}

// We store cordz_info as 64 bit pointer value in little endian format. This
// guarantees that the least significant byte of cordz_info matches the first
// byte of the inline data representation in `data`, which holds the inlined
// size or the 'is_tree' bit.
using cordz_info_t = int64_t;

// Assert that the `cordz_info` pointer value perfectly overlaps the last half
// of `data` and can hold a pointer value.
static_assert(sizeof(cordz_info_t) * 2 == kMaxInline + 1, "");
static_assert(sizeof(cordz_info_t) >= sizeof(intptr_t), "");

// LittleEndianByte() creates a little endian representation of 'value', i.e.:
// a little endian value where the first byte in the host's representation
// holds 'value`, with all other bytes being 0.

// 用:
// 这个函数确保了无论在哪种字节序的机器上运行，都能得到一个符合小端字节序的表示，
// 其中第一个字节（在内存中的低地址部分）存储 value，其余字节为0
//  0x1234
// 小端
// 地址 0x00: 34
// 地址 0x01: 12
// 大端
// 地址 0x00: 12
// 地址 0x01: 34
static constexpr cordz_info_t LittleEndianByte(unsigned char value) {
#if defined(ABSL_IS_BIG_ENDIAN)
  return static_cast<cordz_info_t>(value) << ((sizeof(cordz_info_t) - 1) * 8);
#else
  return value;
#endif
}

class InlineData {
 public:
  // DefaultInitType forces the use of the default initialization constructor.
  enum DefaultInitType { kDefaultInit };

  // kNullCordzInfo holds the little endian representation of intptr_t(1)
  // This is the 'null' / initial value of 'cordz_info'. The null value
  // is specifically big endian 1 as with 64-bit pointers, the last
  // byte of cordz_info overlaps with the last byte holding the tag.
  static constexpr cordz_info_t kNullCordzInfo = LittleEndianByte(1);

  // kTagOffset contains the offset of the control byte / tag. This constant is
  // intended mostly for debugging purposes: do not remove this constant as it
  // is actively inspected and used by gdb pretty printing code.
  static constexpr size_t kTagOffset = 0;

  // Implement `~InlineData()` conditionally: we only need this destructor to
  // unpoison poisoned instances under *SAN, and it will only compile correctly
  // if the current compiler supports `absl::is_constant_evaluated()`.
#ifdef ABSL_INTERNAL_CORD_HAVE_SANITIZER
  ~InlineData() noexcept { unpoison(); }
#endif

  constexpr InlineData() noexcept { poison_this(); }

  explicit InlineData(DefaultInitType) noexcept : rep_(kDefaultInit) {
    poison_this();
  }

  explicit InlineData(CordRep* rep) noexcept : rep_(rep) {
    ABSL_ASSERT(rep != nullptr);
  }

  // Explicit constexpr constructor to create a constexpr InlineData
  // value. Creates an inlined SSO value if `rep` is null, otherwise
  // creates a tree instance value.
  constexpr InlineData(absl::string_view sv, CordRep* rep) noexcept
      : rep_(rep ? Rep(rep) : Rep(sv)) {
    poison();
  }

  constexpr InlineData(const InlineData& rhs) noexcept;
  InlineData& operator=(const InlineData& rhs) noexcept;
  friend void swap(InlineData& lhs, InlineData& rhs) noexcept;

  friend bool operator==(const InlineData& lhs, const InlineData& rhs) {
#ifdef ABSL_INTERNAL_CORD_HAVE_SANITIZER
    const Rep l = lhs.rep_.SanitizerSafeCopy();
    const Rep r = rhs.rep_.SanitizerSafeCopy();
    return memcmp(&l, &r, sizeof(l)) == 0;
#else
    return memcmp(&lhs, &rhs, sizeof(lhs)) == 0;
#endif
  }
  friend bool operator!=(const InlineData& lhs, const InlineData& rhs) {
    return !operator==(lhs, rhs);
  }

  // Poisons the unused inlined SSO data if the current instance
  // is inlined, else un-poisons the entire instance.
  // 如果当前实例是 inlined ,则毒化 未使用的 inlined SSO data
  constexpr void poison();

  // Un-poisons this instance.
  // 这个函数用于取消毒化（unpoison）当前实例的所有内存。
  // 换句话说，它清除之前设置的毒化标记，使得内存区域被视为有效且可安全使用
  constexpr void unpoison();

  // Poisons the current instance. This is used on default initialization.
  // 此函数专门用于在默认初始化时毒化当前实例。这意味着当对象被创建但没有明确地给予初始值时，
  // 它的内存会被自动毒化，以增强安全性，防止未初始化的对象被错误地访问或使用。
  constexpr void poison_this();

  // Returns true if the current instance is empty.
  // The 'empty value' is an inlined data value of zero length.
  bool is_empty() const { return rep_.tag() == 0; }

  // Returns true if the current instance holds a tree value.
  // rep 里面定义了一个union {data,as_tree} 最后1位是1，则是tree
  bool is_tree() const { return (rep_.tag() & 1) != 0; }

  // Returns true if the current instance holds a cordz_info value.
  // Requires the current instance to hold a tree value.
  bool is_profiled() const {
    assert(is_tree());
    return rep_.cordz_info() != kNullCordzInfo;
  }

  // Returns true if either of the provided instances hold a cordz_info value.
  // This method is more efficient than the equivalent `data1.is_profiled() ||
  // data2.is_profiled()`. Requires both arguments to hold a tree.
  // 如果 data1 或 data2 持有 cordz_info 值，则返回 true。
  static bool is_either_profiled(const InlineData& data1,
                                 const InlineData& data2) {
    assert(data1.is_tree() && data2.is_tree());
    return (data1.rep_.cordz_info() | data2.rep_.cordz_info()) !=
           kNullCordzInfo;
  }

  // Returns the cordz_info sampling instance for this instance, or nullptr
  // if the current instance is not sampled and does not have CordzInfo data.
  // Requires the current instance to hold a tree value.
  CordzInfo* cordz_info() const {
    assert(is_tree());
    intptr_t info = static_cast<intptr_t>(absl::little_endian::ToHost64(
        static_cast<uint64_t>(rep_.cordz_info())));
    assert(info & 1);
    return reinterpret_cast<CordzInfo*>(info -1);  // info 是整型，可以安全存储指针
  }

  // Sets the current cordz_info sampling instance for this instance, or nullptr
  // if the current instance is not sampled and does not have CordzInfo data.
  // Requires the current instance to hold a tree value.
  void set_cordz_info(CordzInfo* cordz_info) {
    assert(is_tree());
    // 将cordz_info指针转换为uintptr_t类型，并设置其最低位为1
    uintptr_t info = reinterpret_cast<uintptr_t>(cordz_info) | 1;
    rep_.set_cordz_info(
        static_cast<cordz_info_t>(absl::little_endian::FromHost64(info)));
  }

  // Resets the current cordz_info to null / empty.
  void clear_cordz_info() {
    assert(is_tree());
    rep_.set_cordz_info(kNullCordzInfo);
  }

  // Returns a read only pointer to the character data inside this instance.
  // Requires the current instance to hold inline data.
  const char* as_chars() const {
    assert(!is_tree());
    return rep_.as_chars();
  }

  // Returns a mutable pointer to the character data inside this instance.
  // Should be used for 'write only' operations setting an inlined value.
  // Applications can set the value of inlined data either before or after
  // setting the inlined size, i.e., both of the below are valid:
  //
  //   // Set inlined data and inline size
  //   memcpy(data_.as_chars(), data, size);
  //   data_.set_inline_size(size);
  //
  //   // Set inlined size and inline data
  //   data_.set_inline_size(size);
  //   memcpy(data_.as_chars(), data, size);
  //
  // It's an error to read from the returned pointer without a preceding write
  // if the current instance does not hold inline data, i.e.: is_tree() == true.
  char* as_chars() { return rep_.as_chars(); }

  // Returns the tree value of this value.
  // Requires the current instance to hold a tree value.
  CordRep* as_tree() const {
    assert(is_tree());
    return rep_.tree();
  }

  void set_inline_data(const char* data, size_t n) {
    ABSL_ASSERT(n <= kMaxInline);
    unpoison();
    rep_.set_tag(static_cast<int8_t>(n << 1));  // 设置tag，长度左移1位后保存
    SmallMemmove<true>(rep_.as_chars(), data, n);
    poison();
  }

  void copy_max_inline_to(char* dst) const {
    assert(!is_tree());
    memcpy(dst, rep_.SanitizerSafeCopy().as_chars(), kMaxInline);
  }

  // Initialize this instance to holding the tree value `rep`,
  // initializing the cordz_info to null, i.e.: 'not profiled'.
  void make_tree(CordRep* rep) {
    unpoison();
    rep_.make_tree(rep);
  }

  // Set the tree value of this instance to 'rep`.
  // Requires the current instance to already hold a tree value.
  // Does not affect the value of cordz_info.
  void set_tree(CordRep* rep) {
    assert(is_tree());
    rep_.set_tree(rep);
  }

  // Returns the size of the inlined character data inside this instance.
  // Requires the current instance to hold inline data.
  size_t inline_size() const { return rep_.inline_size(); }

  // Sets the size of the inlined character data inside this instance.
  // Requires `size` to be <= kMaxInline.
  // See the documentation on 'as_chars()' for more information and examples.
  void set_inline_size(size_t size) {
    unpoison();
    rep_.set_inline_size(size);
    poison();
  }

  // Compares 'this' inlined data  with rhs. The comparison is a straightforward
  // lexicographic comparison. `Compare()` returns values as follows:
  //
  //   -1  'this' InlineData instance is smaller
  //    0  the InlineData instances are equal
  //    1  'this' InlineData instance larger
  int Compare(const InlineData& rhs) const {
    return Compare(rep_.SanitizerSafeCopy(), rhs.rep_.SanitizerSafeCopy());
  }

 private:
  struct Rep {
    // See cordz_info_t for forced alignment and size of `cordz_info` details.
    struct AsTree {
      explicit constexpr AsTree(absl::cord_internal::CordRep* tree)
          : rep(tree) {}
      cordz_info_t cordz_info = kNullCordzInfo;
      absl::cord_internal::CordRep* rep;
    };

    explicit Rep(DefaultInitType) {}
    constexpr Rep() : data{0} {}
    constexpr Rep(const Rep&) = default;
    constexpr Rep& operator=(const Rep&) = default;

    explicit constexpr Rep(CordRep* rep) : as_tree(rep) {}

    explicit constexpr Rep(absl::string_view chars)
        : data{static_cast<char>((chars.size() << 1)),
               GetOrNull(chars, 0),
               GetOrNull(chars, 1),
               GetOrNull(chars, 2),
               GetOrNull(chars, 3),
               GetOrNull(chars, 4),
               GetOrNull(chars, 5),
               GetOrNull(chars, 6),
               GetOrNull(chars, 7),
               GetOrNull(chars, 8),
               GetOrNull(chars, 9),
               GetOrNull(chars, 10),
               GetOrNull(chars, 11),
               GetOrNull(chars, 12),
               GetOrNull(chars, 13),
               GetOrNull(chars, 14)} {}

    // Disable sanitizer as we must always be able to read `tag`.
    ABSL_CORD_INTERNAL_NO_SANITIZE
    int8_t tag() const { return reinterpret_cast<const int8_t*>(this)[0]; }
    // 该类只有一个成员变量 union  , 所以直接用 data 就行了
    void set_tag(int8_t rhs) { reinterpret_cast<int8_t*>(this)[0] = rhs; }

    char* as_chars() { return data + 1; }  // 第1个字节存 tag，之后存 data
    const char* as_chars() const { return data + 1; }

    // tag 字节第1为是1 说明是 tree
    bool is_tree() const { return (tag() & 1) != 0; }

    size_t inline_size() const {
      ABSL_ASSERT(!is_tree());
      // 存储size 时进行了左移
      return static_cast<size_t>(tag()) >> 1;
    }

    void set_inline_size(size_t size) {
      ABSL_ASSERT(size <= kMaxInline);
      set_tag(static_cast<int8_t>(size << 1));
    }

    CordRep* tree() const { return as_tree.rep; }
    void set_tree(CordRep* rhs) { as_tree.rep = rhs; }

    cordz_info_t cordz_info() const { return as_tree.cordz_info; }
    // 将 cordz_info 对象的指针地址 转为 int64 ,再 + 1
    void set_cordz_info(cordz_info_t rhs) { as_tree.cordz_info = rhs; }

    void make_tree(CordRep* tree) {
      as_tree.rep = tree;                   // 指针赋值
      as_tree.cordz_info = kNullCordzInfo;  // 标记时 tree 类型
    }

#ifdef ABSL_INTERNAL_CORD_HAVE_SANITIZER
    constexpr Rep SanitizerSafeCopy() const {
      if (!absl::is_constant_evaluated()) {
        Rep res;
        if (is_tree()) {
          res = *this;
        } else {
          res.set_tag(tag());
          memcpy(res.as_chars(), as_chars(), inline_size());
        }
        return res;
      } else {
        return *this;
      }
    }
#else
    constexpr const Rep& SanitizerSafeCopy() const { return *this; }
#endif

    // If the data has length <= kMaxInline, we store it in `data`, and
    // store the size in the first char of `data` shifted left + 1.
    // Else we store it in a tree and store a pointer to that tree in
    // `as_tree.rep` with a tagged pointer to make `tag() & 1` non zero.
    // 当这个union作为结构体的一个成员时，结构体的总大小还会包括其他成员的大小
    // 以及可能的内存对齐开销。结构体的总大小至少会包含这个union的最大成员大小，
    // 同时也会根据结构体内成员的排列和特定编译器及平台的对齐规则进行适当的内存对齐，
    // 以优化访问速度或满足硬件要求
    union {
      char data[kMaxInline + 1];
      AsTree as_tree;
    };

    // TODO(b/145829486): see swap(InlineData, InlineData) for more info.
    inline void SwapValue(Rep rhs, Rep& refrhs) {
      memcpy(&refrhs, this, sizeof(*this));
      memcpy(this, &rhs, sizeof(*this));
    }
  };

  // Private implementation of `Compare()`
  static inline int Compare(const Rep& lhs, const Rep& rhs) {
    uint64_t x, y;
    memcpy(&x, lhs.as_chars(), sizeof(x));
    memcpy(&y, rhs.as_chars(), sizeof(y));
    if (x == y) {
      memcpy(&x, lhs.as_chars() + 7, sizeof(x));
      memcpy(&y, rhs.as_chars() + 7, sizeof(y));
      if (x == y) {
        if (lhs.inline_size() == rhs.inline_size()) return 0;
        return lhs.inline_size() < rhs.inline_size() ? -1 : 1;
      }
    }
    x = absl::big_endian::FromHost64(x);
    y = absl::big_endian::FromHost64(y);
    return x < y ? -1 : 1;
  }

  Rep rep_;
};

static_assert(sizeof(InlineData) == kMaxInline + 1, "");

#ifdef ABSL_INTERNAL_CORD_HAVE_SANITIZER

constexpr InlineData::InlineData(const InlineData& rhs) noexcept
    : rep_(rhs.rep_.SanitizerSafeCopy()) {
  poison();
}

inline InlineData& InlineData::operator=(const InlineData& rhs) noexcept {
  unpoison();
  rep_ = rhs.rep_.SanitizerSafeCopy();
  poison();
  return *this;
}

constexpr void InlineData::poison_this() {
  if (!absl::is_constant_evaluated()) {
    container_internal::SanitizerPoisonObject(this);
  }
}

constexpr void InlineData::unpoison() {
  if (!absl::is_constant_evaluated()) {
    container_internal::SanitizerUnpoisonObject(this);
  }
}

constexpr void InlineData::poison() {
  if (!absl::is_constant_evaluated()) {
    if (is_tree()) {
      container_internal::SanitizerUnpoisonObject(this);
    } else if (const size_t size = inline_size()) {
      if (size < kMaxInline) {
        const char* end = rep_.as_chars() + size;
        container_internal::SanitizerPoisonMemoryRegion(end, kMaxInline - size);
      }
    } else {
      container_internal::SanitizerPoisonObject(this);
    }
  }
}

#else  // ABSL_INTERNAL_CORD_HAVE_SANITIZER

constexpr InlineData::InlineData(const InlineData&) noexcept = default;
inline InlineData& InlineData::operator=(const InlineData&) noexcept = default;

constexpr void InlineData::poison_this() {}
constexpr void InlineData::unpoison() {}
// 这里实现为空，什么都没做，因为编译器不支持
constexpr void InlineData::poison() {}

#endif  // ABSL_INTERNAL_CORD_HAVE_SANITIZER

inline CordRepSubstring* CordRep::substring() {
  assert(IsSubstring());
  return static_cast<CordRepSubstring*>(this);
}

inline const CordRepSubstring* CordRep::substring() const {
  assert(IsSubstring());
  return static_cast<const CordRepSubstring*>(this);
}

inline CordRepExternal* CordRep::external() {
  assert(IsExternal());
  return static_cast<CordRepExternal*>(this);
}

inline const CordRepExternal* CordRep::external() const {
  assert(IsExternal());
  return static_cast<const CordRepExternal*>(this);
}

inline CordRep* CordRep::Ref(CordRep* rep) {
  // ABSL_ASSUME is a workaround for
  // https://gcc.gnu.org/bugzilla/show_bug.cgi?id=105585
  ABSL_ASSUME(rep != nullptr);
  rep->refcount.Increment();
  return rep;
}

// 原子操作的成本： 原子操作（如原子减一）保证了在多线程环境中操作的完整性，防止了数据竞争。但是，
// 原子操作通常比非原子操作成本更高，因为它们需要硬件级别的锁或其他同步机制来确保操作的原子性。
// 这意味着原子操作可能涉及更多的 CPU 周期和潜在的缓存失效，从而影响性能

// 分支预测: 现代处理器使用分支预测技术来猜测程序中分支（if语句）的结果，
// 以便提前执行可能的路径，从而提高指令流水线的效率。如果分支预测准确，那么即使存在分支，
// 性能也不会受到太大影响。但是，如果预测错误，处理器可能需要回退并重新执行正确的路径，
// 这会导致性能下降

// 在 CordRep::Unref 函数中，作者选择避免在引用计数为1的情况下执行原子减一操作。
// 这是因为当引用计数为1时，原子减一操作实际上没有必要，因为减一后引用计数必然变为0，
// 此时可以安全地销毁对象。通过避免在这种情况下执行原子操作，可以节省原子操作的开销。

// 在 CordRep::Unref 函数中，作者选择避免在引用计数为1的情况下执行原子减一操作。
// 这是因为当引用计数为1时，原子减一操作实际上没有必要，因为减一后引用计数必然变为0，
// 此时可以安全地销毁对象。通过避免在这种情况下执行原子操作，可以节省原子操作的开销。
// 同时，代码中增加了一个额外的分支来检查引用计数是否为1，这可能会引入一些额外的分支预测
// 开销。但是，根据注释，作者认为避免原子操作的成本通常会超过这个额外分支带来的开销。这是因为：
//    1.分支预测通常很准确，尤其是在引用计数较高的情况下，分支很少会被采取。
//    2.原子操作的开销相对较高，特别是在高并发场景下。
inline void CordRep::Unref(CordRep* rep) {
  assert(rep != nullptr);
  // Expect refcount to be 0. Avoiding the cost of an atomic decrement should
  // typically outweigh the cost of an extra branch checking for ref == 1.
  // 引用计数大于1则条件判断成功
  if (ABSL_PREDICT_FALSE(!rep->refcount.DecrementExpectHighRefcount())) {
    Destroy(rep);
  }
}

inline void swap(InlineData& lhs, InlineData& rhs) noexcept {
  lhs.unpoison();
  rhs.unpoison();
  // TODO(b/145829486): `std::swap(lhs.rep_, rhs.rep_)` results in bad codegen
  // on clang, spilling the temporary swap value on the stack. Since `Rep` is
  // trivial, we can make clang DTRT by calling a hand-rolled `SwapValue` where
  // we pass `rhs` both by value (register allocated) and by reference. The IR
  // then folds and inlines correctly into an optimized swap without spill.
  lhs.rep_.SwapValue(rhs.rep_, rhs.rep_);
  rhs.poison();
  lhs.poison();
}

}  // namespace cord_internal

ABSL_NAMESPACE_END
}  // namespace absl
#endif  // ABSL_STRINGS_INTERNAL_CORD_INTERNAL_H_
