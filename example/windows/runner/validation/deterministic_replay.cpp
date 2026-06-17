#include "validation/deterministic_replay.h"

#include <iomanip>
#include <thread>

// --- ReplayEventType string conversion ---

const char* ToString(ReplayEventType type) {
  switch (type) {
    case ReplayEventType::PointerDown:       return "pointer_down";
    case ReplayEventType::PointerMove:       return "pointer_move";
    case ReplayEventType::PointerUp:         return "pointer_up";
    case ReplayEventType::ResizeBegin:       return "resize_begin";
    case ReplayEventType::ResizeUpdate:      return "resize_update";
    case ReplayEventType::ResizeEnd:         return "resize_end";
    case ReplayEventType::Activate:          return "activate";
    case ReplayEventType::CaptureLost:       return "capture_lost";
    case ReplayEventType::CaptureStolenBack: return "capture_stolen_back";
    case ReplayEventType::SurfaceCreated:    return "surface_created";
    case ReplayEventType::SurfaceDestroyed:  return "surface_destroyed";
    case ReplayEventType::GroupCreated:       return "group_created";
    case ReplayEventType::GroupDestroyed:     return "group_destroyed";
    case ReplayEventType::DpiChanged:        return "dpi_changed";
    case ReplayEventType::FocusCycle:        return "focus_cycle";
  }
  return "unknown";
}

// --- ReplayDigest ---

std::string ReplayDigest::diff(const ReplayDigest& other) const {
  std::ostringstream oss;
  if (surface_count != other.surface_count)
    oss << "surface_count: " << surface_count << " vs " << other.surface_count << "; ";
  if (group_count != other.group_count)
    oss << "group_count: " << group_count << " vs " << other.group_count << "; ";
  if (active_surface_index != other.active_surface_index)
    oss << "active_surface: " << active_surface_index << " vs " << other.active_surface_index << "; ";
  if (interaction_active != other.interaction_active)
    oss << "interaction_active: " << interaction_active << " vs " << other.interaction_active << "; ";
  if (transactions_open != other.transactions_open)
    oss << "transactions_open: " << transactions_open << " vs " << other.transactions_open << "; ";
  if (capture_held != other.capture_held)
    oss << "capture_held: " << capture_held << " vs " << other.capture_held << "; ";
  if (z_order != other.z_order) {
    oss << "z_order: [";
    for (size_t i = 0; i < z_order.size(); i++) {
      if (i > 0) oss << ",";
      oss << z_order[i];
    }
    oss << "] vs [";
    for (size_t i = 0; i < other.z_order.size(); i++) {
      if (i > 0) oss << ",";
      oss << other.z_order[i];
    }
    oss << "]; ";
  }
  if (group_memberships != other.group_memberships) {
    oss << "group_memberships_differ; ";
  }
  return oss.str();
}

std::string ReplayDigest::toJson() const {
  std::ostringstream oss;
  oss << "{\n";
  oss << "  \"surface_count\": " << surface_count << ",\n";
  oss << "  \"group_count\": " << group_count << ",\n";
  oss << "  \"active_surface_index\": " << active_surface_index << ",\n";
  oss << "  \"interaction_active\": " << (interaction_active ? "true" : "false") << ",\n";
  oss << "  \"transactions_open\": " << transactions_open << ",\n";
  oss << "  \"capture_held\": " << (capture_held ? "true" : "false") << ",\n";
  oss << "  \"z_order\": [";
  for (size_t i = 0; i < z_order.size(); i++) {
    if (i > 0) oss << ", ";
    oss << z_order[i];
  }
  oss << "],\n";
  oss << "  \"group_memberships\": {";
  bool first_group = true;
  for (const auto& [gid, members] : group_memberships) {
    if (!first_group) oss << ", ";
    first_group = false;
    oss << "\"" << gid << "\": [";
    for (size_t i = 0; i < members.size(); i++) {
      if (i > 0) oss << ", ";
      oss << members[i];
    }
    oss << "]";
  }
  oss << "}\n";
  oss << "}";
  return oss.str();
}

// --- DeterministicReplayCapture ---

void DeterministicReplayCapture::beginRecording() {
  events_.clear();
  recording_ = true;
  recordStart_ = std::chrono::steady_clock::now();
}

void DeterministicReplayCapture::recordEvent(const ReplayEvent& event) {
  if (!recording_) return;
  ReplayEvent e = event;
  e.relative_time_ms = std::chrono::duration<double, std::milli>(
      std::chrono::steady_clock::now() - recordStart_).count();
  events_.push_back(e);
}

void DeterministicReplayCapture::endRecording() {
  recording_ = false;
}

// --- Convenience recorders ---

void DeterministicReplayCapture::recordPointerDown(int surface_index, POINT pt) {
  ReplayEvent e;
  e.type = ReplayEventType::PointerDown;
  e.surface_index = surface_index;
  e.screen_pt = pt;
  recordEvent(e);
}

void DeterministicReplayCapture::recordPointerMove(int surface_index, POINT pt) {
  ReplayEvent e;
  e.type = ReplayEventType::PointerMove;
  e.surface_index = surface_index;
  e.screen_pt = pt;
  recordEvent(e);
}

void DeterministicReplayCapture::recordPointerUp(int surface_index, POINT pt) {
  ReplayEvent e;
  e.type = ReplayEventType::PointerUp;
  e.surface_index = surface_index;
  e.screen_pt = pt;
  recordEvent(e);
}

void DeterministicReplayCapture::recordActivation(int surface_index) {
  ReplayEvent e;
  e.type = ReplayEventType::Activate;
  e.surface_index = surface_index;
  recordEvent(e);
}

void DeterministicReplayCapture::recordCaptureLost() {
  ReplayEvent e;
  e.type = ReplayEventType::CaptureLost;
  recordEvent(e);
}

void DeterministicReplayCapture::recordSurfaceCreated(int surface_index) {
  ReplayEvent e;
  e.type = ReplayEventType::SurfaceCreated;
  e.surface_index = surface_index;
  recordEvent(e);
}

void DeterministicReplayCapture::recordSurfaceDestroyed(int surface_index) {
  ReplayEvent e;
  e.type = ReplayEventType::SurfaceDestroyed;
  e.surface_index = surface_index;
  recordEvent(e);
}

void DeterministicReplayCapture::recordGroupCreated(int group_index,
                                                     const std::vector<int>& members) {
  ReplayEvent e;
  e.type = ReplayEventType::GroupCreated;
  e.surface_index = group_index;
  // Encode members in metadata
  std::ostringstream oss;
  for (size_t i = 0; i < members.size(); i++) {
    if (i > 0) oss << ",";
    oss << members[i];
  }
  e.metadata = oss.str();
  recordEvent(e);
}

void DeterministicReplayCapture::recordGroupDestroyed(int group_index) {
  ReplayEvent e;
  e.type = ReplayEventType::GroupDestroyed;
  e.surface_index = group_index;
  recordEvent(e);
}

// --- Persistence (NDJSON) ---

bool DeterministicReplayCapture::saveToFile(const std::string& path) const {
  std::ofstream out(path);
  if (!out.is_open()) return false;

  // First line: digest
  out << "{\"type\":\"digest\",\"data\":" << recordedDigest_.toJson() << "}\n";

  // Remaining lines: events
  for (const auto& e : events_) {
    out << "{\"t\":" << std::fixed << std::setprecision(3) << e.relative_time_ms
        << ",\"type\":\"" << ToString(e.type) << "\""
        << ",\"si\":" << e.surface_index
        << ",\"x\":" << e.screen_pt.x
        << ",\"y\":" << e.screen_pt.y;
    if (e.w != 0 || e.h != 0)
      out << ",\"w\":" << e.w << ",\"h\":" << e.h;
    if (e.resize_edge != 0)
      out << ",\"edge\":" << e.resize_edge;
    if (e.dpi != 96)
      out << ",\"dpi\":" << e.dpi;
    if (!e.metadata.empty())
      out << ",\"meta\":\"" << e.metadata << "\"";
    out << "}\n";
  }

  return true;
}

bool DeterministicReplayCapture::loadFromFile(const std::string& path) {
  std::ifstream in(path);
  if (!in.is_open()) return false;

  events_.clear();
  std::string line;

  while (std::getline(in, line)) {
    if (line.empty()) continue;

    // Minimal JSON parsing for replay events.
    // This is intentionally simple — determinism > parsing elegance.

    // Skip digest line (starts with {"type":"digest")
    if (line.find("\"type\":\"digest\"") != std::string::npos) continue;

    ReplayEvent e;

    // Extract timestamp
    auto tPos = line.find("\"t\":");
    if (tPos != std::string::npos) {
      e.relative_time_ms = std::stod(line.substr(tPos + 4));
    }

    // Extract type
    auto typePos = line.find("\"type\":\"");
    if (typePos != std::string::npos) {
      size_t start = typePos + 8;
      size_t end = line.find("\"", start);
      std::string typeStr = line.substr(start, end - start);

      if (typeStr == "pointer_down") e.type = ReplayEventType::PointerDown;
      else if (typeStr == "pointer_move") e.type = ReplayEventType::PointerMove;
      else if (typeStr == "pointer_up") e.type = ReplayEventType::PointerUp;
      else if (typeStr == "resize_begin") e.type = ReplayEventType::ResizeBegin;
      else if (typeStr == "resize_update") e.type = ReplayEventType::ResizeUpdate;
      else if (typeStr == "resize_end") e.type = ReplayEventType::ResizeEnd;
      else if (typeStr == "activate") e.type = ReplayEventType::Activate;
      else if (typeStr == "capture_lost") e.type = ReplayEventType::CaptureLost;
      else if (typeStr == "surface_created") e.type = ReplayEventType::SurfaceCreated;
      else if (typeStr == "surface_destroyed") e.type = ReplayEventType::SurfaceDestroyed;
      else if (typeStr == "group_created") e.type = ReplayEventType::GroupCreated;
      else if (typeStr == "group_destroyed") e.type = ReplayEventType::GroupDestroyed;
      else if (typeStr == "dpi_changed") e.type = ReplayEventType::DpiChanged;
      else if (typeStr == "focus_cycle") e.type = ReplayEventType::FocusCycle;
    }

    // Extract surface_index
    auto siPos = line.find("\"si\":");
    if (siPos != std::string::npos) {
      e.surface_index = std::stoi(line.substr(siPos + 5));
    }

    // Extract coordinates
    auto xPos = line.find("\"x\":");
    if (xPos != std::string::npos) {
      e.screen_pt.x = static_cast<LONG>(std::stoi(line.substr(xPos + 4)));
    }
    auto yPos = line.find("\"y\":");
    if (yPos != std::string::npos) {
      e.screen_pt.y = static_cast<LONG>(std::stoi(line.substr(yPos + 4)));
    }

    // Extract optional fields
    auto wPos = line.find("\"w\":");
    if (wPos != std::string::npos) e.w = std::stoi(line.substr(wPos + 4));
    auto hPos = line.find("\"h\":");
    if (hPos != std::string::npos) e.h = std::stoi(line.substr(hPos + 4));
    auto edgePos = line.find("\"edge\":");
    if (edgePos != std::string::npos) e.resize_edge = std::stoi(line.substr(edgePos + 7));

    events_.push_back(e);
  }

  return true;
}

// --- Replay ---

ReplayComparisonResult DeterministicReplayCapture::replay(RuntimeCallbacks& cb) {
  if (events_.empty()) return {};

  auto replayStart = std::chrono::steady_clock::now();

  for (const auto& event : events_) {
    // Wait until the correct relative timestamp
    auto targetTime = replayStart +
        std::chrono::duration<double, std::milli>(event.relative_time_ms);
    while (std::chrono::steady_clock::now() < targetTime) {
      auto remaining = std::chrono::duration<double, std::milli>(
          targetTime - std::chrono::steady_clock::now()).count();
      if (remaining > 2.0) {
        Sleep(1);
      }
    }

    dispatchEvent(event, cb);
  }

  // Capture post-replay digest
  ReplayComparisonResult result;
  result.recorded_digest = recordedDigest_;
  if (cb.captureDigest) {
    result.replayed_digest = cb.captureDigest();
  }
  result.identical = (result.recorded_digest == result.replayed_digest);
  if (!result.identical) {
    result.divergence_summary = result.recorded_digest.diff(result.replayed_digest);
  }
  return result;
}

ReplayComparisonResult DeterministicReplayCapture::replayImmediate(RuntimeCallbacks& cb) {
  for (const auto& event : events_) {
    dispatchEvent(event, cb);
  }

  ReplayComparisonResult result;
  result.recorded_digest = recordedDigest_;
  if (cb.captureDigest) {
    result.replayed_digest = cb.captureDigest();
  }
  result.identical = (result.recorded_digest == result.replayed_digest);
  if (!result.identical) {
    result.divergence_summary = result.recorded_digest.diff(result.replayed_digest);
  }
  return result;
}

void DeterministicReplayCapture::dispatchEvent(const ReplayEvent& event,
                                                RuntimeCallbacks& cb) {
  switch (event.type) {
    case ReplayEventType::PointerDown:
      if (cb.beginDrag) cb.beginDrag(event.surface_index, event.screen_pt);
      break;
    case ReplayEventType::PointerMove:
      if (cb.updateDrag) cb.updateDrag(event.surface_index, event.screen_pt);
      break;
    case ReplayEventType::PointerUp:
      if (cb.endDrag) cb.endDrag(event.surface_index, event.screen_pt);
      break;
    case ReplayEventType::ResizeBegin:
      if (cb.beginResize) cb.beginResize(event.surface_index, event.screen_pt, event.resize_edge);
      break;
    case ReplayEventType::ResizeUpdate:
      if (cb.updateResize) cb.updateResize(event.surface_index, event.w, event.h);
      break;
    case ReplayEventType::ResizeEnd:
      if (cb.endResize) cb.endResize(event.surface_index);
      break;
    case ReplayEventType::Activate:
      if (cb.activate) cb.activate(event.surface_index);
      break;
    case ReplayEventType::CaptureLost:
      if (cb.releaseCapture) cb.releaseCapture();
      break;
    case ReplayEventType::SurfaceDestroyed:
      if (cb.destroySurface) cb.destroySurface(event.surface_index);
      break;
    case ReplayEventType::SurfaceCreated:
      if (cb.createSurface) cb.createSurface(400, 300);
      break;
    case ReplayEventType::GroupCreated:
      if (cb.createGroup && !event.metadata.empty()) {
        // Parse member indices from metadata
        std::vector<int> members;
        std::istringstream ss(event.metadata);
        std::string token;
        while (std::getline(ss, token, ',')) {
          members.push_back(std::stoi(token));
        }
        cb.createGroup(members);
      }
      break;
    case ReplayEventType::GroupDestroyed:
      if (cb.destroyGroup) cb.destroyGroup(event.surface_index);
      break;
    case ReplayEventType::FocusCycle:
      if (event.surface_index > 0 && cb.focusNext) cb.focusNext();
      else if (cb.focusPrevious) cb.focusPrevious();
      break;
    case ReplayEventType::CaptureStolenBack:
    case ReplayEventType::DpiChanged:
      // These are recorded for forensic purposes but not actively replayed
      // (DPI changes require OS-level simulation not available in userland)
      break;
  }

  // Process a frame after each event to maintain real orchestration flow
  if (cb.processFrame) cb.processFrame();
}
