#ifndef IV_CONVERSIONS_H_
#define IV_CONVERSIONS_H_
#include <cstdio>
#include <cmath>
#include <string>
#include <limits>
#include <tr1/array>
#include <tr1/cstdint>
#include "chars.h"
#include "dtoa.h"
#include "ustringpiece.h"
#include "none.h"
namespace iv {
namespace core {
namespace detail {
template<typename T>
class Conversions {
 public:
  static const double kNaN;
  static const int kMaxSignificantDigits = 772;
  static const std::string kInfinity;
  static const double DoubleToInt32_Two32;
  static const double DoubleToInt32_Two31;
  static const char* kHex;
  static const int kMaxDoubleToStringWithRadixBufferSize = 2200;
};
template<typename T>
const double Conversions<T>::kNaN = std::numeric_limits<double>::quiet_NaN();

template<typename T>
const std::string Conversions<T>::kInfinity = "Infinity";

template<typename T>
const double Conversions<T>::DoubleToInt32_Two32 = 4294967296.0;

template<typename T>
const double Conversions<T>::DoubleToInt32_Two31 = 2147483648.0;

template<typename T>
const char* Conversions<T>::kHex = "0123456789abcdefghijklmnopqrstuvwxyz";

}  // namespace iv::core::detail

typedef detail::Conversions<None> Conversions;

template<typename Iter>
inline double StringToDouble(Iter it, Iter last) {
  bool is_decimal = true;
  bool is_signed = false;
  bool is_sign_found = false;
  std::size_t pos = 0;
  int significant_digits = 0;
  int insignificant_digits = 0;
  std::tr1::array<char, Conversions::kMaxSignificantDigits+10> buffer;

  // empty string ""
  if (it == last) {
    return 0;
  }

  while (it != last &&
         (Chars::IsWhiteSpace(*it) || Chars::IsLineTerminator(*it))) {
    ++it;
  }

  // white space only "  "
  if (it == last) {
    return 0;
  }

  if (*it == '-') {
    buffer[pos++] = '-';
    ++it;
    is_signed = true;
    is_sign_found = true;
  } else if (*it == '+') {
    ++it;
    is_sign_found = true;
  }

  if (it == last) {
    return Conversions::kNaN;
  }

  if (Chars::IsDecimalDigit(*it)) {
    if (*it == '0') {
      ++it;
      if (it == last) {
        return 0;
      }
      if (*it == 'x' || *it == 'X') {
        if (is_sign_found) {
          return Conversions::kNaN;
        }
        is_decimal = false;
        buffer[pos++] = '0';
        buffer[pos++] = *it;
        ++it;
        ++significant_digits;
        if (it == last || !Chars::IsHexDigit(*it)) {
          return Conversions::kNaN;
        }
        // waste leading zero
        while (it != last && *it == '0') {
          ++it;
        }
        while (it != last && Chars::IsHexDigit(*it)) {
          if (significant_digits < Conversions::kMaxSignificantDigits) {
            buffer[pos++] = *it;
            ++it;
            ++significant_digits;
          } else {
            ++it;
          }
        }
      } else {
        // waste leading zero
        while (it != last && *it == '0') {
          ++it;
        }
      }
    }
    if (is_decimal) {
      while (it != last &&
             Chars::IsDecimalDigit(*it)) {
        if (significant_digits < Conversions::kMaxSignificantDigits) {
          buffer[pos++] = *it;
          ++significant_digits;
        } else {
          ++insignificant_digits;
        }
        ++it;
      }
      if (it != last && *it == '.') {
        buffer[pos++] = '.';
        ++it;
        while (it != last &&
               Chars::IsDecimalDigit(*it)) {
          if (significant_digits < Conversions::kMaxSignificantDigits) {
            buffer[pos++] = *it;
            ++significant_digits;
          }
          ++it;
        }
      }
    }
  } else {
    if (*it == '.') {
      buffer[pos++] = '.';
      ++it;
      while (it != last &&
             Chars::IsDecimalDigit(*it)) {
        if (significant_digits < Conversions::kMaxSignificantDigits) {
          buffer[pos++] = *it;
          ++significant_digits;
        }
        ++it;
      }
    } else {
      for (std::string::const_iterator inf_it = Conversions::kInfinity.begin(),
           inf_last = Conversions::kInfinity.end();
           inf_it != inf_last; ++inf_it, ++it) {
        if (it == last ||
            (*inf_it) != (*it)) {
          return Conversions::kNaN;
        }
      }
      // infinity
      while (it != last &&
             (Chars::IsWhiteSpace(*it) || Chars::IsLineTerminator(*it))) {
        ++it;
      }
      if (it == last) {
        if (is_signed) {
          return -std::numeric_limits<double>::infinity();
        } else {
          return std::numeric_limits<double>::infinity();
        }
      } else {
        return Conversions::kNaN;
      }
    }
  }

  // exponent part
  if (it != last && (*it == 'e' || *it == 'E')) {
    if (!is_decimal) {
      return Conversions::kNaN;
    }
    buffer[pos++] = *it;
    ++it;
    if (it == last) {
      return Conversions::kNaN;
    }
    if (*it == '+' || *it == '-') {
      buffer[pos++] = *it;
      ++it;
    }
    if (it == last) {
      return Conversions::kNaN;
    }
    // more than 1 decimal digit required
    if (!Chars::IsDecimalDigit(*it)) {
      return Conversions::kNaN;
    }
    int exponent = 0;
    do {
      if (exponent > 9999) {
        exponent = 9999;
      } else {
        exponent = exponent * 10 + (*it - '0');
      }
      ++it;
    } while (it != last && Chars::IsDecimalDigit(*it));
    exponent+=insignificant_digits;
    if (exponent > 9999) {
      exponent = 9999;
    }
    std::snprintf(buffer.data()+pos, 5, "%d", exponent);  // NOLINT
    pos+=4;
  }

  while (it != last &&
         (Chars::IsWhiteSpace(*it) || Chars::IsLineTerminator(*it))) {
    ++it;
  }

  if (it == last) {
    if (pos == 0) {
      // empty
      return 0;
    } else {
      buffer[pos++] = '\0';
      return std::strtod(buffer.data(), NULL);
    }
  } else {
    return Conversions::kNaN;
  }
}

inline double StringToDouble(const StringPiece& str) {
  return StringToDouble(str.begin(), str.end());
}

inline double StringToDouble(const UStringPiece& str) {
  return StringToDouble(str.begin(), str.end());
}

inline std::size_t StringToHash(const UStringPiece& x) {
  std::size_t len = x.size();
  std::size_t step = (len >> 5) + 1;
  std::size_t h = 0;
  for (std::size_t l1 = len; l1 >= step; l1 -= step) {
    h = h ^ ((h << 5) + (h >> 2) + x[l1-1]);
  }
  return h;
}

inline std::size_t StringToHash(const StringPiece& x) {
  std::size_t len = x.size();
  std::size_t step = (len >> 5) + 1;
  std::size_t h = 0;
  for (std::size_t l1 = len; l1 >= step; l1 -= step) {
    h = h ^ ((h << 5) + (h >> 2) + x[l1-1]);
  }
  return h;
}


inline int32_t DoubleToInt32(double d) {
  int32_t i = static_cast<int32_t>(d);
  if (static_cast<double>(i) == d) {
    return i;
  }
  if (!std::isfinite(d) || d == 0) {
    return 0;
  }
  if (d < 0 || d >= Conversions::DoubleToInt32_Two32) {
    d = std::fmod(d, Conversions::DoubleToInt32_Two32);
  }
  d = (d >= 0) ?
      std::floor(d) : std::ceil(d) + Conversions::DoubleToInt32_Two32;
  return static_cast<int32_t>(d >= Conversions::DoubleToInt32_Two31 ?
                              d - Conversions::DoubleToInt32_Two32 : d);
}

inline int32_t DoubleToUInt32(double d) {
  return static_cast<uint32_t>(d);
}

inline double DoubleToInteger(double d) {
  if (std::isnan(d)) {
    return 0;
  }
  if (!std::isfinite(d) || d == 0) {
    return d;
  }
  return std::floor(std::abs(d)) * (std::signbit(d) ? -1 : 1);
}

inline bool ConvertToUInt32(const UStringPiece& str, uint32_t* value) {
  uint16_t ch;
  *value = 0;
  UStringPiece::const_iterator it = str.begin();
  const UStringPiece::const_iterator last = str.end();
  if (it != last && *it != '0' && Chars::IsDecimalDigit(*it)) {
    ch = *it - '0';
    *value = ch;
  } else {
    return false;
  }
  ++it;
  uint32_t prev = *value;
  for (;it != last; ++it) {
    prev = *value;
    if (Chars::IsDecimalDigit(*it)) {
      ch = *it - '0';
      *value = ch + (prev * 10);
    } else {
      return false;
    }
  }
  return (prev < (std::numeric_limits<uint32_t>::max() / 10) ||
          ((prev == (std::numeric_limits<uint32_t>::max() / 10)) &&
           (ch < (std::numeric_limits<uint32_t>::max() % 10))));
}

template<typename T>
inline std::size_t DoubleToStringWithRadix(double v, int radix, T* buf) {
  static const int kMaxBufSize = 1100;
  std::tr1::array<
      char,
      Conversions::kMaxDoubleToStringWithRadixBufferSize> buffer;
  const bool is_negative = v < 0.0;
  if (is_negative) {
    v = -v;
  }
  double integer = std::floor(v);
  double decimal = v - integer;

  // integer part
  int integer_pos = kMaxBufSize - 1;
  do {
    buffer[integer_pos--] =
        Conversions::kHex[static_cast<std::size_t>(std::fmod(integer, radix))];
    integer /= radix;
  } while (integer >= 1.0);
  if (is_negative) {
    buffer[integer_pos--] = '-';
  }
  assert(integer_pos >= 0);

  // decimal part
  int decimal_pos = kMaxBufSize;
  if (decimal) {
    buffer[decimal_pos++] = '.';
    while ((decimal > 0.0) && (decimal_pos < (kMaxBufSize * 2))) {
      decimal *= radix;
      const std::size_t res = static_cast<std::size_t>(std::floor(decimal));
      buffer[decimal_pos++] = Conversions::kHex[res];
      decimal -= res;
    }
  }
  buf->assign(buffer.data() + integer_pos + 1,
              buffer.data() + decimal_pos);
  return decimal_pos - integer_pos - 1;  // size
}

inline std::string DoubleToStringWithRadix(double v, int radix) {
  std::string str;
  DoubleToStringWithRadix(v, radix, &str);
  return str;
}

} }  // namespace iv::core
#endif  // IV_CONVERSIONS_H_
