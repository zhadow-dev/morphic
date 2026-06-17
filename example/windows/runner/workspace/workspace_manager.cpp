#include "workspace/workspace_manager.h"

#include <algorithm>

#include "forensic_trace.h"

namespace morphic::workspace {

WorkspaceManager::WorkspaceManager() {
  forensic::Log("ECOLOGY", "WorkspaceManager created");
}

std::string WorkspaceManager::Create(const std::string& title) {
  const std::string id = "ws" + std::to_string(next_id_++);
  Workspace w;
  w.id = id;
  w.title = title;
  workspaces_[id] = std::move(w);
  if (current_.empty()) current_ = id;
  forensic::Log("ECOLOGY", "workspace create id=" + id + " title=" + title +
                               (current_ == id ? " (current)" : ""));
  return id;
}

std::vector<std::string> WorkspaceManager::Destroy(
    const std::string& workspace_id) {
  auto it = workspaces_.find(workspace_id);
  if (it == workspaces_.end()) return {};

  std::vector<std::string> orphaned = it->second.surface_ids;
  forensic::Log("ECOLOGY", "workspace destroy id=" + workspace_id +
                               " surfaces=" + std::to_string(orphaned.size()));
  workspaces_.erase(it);

  // Pick a new current if we destroyed the active one.
  if (current_ == workspace_id) {
    current_ = workspaces_.empty() ? "" : workspaces_.begin()->first;
    if (!current_.empty()) {
      forensic::Log("ECOLOGY", "workspace current now=" + current_);
    }
  }
  return orphaned;
}

void WorkspaceManager::AddSurface(const std::string& workspace_id,
                                  const std::string& surface_id) {
  auto it = workspaces_.find(workspace_id);
  if (it == workspaces_.end()) return;
  auto& ids = it->second.surface_ids;
  if (std::find(ids.begin(), ids.end(), surface_id) == ids.end()) {
    ids.push_back(surface_id);
    forensic::Log("ECOLOGY", "workspace " + workspace_id + " += surface " +
                                 surface_id);
  }
}

void WorkspaceManager::RemoveSurface(const std::string& surface_id) {
  for (auto& [wid, w] : workspaces_) {
    auto& ids = w.surface_ids;
    auto n = ids.size();
    ids.erase(std::remove(ids.begin(), ids.end(), surface_id), ids.end());
    if (ids.size() != n) {
      forensic::Log("ECOLOGY", "workspace " + wid + " -= surface " + surface_id);
    }
  }
}

const Workspace* WorkspaceManager::Get(const std::string& workspace_id) const {
  auto it = workspaces_.find(workspace_id);
  return it == workspaces_.end() ? nullptr : &it->second;
}

std::vector<std::string> WorkspaceManager::SurfacesOf(
    const std::string& workspace_id) const {
  auto it = workspaces_.find(workspace_id);
  return it == workspaces_.end() ? std::vector<std::string>{}
                                 : it->second.surface_ids;
}

std::string WorkspaceManager::WorkspaceOf(const std::string& surface_id) const {
  for (const auto& [wid, w] : workspaces_) {
    if (std::find(w.surface_ids.begin(), w.surface_ids.end(), surface_id) !=
        w.surface_ids.end()) {
      return wid;
    }
  }
  return "";
}

std::vector<Workspace> WorkspaceManager::All() const {
  std::vector<Workspace> out;
  out.reserve(workspaces_.size());
  for (const auto& [_, w] : workspaces_) out.push_back(w);
  return out;
}

void WorkspaceManager::SetCurrent(const std::string& workspace_id) {
  if (workspaces_.find(workspace_id) != workspaces_.end()) {
    current_ = workspace_id;
  }
}

}  // namespace morphic::workspace
