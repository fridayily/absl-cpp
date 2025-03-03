
struct Base {}; // empty class

struct Derived1 : Base
{
  int i;
};

int main()
{
  // the size of any object of empty class type is at least 1
  static_assert(sizeof(Base) >= 1);

  // empty base optimization applies
  static_assert(sizeof(Derived1) == sizeof(int));
}