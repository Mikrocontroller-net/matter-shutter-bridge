#pragma once

#include <stdint.h>

#include "message.h"

namespace fernotron {

/// Collects commands and merges identical ones, so a whole room or the whole
/// house moves with one radio frame instead of one frame per shutter (~1.2 s
/// each with repeats).
///
/// A batch leaves once no command has arrived for `quietMs`, or `maxWaitMs`
/// after its first command, whichever comes first. Alexa sends a routine's or
/// group's commands one per shutter, about 200 ms apart, so a fixed window
/// after the first command would split them.
///
/// Merging relies on the configured membership being complete: a frame to
/// (group g, member 0) reaches every motor paired to g on the 2411, and
/// (0, 0) reaches every motor of every group. Only groups whose configured
/// members all received the same command are merged.
///
/// STOPs leave first. Merges are recomputed for every frame handed out.
/// Not thread-safe; one task owns it.
class Batcher {
 public:
  /// One frame to transmit.
  struct Frame {
    uint8_t group;   ///< 0 addresses all groups.
    uint8_t member;  ///< 0 addresses all members of the group.
    Command cmd;
    uint8_t merged;  ///< How many queued commands this frame stands for.
  };

  /// @param quietMs   a batch leaves this long after its latest command.
  /// @param maxWaitMs ... but no later than this after its first command.
  Batcher(uint32_t quietMs, uint32_t maxWaitMs) : quietMs_(quietMs), maxWaitMs_(maxWaitMs) {}

  /// Records that member `member` (1..7) of group `group` (1..7) is a motor
  /// this device controls. Out-of-range values are ignored.
  void addMember(uint8_t group, uint8_t member);
  /// Forgets all members; nothing merges until they are added again.
  void clearMembers();

  /// Queues a command for `group`/`member` (0..7; 0 addresses all) and
  /// extends the batch's quiet period, up to its maximum wait.
  ///
  /// A newer command for the same address replaces the older one but keeps
  /// its place in line. Commands added while a due batch is being handed out
  /// join it without delay. Out-of-range addresses are ignored.
  void add(uint8_t group, uint8_t member, Command cmd, uint32_t nowMs);

  /// Hands out the next frame once the batch is due.
  /// @return false when nothing is due; call again until it returns false.
  bool next(uint32_t nowMs, Frame &out);

  /// True while any command is queued.
  bool pending() const;
  /// Milliseconds until the batch is due; 0 if it is due or nothing is queued.
  uint32_t msUntilDue(uint32_t nowMs) const;

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
