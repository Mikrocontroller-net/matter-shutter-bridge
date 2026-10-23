#pragma once

#include <stdint.h>

#include "message.h"

// Collects commands and merges identical ones, so a whole room or the whole
// house moves with one radio frame instead of one frame per shutter (~1.2 s
// each with repeats).
//
// A batch leaves once no command has arrived for `quietMs`, or `maxWaitMs`
// after its first command, whichever comes first. Alexa sends a routine's or
// group's commands one per shutter, about 200 ms apart, so a fixed window
// after the first command would split them.
//
// Merging relies on the configured membership being complete: a frame to
// (group g, member 0) reaches every motor paired to g on the 2411, and
// (0, 0) reaches every motor of every group. Only groups whose configured
// members all received the same command are merged.
//
// STOPs leave first. Merges are recomputed for every frame, so a STOP that
// arrives while earlier frames are on air still goes next.
namespace fernotron {

class Batcher {
 public:
  struct Frame {
    uint8_t group;   // 0 = all groups
    uint8_t member;  // 0 = all members of the group
    Command cmd;
    uint8_t merged;  // commands this frame stands for
  };

  Batcher(uint32_t quietMs, uint32_t maxWaitMs) : quietMs_(quietMs), maxWaitMs_(maxWaitMs) {}

  // Membership: which members (1..7) of which groups (1..7) are motors this
  // device controls.
  void addMember(uint8_t group, uint8_t member);
  void clearMembers();

  // Queues a command for `group`/`member` (0..7; 0 addresses all) and
  // extends the batch's quiet period, up to its maximum wait. A newer command
  // for the same address replaces the older one but keeps its place in line.
  // Commands added while a due batch is being sent join it without delay.
  void add(uint8_t group, uint8_t member, Command cmd, uint32_t nowMs);

  // Once the batch is due, hands out one frame at a time until the queue is
  // empty. Returns false when nothing is due.
  bool next(uint32_t nowMs, Frame &out);

  bool pending() const;
  uint32_t msUntilDue(uint32_t nowMs) const;  // 0 if something is due

 private:
  static constexpr uint8_t SIZE = 8;  // addresses 0..7

  struct Slot {
    bool used = false;
    Command cmd = Command::Stop;
    uint32_t addedMs = 0;  // order in which frames leave
  };

  bool due(uint32_t nowMs) const;
  uint8_t pendingMask(uint8_t group, Command cmd) const;
  void clearMask(uint8_t group, uint8_t mask, Command cmd);

  uint32_t quietMs_;
  uint32_t maxWaitMs_;
  uint32_t firstMs_ = 0;  // first command of the current batch
  uint32_t dueMs_ = 0;
  Slot slots_[SIZE][SIZE];
  uint8_t members_[SIZE] = {};  // bit m set: member m of group g is ours
};

}  // namespace fernotron
