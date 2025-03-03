#include <gtest/gtest.h>

#include <chrono>
#include <iostream>
#include <type_traits>

// 检查类型是否为整数
template <typename T>
using is_integer = std::is_integral<T>;

// 使用std::enable_if_t的例子，仅对整数类型有效
template <typename T>
std::enable_if_t<is_integer<T>::value, long> print_number(T num) {
  std::cout << "The number is: " << num << std::endl;
  return 1L;
}

template <typename A>
using StringVector = std::vector<std::string, A>;

TEST(EnableIfTest, BasicTest) { EXPECT_EQ(print_number(5), 1L); }

TEST(EnableIfTest, BasicTest2) { std::cout << "--" << std::endl; }

// 测试模板定义一个参数，但是不传入参数，未成功
// TEST(TempTest, BasicTest){
//
//  StringVector<> default_vec;
//  default_vec.push_back("Hello");
//  default_vec.push_back("World");
//
//}

// 链表节点
struct ListNode {
  int value;
  ListNode* next;
};

// 前向迭代器类
class ForwardIterator {
 public:
  explicit ForwardIterator(ListNode* node) : current(node) {}

  // 增量操作  ++i
  ForwardIterator& operator++() {
    current = current->next;
    return *this;
  }

  // i++
  ForwardIterator operator++(int) {
    ForwardIterator temp(*this);
    operator++();
    return temp;
  }

  // 访问操作
  int& operator*() const { return current->value; }

  // 相等比较
  bool operator==(const ForwardIterator& other) const {
    return current == other.current;
  }

  // 不等比较
  bool operator!=(const ForwardIterator& other) const {
    return current != other.current;
  }

 private:
  ListNode* current;
};
// 链表类
class LinkedList {
 public:
  LinkedList() : head(nullptr), tail(nullptr) {}

  // 添加元素到链表尾部
  void push_back(int value) {
    ListNode* newNode = new ListNode{value, nullptr};
    if (tail) {
      tail->next = newNode;
    } else {
      head = newNode;
    }
    tail = newNode;
  }

  // 创建并返回开始迭代器
  ForwardIterator begin() const { return ForwardIterator(head); }

  // 创建并返回结束迭代器
  ForwardIterator end() const { return ForwardIterator(nullptr); }

 private:
  ListNode* head;
  ListNode* tail;
};

TEST(IteratorTest, BasicTest) {
  LinkedList list;
  list.push_back(1);
  list.push_back(2);
  list.push_back(3);

  for (ForwardIterator it = list.begin(); it != list.end(); ++it) {
    std::cout << *it << " ";
  }
}

class MyClass {
 public:
  MyClass() { std::cout << "MyClass constructor called." << std::endl; }
  ~MyClass() { std::cout << "MyClass destructor called." << std::endl; }
};

// :operator new 分配足够的内存来存放一个 MyClass 对象，
// 然后通过 placement new 在这块内存上构造对象。
// 当不再需要这个对象时，我们首先显式调用析构函数来清理资源，
// 然后使用 ::operator delete 释放内存。
TEST(MyClassTest, OperatorDelete) {
  void* memory = ::operator new(sizeof(MyClass));

  // 在分配的内存上构造对象（placement new）
  MyClass* myObj = new (memory) MyClass();

  // 使用对象...

  // 显式调用析构函数
  myObj->~MyClass();

  // 释放内存
  ::operator delete(memory);
}

// new 来分配内存并构造对象，然后使用 delete 来自动调用析构函数并释放内存

TEST(MyClassTest, OperatorDelete2) {
  // 动态分配并构造对象
  MyClass* myObj = new MyClass();

  // 使用对象...

  // 自动调用析构函数并释放内存
  delete myObj;
}

// https://www.cnblogs.com/xyf327/p/15133390.html
class Base {
 public:
  // 基类拥有虚析构函数
  Base() { std::cout << "Base constructor called" << std::endl; }

  virtual ~Base() { std::cout << "Base destructor called" << std::endl; }
  virtual void showInfo() { std::cout << "This is Base class" << std::endl; }
};

class Derived : public Base {
 public:
  // 派生类的资源，如这里假设有一个需要释放的成员
  int* data;

  Derived() {
    std::cout << "Derived constructor called" << std::endl;

    data = new int[100];  // 分配内存
  }

  // 派生类析构函数，也是虚的
  ~Derived() override {
    delete[] data;  // 释放派生类特有的资源
    std::cout << "Derived destructor called" << std::endl;
  }

  void showInfo() override {
    std::cout << "This is Derived class" << std::endl;
  }
};

/*
 * 动态调度（Dynamic Dispatch），也常被称为动态分发或晚期绑定（Late Binding），
 * 是面向对象编程中的一个核心概念，它允许在运行时决定调用哪个函数实现。这一机制是实现多态性
 * 的基础，使得代码可以在不知道具体对象类型的情况下调用对象的方法，尤其是当对象属于某个基类
 * 的派生类时。
 * 在多态的上下文中，当你通过基类的指针或引用来调用一个虚函数时，动态调度机制会根据对象的实际类
 *  型来选择并执行相应的派生类函数实现。这一过程涉及以下几个关键点：
 * 虚函数表（Virtual
 * Table,vtable）：许多实现动态调度的语言（如C++）会在每个具有虚函数的类
 *  中创建一个虚函数表。这个表是一个函数指针数组，存储了该类及其所有基类中虚函数的地址。
 * 对象中的vptr：每个对象实例中都有一个隐式的指针（称为vptr，虚指针），指向其所属类的虚函数表。
 *  这个指针在对象构造时被初始化。
 * 调用过程：当通过基类指针或引用来调用虚函数时，编译器生成的代码不是直接跳转到函数地址，
 *  而是通过对象的vptr查找虚函数表，然后根据虚函数在表中的位置找到并调用实际的函数实现。
 * 动态调度的优势在于它提高了代码的灵活性和可扩展性，使得程序可以在不修改现有代码的情况下，
 *  通过添加新的派生类来改变行为。然而，相比于静态调度（在编译时决定函数调用），
 *  动态调度可能会带来轻微的性能开销，因为它涉及到额外的查找步骤
 * */
TEST(VirtualTest, BasicTest) {
  Base* basePtr = new Derived();  //  基类指针指向派生类对象
  basePtr->showInfo();            // 调用派生类的showInfo()，多态行为
  delete basePtr;                 // 通过基类指针删除派生类对象
}

static int64_t GetCurrentTimeNanosFromSystem() {
  return std::chrono::duration_cast<std::chrono::nanoseconds>(
             std::chrono::system_clock::now() -
             std::chrono::system_clock::from_time_t(0))
      .count();
}

static int64_t GetCurrentTimeSecFromSystem() {
  return std::chrono::duration_cast<std::chrono::seconds>(
             std::chrono::system_clock::now() -
             std::chrono::system_clock::from_time_t(0))
      .count();
}

TEST(TimeTest, BasicTest) {
  std::cout << "Current time: " << GetCurrentTimeNanosFromSystem() << std::endl;
  std::cout << "Current time: " << GetCurrentTimeSecFromSystem() << std::endl;
  auto now = std::chrono::system_clock::now();
  std::time_t now_c = std::chrono::system_clock::to_time_t(now);
  std::cout << "Current time: " << std::ctime(&now_c);

  std::stringstream ss;
  ss << std::put_time(std::localtime(&now_c), "%Y-%m-%d %H:%M:%S");
  std::string formatted_time = ss.str();
  std::cout << "Custom format time: " << formatted_time << std::endl;
}

class Counter : public std::atomic<int64_t> {
 public:
  constexpr Counter() noexcept : std::atomic<int64_t>(0) {}
};

TEST(CounterTest, BasicTest) {
  Counter counter;
  std::cout << "sizeof(Counter): " << sizeof(Counter) << std::endl;

  counter.store(10);
  std::cout << "Counter value: " << counter.load() << std::endl;
  counter.fetch_add(5);
  std::cout << "Counter value: " << counter.load() << std::endl;
  counter.fetch_sub(3);
  std::cout << "Counter value: " << counter.load() << std::endl;
}

// 非类型模板参数允许你将整数、枚举值、指针或类类型作为模板参数传递。
// 这在模板元编程中非常有用，因为它允许你在编译时传递和操作常量值。
template <int N>
struct MyCounter {
  static constexpr int value = N;
};

TEST(MyCounter, BasicTest) {
  MyCounter<5> c5;    // N 在这里被 5 替换
  MyCounter<10> c10;  // N 在这里被 10 替换

  // 输出实例化的值
  static_assert(MyCounter<5>::value == 5);
  static_assert(MyCounter<10>::value == 10);
}

template <typename T, T... ints>
void print_sequence(std::integer_sequence<T, ints...> int_seq) {
  std::cout << "The sequence of size " << int_seq.size() << ": ";
  ((std::cout << ints << ' '), ...);
  std::cout << '\n';
}

// 展开左侧的折叠表达式
template <typename... Args>
auto sumleft(Args... args) {
  return (... + args);
}

// 展开右侧的折叠表达式
template <typename... Args>
auto sumright(Args... args) {
  return (args + ...);
}

TEST(InexTest, BasicTest) {
  print_sequence(std::integer_sequence<unsigned, 9, 2, 5, 1, 9, 1, 6>{});

  print_sequence(std::make_integer_sequence<int, 20>{});

  print_sequence(std::make_index_sequence<10>{});

  print_sequence(std::index_sequence_for<float, std::iostream, char>{});

  std::cout << sumleft(1, 2, 3, 4, 5) << std::endl;
  std::cout << sumright(1, 2, 3, 4, 5) << std::endl;

  using T = std::tuple<int, double, std::string>;
  std::cout << "T::type " << typeid(std::tuple_element<0, T>::type).name()
            << std::endl;
  std::cout << "T::type " << typeid(std::tuple_element<1, T>::type).name()
            << std::endl;
}

TEST(AnyTest, BasicTest) {
  std::any value = 42;  // 存储整数值

  if (value.has_value()) {
    int intValue = std::any_cast<int>(value);  // 安全地转换回int类型
    std::cout << "Value: " << intValue << std::endl;
  }

  value = std::string("Hello, World!");  // 存储字符串
  if (value.has_value()) {
    std::string strValue =
        std::any_cast<std::string>(value);  // 安全地转换回std::string类型
    std::cout << "Value: " << strValue << std::endl;
  }

  // 尝试错误的类型转换
  try {
    double doubleValue = std::any_cast<double>(value);
  } catch (const std::bad_any_cast& e) {
    std::cout << "Caught exception: " << e.what() << std::endl;
  }
}

template <class... Ts, class... Vs>
void print_types() {
  (void)sizeof...(Ts);  // 防止未使用的警告
  (void)sizeof...(Vs);
  std::cout << "Ts contains: " << sizeof...(Ts) << " types, ";
  std::cout << "Vs contains: " << sizeof...(Vs) << " types." << std::endl;
}
TEST(PrintTest, Base) {
  print_types<int, double, std::string>();  // 调用模板，传入Ts参数
  print_types<char*, float>();              // 调用模板，传入Vs参数
  print_types<int, double, std::string, char*, float>();  // 同时传入Ts和Vs参数
}

TEST(FalseType, Base) {
  std::false_type Or(std::initializer_list<std::false_type>);
  std::true_type Or(std::initializer_list<bool>);

  std::initializer_list<bool> list = {true, true};

  //  std::true_type a = Or(list);

  //  Or({{}, {}});
}

TEST(PointTest, Shared) {
  std::map<int, std::string> myMap;

  // 向 map 中插入元素
  myMap.insert({1, "one"});
  myMap.insert({2, "two"});

  // 创建一个指向已有 std::map 的智能指针
  // 创建一个指向已有 std::map 的智能指针
  auto mapPtr = std::make_shared<std::map<int, std::string>>();

  // 重置智能指针，使其指向 myMap
  mapPtr.reset(&myMap, [](std::map<int, std::string>* ptr) {});

  // 向 map 中插入元素
  mapPtr->insert({3, "three"});

  // 查找元素
  std::string value;
  if (mapPtr->find(1) != mapPtr->end()) {
    value = (*mapPtr)[1];
    std::cout << "Found: " << value << std::endl;
  } else {
    std::cout << "Not found." << std::endl;
  }

  // 遍历所有元素
  std::cout << "All elements:" << std::endl;
  for (const auto& pair : *mapPtr) {
    std::cout << pair.first << ": " << pair.second << std::endl;
  }
}

// 当您看到错误提示 "Missing 'typename' prior to dependent type name
// 'traits::value_type'" 时，这意味着您在使用一个依赖于模板参数的类型时没有
// 正确地使用 typename 关键字。在 C++ 中，当涉及到依赖于模板参数的类型时，
// 需要显式地使用 typename 关键字来声明它是类型的一部分。
template <typename T, typename Allocator>
void print_allocator_info(const Allocator& alloc) {
  using traits = std::allocator_traits<Allocator>;

  std::cout << "Max size: " << traits::max_size(alloc) << std::endl;
  std::cout << "Value type: " << typeid(typename traits::value_type).name()
            << std::endl;
}

template <typename T, typename Allocator>
void use_allocator(Allocator alloc) {
  using traits = std::allocator_traits<Allocator>;

  // 分配内存
  T* ptr = traits::allocate(alloc, 1);

  // 构造对象
  traits::construct(alloc, ptr, 42);

  // 输出对象
  std::cout << "Value: " << *ptr << std::endl;

  // 销毁对象
  traits::destroy(alloc, ptr);

  // 释放内存
  traits::deallocate(alloc, ptr, 1);
}

TEST(AllocatorTest, Base) {
  // 使用 std::allocator<int>
  std::allocator<int> alloc;

  // 打印分配器信息
  print_allocator_info<int>(alloc);

  // 使用分配器
  use_allocator<int>(alloc);
}

// TEST(BitCast,Base){
//   float orig_float = 3.14f;
//   int casted_int = std::bit_cast<int>(orig_float);
//   std::cout << "casted_orig " << casted_int << std::endl;
//
//   float casted_back_float = std::bit_cast<float>(casted_int);
//   std::cout << "Bit-casted back to float: " << casted_back_float <<
//   std::endl;
// }

#include <mutex>
#include <thread>

std::once_flag flag;
int x = 0;

void init() {
  std::cout << "Initialization started" << std::endl;
  x = 42;
  std::cout << "Initialization finished, x = " << x << std::endl;
}

void thread_func() {
  // 在每个线程中调用 std::call_once
  std::call_once(flag, init);
  std::cout << "x=" << x << std::endl;
}

TEST(CallOnce, Base) {
  std::thread t1(thread_func);
  std::thread t2(thread_func);

  t1.join();
  t2.join();

  std::cout << "Final value of x: " << x << std::endl;
}

// 自定义迭代器类
template <typename T>
class CustomIterator {
 public:
  using iterator_category = std::forward_iterator_tag;
  using value_type = T;
  using difference_type = std::ptrdiff_t;
  using pointer = T*;
  using reference = T&;

  CustomIterator(T* ptr) : ptr_(ptr) {}

  reference operator*() const { return *ptr_; }
  pointer operator->() const { return ptr_; }
  CustomIterator& operator++() {
    ++ptr_;
    return *this;
  }
  CustomIterator operator++(int) {
    CustomIterator tmp(*this);
    ++(*this);
    return tmp;
  }
  bool operator==(const CustomIterator& other) const {
    return ptr_ == other.ptr_;
  }
  bool operator!=(const CustomIterator& other) const {
    return !(*this == other);
  }

 private:
  T* ptr_;
};

TEST(CustomIter, demo) {
  int arr[] = {10, 20, 30, 40, 50};
  CustomIterator<int> it(arr);
  CustomIterator<int> end(arr + 5);

  std::cout << "1: " << *it << std::endl;
  std::cout << "1.operator->(): " << it.operator->() << std::endl;
  ++it;
  std::cout << "2: " << *it << std::endl;
  // 输出迭代器类别
  using iterator_category =
      typename std::iterator_traits<CustomIterator<int>>::iterator_category;
  std::cout << "迭代器类别: " << typeid(iterator_category).name() << std::endl;

  // 检查是否为 forward_iterator_tag
  if (std::is_same<iterator_category, std::forward_iterator_tag>::value) {
    std::cout << "这是一个前向迭代器" << std::endl;
  } else if (std::is_same<iterator_category,
                          std::bidirectional_iterator_tag>::value) {
    std::cout << "这是一个双向迭代器" << std::endl;
  } else if (std::is_same<iterator_category,
                          std::random_access_iterator_tag>::value) {
    std::cout << "这是一个随机访问迭代器" << std::endl;
  } else {
    std::cout << "其他类型的迭代器" << std::endl;
  }
}