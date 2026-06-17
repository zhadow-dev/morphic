#include "runtime_events.h"

const char* ToString(RuntimeEvent event) {
  switch (event) {
    case RuntimeEvent::SurfaceCreated: return "SurfaceCreated";
    case RuntimeEvent::SurfaceReady: return "SurfaceReady";
    case RuntimeEvent::SurfaceActivated: return "SurfaceActivated";
    case RuntimeEvent::SurfaceFocused: return "SurfaceFocused";
    case RuntimeEvent::SurfaceMoved: return "SurfaceMoved";
    case RuntimeEvent::SurfaceResized: return "SurfaceResized";
    case RuntimeEvent::InteractionBegan: return "InteractionBegan";
    case RuntimeEvent::InteractionUpdated: return "InteractionUpdated";
    case RuntimeEvent::InteractionEnded: return "InteractionEnded";
    case RuntimeEvent::SurfaceClosing: return "SurfaceClosing";
    case RuntimeEvent::SurfaceDestroyed: return "SurfaceDestroyed";
    case RuntimeEvent::SurfaceAttached: return "SurfaceAttached";
    case RuntimeEvent::SurfaceDetached: return "SurfaceDetached";
    case RuntimeEvent::SurfaceGrouped: return "SurfaceGrouped";
    case RuntimeEvent::SurfaceUngrouped: return "SurfaceUngrouped";
    case RuntimeEvent::TopologyMutated: return "TopologyMutated";
    case RuntimeEvent::GroupFractured: return "GroupFractured";
    case RuntimeEvent::SurfaceDocked: return "SurfaceDocked";
    case RuntimeEvent::SurfaceUndocked: return "SurfaceUndocked";
  }
  return "Unknown";
}

EventBus::Token EventBus::Subscribe(Handler handler) {
  const Token token = next_token_++;
  handlers_.emplace(token, std::move(handler));
  return token;
}

void EventBus::Unsubscribe(Token token) {
  handlers_.erase(token);  // erase of unknown key is a safe no-op
}

void EventBus::Publish(RuntimeEvent event, SurfaceShell* surface) {
  // Snapshot TOKENS (not handlers) so a handler may Subscribe / Unsubscribe — and
  // even cause a subscriber to be DESTROYED — during dispatch. Before invoking
  // each, re-check the token still exists: a stale entry (its owner unsubscribed
  // or was freed earlier this publish) is SKIPPED rather than invoked through a
  // dangling std::function capturing a freed object. Copying handlers into a
  // vector would NOT protect against the freed-capture case. (PHASE 8C — mirrors
  // the FrameClock::Dispatch token-aware fix.)
  std::vector<Token> tokens;
  tokens.reserve(handlers_.size());
  for (auto& [token, _] : handlers_) {
    tokens.push_back(token);
  }
  for (Token token : tokens) {
    auto it = handlers_.find(token);
    if (it == handlers_.end()) {
      continue;  // unsubscribed (possibly self-destroyed) earlier this publish
    }
    // COPY the handler to a local before invoking — it may Subscribe/Unsubscribe,
    // rehashing handlers_ and destroying the map node (and its std::function)
    // while operator() is on the stack. The token re-lookup skips entries freed
    // earlier; the local copy keeps THIS one alive for its own call.
    Handler cb = it->second;
    cb(event, surface);
  }
}
