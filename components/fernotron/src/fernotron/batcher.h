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
/// A receiver ignores a frame that follows another frame closely, even one
/// for a different address; only a frame after silence is acted on. After a
/// frame has left the air (sent()), the next one is held for `gapMs`.
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
  /// @param gapMs     silence between the end of one frame and the next.
  Batcher(uint32_t quietMs, uint32_t maxWaitMs, uint32_t gapMs = 0)
      : quietMs_(quietMs), maxWaitMs_(maxWaitMs), gapMs_(gapMs) {}

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

  /// Hands out the next frame once the batch is due and the gap after the
  /// previous frame has passed.
  /// @return false when nothing is due; call again until it returns false.
  bool next(uint32_t nowMs, Frame &out);
  /// Records that the frame handed out last has left the air; the next one
  /// waits `gapMs` from `nowMs`.
  void sent(uint32_t nowMs);

  /// True while any command is queued.
  bool pending() const;
  /// Milliseconds until the next frame may leave; 0 if one may leave now or
  /// nothing is queued.
  uint32_t msUntilDue(uint32_t nowMs) const;

 private:
  static constexpr uint8_t SIZE = 8;  // addresses 0..7

  struct Slot {
    bool used = false;
    Command cmd = Command::Stop;
    uint32_t addedMs = 0;  // order in which frames leave
  };

  bool due(uint32_t nowMs) const;
  uint32_t gapLeft(uint32_t nowMs) const;
  uint8_t pendingMask(uint8_t group, Command cmd) const;
  void clearMask(uint8_t group, uint8_t mask, Command cmd);

  uint32_t quietMs_;
  uint32_t maxWaitMs_;
  uint32_t gapMs_;
  bool sentAny_ = false;
  uint32_t sentMs_ = 0;   // end of the latest frame on air
  uint32_t firstMs_ = 0;  // first command of the current batch
  uint32_t dueMs_ = 0;
  Slot slots_[SIZE][SIZE];
  uint8_t members_[SIZE] = {};  // bit m set: member m of group g is ours
};

}  // namespace fernotron
