#include "batcher.h"

namespace fernotron {

static bool reached(uint32_t nowMs, uint32_t dueMs) { return (int32_t)(nowMs - dueMs) >= 0; }

static uint8_t count(uint8_t mask) { return (uint8_t)__builtin_popcount(mask); }

void Batcher::addMember(uint8_t group, uint8_t member) {
  if (group >= 1 && group < SIZE && member >= 1 && member < SIZE) members_[group] |= (uint8_t)(1u << member);
}

void Batcher::clearMembers() {
  for (uint8_t &m : members_) m = 0;
}

void Batcher::add(uint8_t group, uint8_t member, Command cmd, uint32_t nowMs) {
  if (group >= SIZE || member >= SIZE) return;
  Slot &s = slots_[group][member];
  if (!s.used) {
    s.used = true;
    s.dueMs = nowMs + windowMs_;
  }
  s.cmd = cmd;
}

bool Batcher::pending() const {
  for (const auto &row : slots_) {
    for (const Slot &s : row) {
      if (s.used) return true;
    }
  }
  return false;
}

bool Batcher::anyDue(uint32_t nowMs) const {
  for (const auto &row : slots_) {
    for (const Slot &s : row) {
      if (s.used && reached(nowMs, s.dueMs)) return true;
    }
  }
  return false;
}

uint32_t Batcher::msUntilDue(uint32_t nowMs) const {
  bool found = false;
  uint32_t best = 0;
  for (const auto &row : slots_) {
    for (const Slot &s : row) {
      if (!s.used) continue;
      const uint32_t left = reached(nowMs, s.dueMs) ? 0 : s.dueMs - nowMs;
      if (!found || left < best) best = left;
      found = true;
    }
  }
  return best;
}

uint8_t Batcher::pendingMask(uint8_t group, Command cmd) const {
  uint8_t mask = 0;
  for (uint8_t m = 1; m < SIZE; m++) {
    const Slot &s = slots_[group][m];
    if (s.used && s.cmd == cmd) mask |= (uint8_t)(1u << m);
  }
  return mask;
}

void Batcher::clearMask(uint8_t group, uint8_t mask, Command cmd) {
  for (uint8_t m = 1; m < SIZE; m++) {
    Slot &s = slots_[group][m];
    if ((mask & (1u << m)) && s.used && s.cmd == cmd) s.used = false;
  }
}

bool Batcher::next(uint32_t nowMs, Frame &out) {
  if (!anyDue(nowMs)) return false;

  // Which command goes next: any STOP, otherwise the oldest command.
  const Slot *oldest = nullptr;
  bool stop = false;
  for (const auto &row : slots_) {
    for (const Slot &s : row) {
      if (!s.used) continue;
      if (s.cmd == Command::Stop) stop = true;
      if (!oldest || (int32_t)(s.dueMs - oldest->dueMs) < 0) oldest = &s;
    }
  }
  const Command cmd = stop ? Command::Stop : oldest->cmd;

  // Every configured group complete: one frame to all.
  uint8_t configured = 0;
  bool allComplete = true;
  for (uint8_t g = 1; g < SIZE; g++) {
    if (!members_[g]) continue;
    configured += count(members_[g]);
    if ((pendingMask(g, cmd) & members_[g]) != members_[g]) allComplete = false;
  }
  if (configured >= 2 && allComplete) {
    for (uint8_t g = 1; g < SIZE; g++) clearMask(g, members_[g], cmd);
    out = {0, 0, cmd, configured};
    return true;
  }

  // One complete group with at least two motors: one frame to that group.
  for (uint8_t g = 1; g < SIZE; g++) {
    if (count(members_[g]) < 2) continue;
    if ((pendingMask(g, cmd) & members_[g]) != members_[g]) continue;
    clearMask(g, members_[g], cmd);
    out = {g, 0, cmd, count(members_[g])};
    return true;
  }

  // Otherwise the oldest single command of this kind.
  Slot *pick = nullptr;
  uint8_t pickGroup = 0, pickMember = 0;
  for (uint8_t g = 0; g < SIZE; g++) {
    for (uint8_t m = 0; m < SIZE; m++) {
      Slot &s = slots_[g][m];
      if (!s.used || s.cmd != cmd) continue;
      if (!pick || (int32_t)(s.dueMs - pick->dueMs) < 0) {
        pick = &s;
        pickGroup = g;
        pickMember = m;
      }
    }
  }
  pick->used = false;
  out = {pickGroup, pickMember, cmd, 1};
  return true;
}

}  // namespace fernotron
