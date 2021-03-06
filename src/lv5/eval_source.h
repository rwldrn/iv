#ifndef IV_LV5_EVAL_SOURCE_H_
#define IV_LV5_EVAL_SOURCE_H_
#include "none.h"
#include "jsstring.h"
#include "noncopyable.h"
namespace iv {
namespace lv5 {
namespace detail {
template<typename T>
class EvalSourceData {
 public:
  static const std::string kEvalSource;
};

template<typename T>
const std::string EvalSourceData<T>::kEvalSource = "(eval)";
}  // namespace iv::lv5::detail

typedef detail::EvalSourceData<core::None> EvalSourceData;

class EvalSource : public core::Noncopyable<EvalSource>::type {
 public:
  EvalSource(JSString* str)
    : source_(str) {
  }

  inline uc16 Get(std::size_t pos) const {
    assert(pos < size());
    return (*source_)[pos];
  }
  inline std::size_t size() const {
    return source_->size();
  }
  inline const std::string& filename() const {
    return EvalSourceData::kEvalSource;
  }
  inline core::UStringPiece SubString(
      std::size_t n, std::size_t len = std::string::npos) const {
    if (len == std::string::npos) {
      return core::UStringPiece((source_->data() + n), (source_->size() - n));
    } else {
      return core::UStringPiece((source_->data() + n), len);
    }
  }
 private:
  JSString* source_;
};

} }  // namespace iv::lv5
#endif  // IV_LV5_EVAL_SOURCE_H_
