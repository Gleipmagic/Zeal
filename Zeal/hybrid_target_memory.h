#pragma once
// Last observed target for ONE current-zone spawn. Never expires solely because
// the actor is idle/stunned. Clear on selection change or authoritative empty
// assist response. Does not imply that an observed target is still selected.
#include <cstdint>

class HybridTargetMemory {
 public:
  enum class Kind { None, Melee, Animation, Assist };

  void select(uint16_t id) {
    if (id == source_) return;
    source_ = id;
    other_ = 0;
    timestamp_ = 0;
    last_passive_seen_ = 0;
    last_empty_reply_at_ = 0;
    kind_ = Kind::None;
  }

  // An old observation must never resurrect a target after /assist said none.
  void observe(uint16_t source, uint16_t other, Kind kind, uint32_t when) {
    if (!source || source != source_ || !other || other == 0xffff ||
        source == other || kind == Kind::None || kind == Kind::Assist ||
        (last_empty_reply_at_ && when <= last_empty_reply_at_) ||
        (last_passive_seen_ && when <= last_passive_seen_)) return;
    last_passive_seen_ = when;
    if (!timestamp_ || when >= timestamp_) {
      other_ = other;
      timestamp_ = when;
      kind_ = kind;
    }
  }

  // The response is tied to the selection at REQUEST time, not reply time.
  // -1 (failed/no target) or 0 clears the last known target. Other invalid
  // values leave the last observation intact rather than guessing.
  void assisted(uint16_t source, int32_t other, uint32_t when, uint32_t sent_at) {
    if (!source || source != source_) return;
    // If an actor revealed a new target after this request was sent, its
    // later-observed activity takes priority over the older assist query.
    if (last_passive_seen_ && last_passive_seen_ > sent_at) return;
    if (other == -1 || other == 0) {
      other_ = 0;
      timestamp_ = when;
      last_empty_reply_at_ = when;
      kind_ = Kind::None;
      return;
    }
    if (other < 0 || other > 65534) return;
    other_ = static_cast<uint16_t>(other);
    timestamp_ = when;
    kind_ = Kind::Assist;
  }

  uint16_t other() const { return other_; }
  uint16_t source() const { return source_; }
  uint32_t timestamp() const { return timestamp_; }
  Kind kind() const { return kind_; }

 private:
  uint16_t source_ = 0;
  uint16_t other_ = 0;
  uint32_t timestamp_ = 0;
  uint32_t last_passive_seen_ = 0;
  uint32_t last_empty_reply_at_ = 0;
  Kind kind_ = Kind::None;
};
