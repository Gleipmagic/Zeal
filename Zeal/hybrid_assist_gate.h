#pragma once
// Experimental single-flight assist scheduler, isolated from the game for tests.
// Responses have no request ID: this limits automatic requests, but cannot
// absolutely distinguish a delayed automatic reply from a manual one.
#include <cstdint>
#include <string>
#include <utility>

class HybridAssistGate {
 public:
  bool pending() const { return pending_; }
  bool disabled_by_timeout() const { return disabled_by_timeout_; }
  uint16_t pending_source() const { return source_; }
  uint32_t pending_sent_at() const { return sent_at_; }
  bool has_deferred_manual() const { return !manual_command_.empty(); }
  bool initial_due() const { return initial_due_; }

  void reset() {
    pending_ = false;
    disabled_by_timeout_ = false;
    source_ = 0;
    selected_ = 0;
    initial_due_ = false;
    selected_at_ = 0;
    sent_at_ = 0;
    last_poll_ = 0;
    last_manual_at_ = 0;
    last_passive_at_ = 0;
    have_passive_ = false;
    have_poll_ = false;
    have_manual_ = false;
    manual_command_.clear();
  }

  // A new target selection schedules ONE initial query. Repeated render frames
  // with the same selection never schedule additional initial requests.
  void selection(uint16_t source, uint32_t now) {
    if (selected_ != source) {
      selected_ = source;
      selected_at_ = now;
      initial_due_ = source != 0;
      have_passive_ = false;
      last_passive_at_ = 0;
    }
  }

  // An observed combat target is better than a yet-UNSENT query. Do not erase
  // a pending request that was already transmitted; its reply can still arrive.
  void observed_passive(uint16_t source, uint32_t observed_at) {
    if (!source || source != selected_) return;
    initial_due_ = false;
    if (!have_passive_ || observed_at != last_passive_at_) {
      last_passive_at_ = observed_at;
      have_passive_ = true;
    }
  }

  bool try_start_initial(uint16_t source, uint32_t now, bool recent_passive,
                         uint32_t min_between_initial_ms = 1500) {
    if (!initial_due_ || !source || source != selected_ || disabled_by_timeout_) return false;
    if (recent_passive) {
      initial_due_ = false; // no server request necessary for this selection
      return false;
    }
    if (pending_ || has_deferred_manual() ||
        (have_poll_ && now - last_poll_ < min_between_initial_ms) ||
        (have_manual_ && now - last_manual_at_ < 10000)) return false;
    initial_due_ = false;
    start(source, now);
    return true;
  }

  // When no NEW animation/combat activity has been seen for the configured
  // delay, request a refresh; repeat at the SAME configurable interval while
  // idle, without ever having more than one automatic request in flight.
  // Display freshness is independent: the last target is retained while idle.
  bool try_start_fallback(uint16_t source, uint32_t now, uint32_t retry_ms = 1000) {
    if (!source || source != selected_ || pending_ || disabled_by_timeout_ ||
        has_deferred_manual() || initial_due_ ||
        (now - selected_at_ < retry_ms) ||
        (have_passive_ && now - last_passive_at_ < retry_ms) ||
        (have_poll_ && now - last_poll_ < retry_ms) ||
        (have_manual_ && now - last_manual_at_ < 10000)) return false;
    start(source, now);
    return true;
  }

  // Defer at most the most recent typed /assist / keybind while a background
  // response is outstanding, instead of creating multiple manual requests.
  bool defer_manual(std::string command, uint32_t now) {
    last_manual_at_ = now;
    have_manual_ = true;
    if (!pending_) return false;
    manual_command_ = std::move(command);
    return true;
  }

  // A deferred manual command is protected from another automatic poll from
  // the time it is actually dispatched (not merely when the key was pressed).
  void mark_manual_sent(uint32_t now) {
    last_manual_at_ = now;
    have_manual_ = true;
  }

  bool receive(uint16_t& source) {
    if (!pending_) return false;
    source = source_;
    pending_ = false;
    source_ = 0;
    return true;
  }

  // A reply may still arrive after the timeout. Disable future automatic
  // polling instead of resetting and creating an ambiguous request queue.
  bool check_timeout(uint32_t now, uint32_t timeout_ms = 3500) {
    if (!pending_ || now - sent_at_ < timeout_ms) return false;
    pending_ = false;
    source_ = 0;
    disabled_by_timeout_ = true;
    return true;
  }

  std::string take_deferred_manual() {
    std::string cmd = std::move(manual_command_);
    manual_command_.clear();
    return cmd;
  }

 private:
  void start(uint16_t source, uint32_t now) {
    pending_ = true;
    source_ = source;
    sent_at_ = now;
    last_poll_ = now;
    have_poll_ = true;
  }

  bool pending_ = false;
  bool disabled_by_timeout_ = false;
  uint16_t source_ = 0;
  uint16_t selected_ = 0;
  bool initial_due_ = false;
  uint32_t selected_at_ = 0;
  uint32_t sent_at_ = 0;
  uint32_t last_poll_ = 0;
  uint32_t last_manual_at_ = 0;
  uint32_t last_passive_at_ = 0;
  bool have_passive_ = false;
  bool have_poll_ = false;
  bool have_manual_ = false;
  std::string manual_command_;
};
