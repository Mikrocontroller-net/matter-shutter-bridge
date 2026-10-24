#include "interleaver.h"

namespace fernotron {

// Group or member 0 addresses all, so it overlaps every value.
static bool overlaps(const Batcher::Frame &a, const Batcher::Frame &b) {
  const bool group = a.group == 0 || b.group == 0 || a.group == b.group;
  const bool member = a.member == 0 || b.member == 0 || a.member == b.member;
  return group && member;
}

void Interleaver::removeAt(size_t i) {
  for (size_t j = i + 1; j < count_; j++) items_[j - 1] = items_[j];
  count_--;
  if (i < cursor_) cursor_--;
  if (cursor_ >= count_) cursor_ = 0;
}

bool Interleaver::add(const Transmission &t) {
  for (size_t i = 0; i < count_;) {
    if (overlaps(items_[i].frame, t.frame)) {
      removeAt(i);
    } else {
      i++;
    }
  }
  if (count_ == CAPACITY || t.copiesLeft == 0) return false;

  if (t.frame.cmd == Command::Stop) {
    for (size_t j = count_; j > cursor_; j--) items_[j] = items_[j - 1];
    items_[cursor_] = t;
  } else {
    items_[count_] = t;
  }
  count_++;
  return true;
}

bool Interleaver::next(Transmission &out) {
  if (count_ == 0) return false;
  items_[cursor_].copiesLeft--;
  out = items_[cursor_];
  if (out.copiesLeft == 0) {
    removeAt(cursor_);  // the cursor now points at the following command
  } else if (++cursor_ >= count_) {
    cursor_ = 0;
  }
  return true;
}

}  // namespace fernotron
