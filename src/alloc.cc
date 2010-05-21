#include "alloc.h"

namespace iv {
namespace core {

void* Malloced::New(std::size_t size) {
  void* result = std::malloc(size);
  if (result == NULL) {
    OutOfMemory();
  }
  return result;
}

void Malloced::Delete(void* p) {
  std::free(p);
}

void Malloced::OutOfMemory() {
  std::puts("FAIL");
  std::exit(EXIT_FAILURE);
}

Space::Space()
  : arena_(&init_arenas_[0]),
    start_malloced_(NULL),
    malloced_() {
  unsigned int c = 0;
  for (; c < kInitArenas-1; ++c) {
    init_arenas_[c].SetNext(&init_arenas_[c+1]);
  }
}

Space::~Space() {

  if (start_malloced_) {
    Arena *now = start_malloced_, *next = start_malloced_;
    while (now) {
      next = now->Next();
      delete now;
      now = next;
    }
  }
  std::for_each(malloced_.begin(), malloced_.end(), Freer());
}

void* Space::New(std::size_t size) {
  if ((size - 1) < kThreshold) {
    // small memory allocator
    void* result = arena_->New(size);
    if (result == NULL) {
      Arena *arena = arena_->Next();
      if (arena) {
        arena_ = arena;
      } else {
        NewArena();
        if (!start_malloced_) {
          start_malloced_ = arena_;
        }
      }
      result = arena_->New(size);
      if (result == NULL) {
        Malloced::OutOfMemory();
      }
    }
    return result;
  } else {
    // malloc as memory allocator
    void* result = Malloced::New(size);
    malloced_.push_back(result);
    return result;
  }
}

Arena* Space::NewArena() {
  Arena* arena = NULL;
  try {
    // TODO(Constellation): use Malloced class instead of new
    arena = new Arena();
    if (arena == NULL) {
      Malloced::OutOfMemory();
      return arena;
    }
  } catch(const std::bad_alloc& e) {
    Malloced::OutOfMemory();
    return arena;
  }
  arena_->SetNext(arena);
  arena_ = arena;
  return arena;
}

Pool::Pool()
  : start_(NULL),
    position_(NULL),
    next_(NULL) {
}

void Pool::Initialize(uintptr_t start, Pool* next) {
  start_ = start;
  position_ = start;
  next_ = next;
}

void* Pool::New(uintptr_t size) {
  if (((position_ + size) & 0xfffff000) == start_) {
    // in this pool
    uintptr_t result = position_;
    position_ += size;
    return reinterpret_cast<void*>(result);
  }
  return NULL;
}

Arena::Arena()
  : pools_(),
    result_(NULL),
    now_(&pools_[0]),
    start_(now_),
    next_(NULL) {
  result_ = Malloced::New(kArenaSize);
  uintptr_t address = reinterpret_cast<uintptr_t>(result_);

  uintptr_t pool_address = AlignOffset(address, Pool::kPoolSize);
  unsigned int c = 0;
  for (; c < kPoolNum-1; ++c) {
    pools_[c].Initialize(pool_address+c*Pool::kPoolSize, &pools_[c+1]);
  }
  pools_[kPoolNum-1].Initialize(
      pool_address+(kPoolNum-1)*Pool::kPoolSize, NULL);
}

Arena::~Arena() {
  Malloced::Delete(result_);
}

void* Arena::New(std::size_t raw_size) {
  uintptr_t size = AlignOffset(raw_size, kAlignment);
  void* result = now_->New(size);
  if (result == NULL) {
    now_ = now_->Next();
    while (now_) {
      result = now_->New(size);
      if (result == NULL) {
        now_ = now_->Next();
      } else {
        return result;
      }
    }
    return NULL;  // full up
  }
  return result;
}

} }  // namespace iv::core
