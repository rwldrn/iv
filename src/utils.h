#ifndef _IV_UTILS_H_
#define _IV_UTILS_H_
#include <cstdio>
#include <cstdlib>
#include <cstddef>
#include <inttypes.h>
#include <limits>
#include <cassert>
#include <cmath>

#ifdef NDEBUG
#undef DEBUG
#else
#define DEBUG
#endif

namespace iv {
namespace core {
class Conv {
 public:
  static int32_t DoubleToInt32(double d);
  inline static int32_t DoubleToUInt32(double d) {
    return static_cast<uint32_t>(d);
  }
};

class Visitor;

class Visited {
 public:
  virtual ~Visited() { }
  virtual void Accept(Visitor*) { }  // NOLINT
};

class Visitor {
 public:
  Visitor() { }
  virtual ~Visitor() { }
  virtual void Visit(Visited*) { }  // NOLINT
};

// A macro to disallow the copy constructor and operator= functions
// This should be used in the private: declarations for a class
#define DISALLOW_COPY_AND_ASSIGN(TypeName) \
  TypeName(const TypeName&);               \
  void operator=(const TypeName&)

// alignment hack
// make struct using char (alignment 1 byte) + T (unknown alignment)
// shift 1 byte using char and get struct offset (offsetof returns T alignment)
template <typename T>
class AlignOf {
 private:
  struct Helper {
    char a_;
    T b_;
  };
 public:
  static const std::size_t value = offsetof(Helper, b_);
};

#define AlignOf(type) AlignOf<type>::value

#define AlignOffset(offset, alignment) \
    ((size_t)((offset) + ((alignment) - 1)) & ~(size_t)((alignment) - 1))

#define AlignType(offset, type) AlignOffset(offset, AlignOf(type))

template<class T>
T LowestOneBit(T value) {
  return value & (~value + 1u);
}

template <typename T>
std::size_t PtrAlignOf(T* value) {
  return static_cast<std::size_t>(
      LowestOneBit(reinterpret_cast<uintptr_t>(value)));
}

// ptr returnd by malloc guaranteed that any type can be set
// so ptr (void*) is the most biggest alignment of all
class Size {
 public:
  static const int KB = 1 << 10;
  static const int MB = KB << 10;
  static const int GB = MB << 10;
  static const int TB = GB << 10;

  static const int kCharSize     = sizeof(char);      // NOLINT
  static const int kShortSize    = sizeof(short);     // NOLINT
  static const int kIntSize      = sizeof(int);       // NOLINT
  static const int kDoubleSize   = sizeof(double);    // NOLINT
  static const int kPointerSize  = sizeof(void*);     // NOLINT
  static const int kIntptrSize   = sizeof(intptr_t);  // NOLINT

  static const int kCharAlign    = AlignOf(char);      // NOLINT
  static const int kShortAlign   = AlignOf(short);     // NOLINT
  static const int kIntAlign     = AlignOf(int);       // NOLINT
  static const int kDoubleAlign  = AlignOf(double);    // NOLINT
  static const int kPointerAlign = AlignOf(void*);     // NOLINT
  static const int kIntptrAlign  = AlignOf(intptr_t);  // NOLINT
};

} }  // namespace iv::core
#endif  // _IV_UTILS_H_
