#include "workspace_channel.h"
#include "morphic_runtime_impl.h"
#include "../../include/morphic/morphic_api.h"

namespace morphic {

int64_t WorkspaceChannel::getInt(const flutter::EncodableMap& map,
                                   const std::string& key, int64_t def) {
    auto it = map.find(flutter::EncodableValue(key));
    if (it != map.end()) {
        if (auto* v = std::get_if<int32_t>(&it->second)) return *v;
        if (auto* v = std::get_if<int64_t>(&it->second)) return *v;
    }
    return def;
}

std::string WorkspaceChannel::getString(const flutter::EncodableMap& map,
                                          const std::string& key,
                                          const std::string& def) {
    auto it = map.find(flutter::EncodableValue(key));
    if (it != map.end()) {
        if (auto* v = std::get_if<std::string>(&it->second)) return *v;
    }
    return def;
}

void WorkspaceChannel::handleCreateWorkspace(const flutter::EncodableMap& args,
    std::unique_ptr<flutter::MethodResult<flutter::EncodableValue>> result) {

    auto activityStr = getString(args, "activity", "editing");
    auto dispositionStr = getString(args, "disposition", "persistent");
    auto label = getString(args, "label", "");

    api::WorkspaceConfig config;
    // Map string to enum
    if (activityStr == "editing") config.activity = api::Activity::Editing;
    else if (activityStr == "debugging") config.activity = api::Activity::Debugging;
    else if (activityStr == "monitoring") config.activity = api::Activity::Monitoring;
    else if (activityStr == "comparing") config.activity = api::Activity::Comparing;
    else if (activityStr == "reviewing") config.activity = api::Activity::Reviewing;
    else if (activityStr == "searching") config.activity = api::Activity::Searching;
    else if (activityStr == "inspecting") config.activity = api::Activity::Inspecting;
    else if (activityStr == "reference") config.activity = api::Activity::Reference;

    if (dispositionStr == "persistent") config.disposition = api::Disposition::Persistent;
    else if (dispositionStr == "transient") config.disposition = api::Disposition::Transient;
    else if (dispositionStr == "interruptSensitive") config.disposition = api::Disposition::InterruptSensitive;
    else if (dispositionStr == "continuityCritical") config.disposition = api::Disposition::ContinuityCritical;
    else if (dispositionStr == "backgroundDominant") config.disposition = api::Disposition::BackgroundDominant;
    else if (dispositionStr == "collaborative") config.disposition = api::Disposition::Collaborative;

    config.label = label;

    auto handle = runtime_.createWorkspace(config);
    result->Success(flutter::EncodableValue(static_cast<int64_t>(handle.id)));
}

void WorkspaceChannel::handleDestroyWorkspace(const flutter::EncodableMap& args,
    std::unique_ptr<flutter::MethodResult<flutter::EncodableValue>> result) {
    auto wsId = getInt(args, "workspaceId");
    runtime_.destroyWorkspace({static_cast<uint64_t>(wsId)});
    result->Success(flutter::EncodableValue(true));
}

void WorkspaceChannel::handleSwitchWorkspace(const flutter::EncodableMap& args,
    std::unique_ptr<flutter::MethodResult<flutter::EncodableValue>> result) {
    auto wsId = getInt(args, "workspaceId");
    runtime_.switchWorkspace({static_cast<uint64_t>(wsId)});
    result->Success(flutter::EncodableValue(true));
}

void WorkspaceChannel::handleActiveWorkspace(
    std::unique_ptr<flutter::MethodResult<flutter::EncodableValue>> result) {
    auto ws = runtime_.activeWorkspace();
    result->Success(flutter::EncodableValue(static_cast<int64_t>(ws.id)));
}

void WorkspaceChannel::handleSetWorkspaceIntent(const flutter::EncodableMap& args,
    std::unique_ptr<flutter::MethodResult<flutter::EncodableValue>> result) {
    auto wsId = getInt(args, "workspaceId");
    auto activityStr = getString(args, "activity", "editing");
    auto dispositionStr = getString(args, "disposition", "persistent");
    auto label = getString(args, "label", "");

    api::WorkspaceConfig config;
    if (activityStr == "editing") config.activity = api::Activity::Editing;
    else if (activityStr == "debugging") config.activity = api::Activity::Debugging;
    else if (activityStr == "monitoring") config.activity = api::Activity::Monitoring;
    else if (activityStr == "reviewing") config.activity = api::Activity::Reviewing;
    else if (activityStr == "searching") config.activity = api::Activity::Searching;
    else if (activityStr == "reference") config.activity = api::Activity::Reference;
    config.label = label;

    runtime_.updateWorkspaceConfig({static_cast<uint64_t>(wsId)}, config);
    result->Success(flutter::EncodableValue(true));
}

void WorkspaceChannel::handleSetSurfaceAttention(const flutter::EncodableMap& args,
    std::unique_ptr<flutter::MethodResult<flutter::EncodableValue>> result) {
    auto surfaceId = getInt(args, "surfaceId");
    auto attentionStr = getString(args, "attention", "active");

    api::Attention level = api::Attention::Active;
    if (attentionStr == "active") level = api::Attention::Active;
    else if (attentionStr == "passiveMonitoring") level = api::Attention::PassiveMonitoring;
    else if (attentionStr == "latentContinuity") level = api::Attention::LatentContinuity;
    else if (attentionStr == "interruptible") level = api::Attention::Interruptible;
    else if (attentionStr == "urgent") level = api::Attention::Urgent;
    else if (attentionStr == "background") level = api::Attention::Background;

    runtime_.declareSurfaceAttention({static_cast<uint64_t>(surfaceId)}, level);
    result->Success(flutter::EncodableValue(true));
}

void WorkspaceChannel::handleAssociateSurfaces(const flutter::EncodableMap& args,
    std::unique_ptr<flutter::MethodResult<flutter::EncodableValue>> result) {
    auto a = getInt(args, "surfaceA");
    auto b = getInt(args, "surfaceB");
    auto assocStr = getString(args, "association", "sharedContext");

    api::Association assoc = api::Association::SharedContext;
    if (assocStr == "coEditing") assoc = api::Association::CoEditing;
    else if (assocStr == "inspecting") assoc = api::Association::Inspecting;
    else if (assocStr == "monitoring") assoc = api::Association::Monitoring;
    else if (assocStr == "temporaryCompanion") assoc = api::Association::TemporaryCompanion;
    else if (assocStr == "sharedContext") assoc = api::Association::SharedContext;

    runtime_.associateSurfaces({static_cast<uint64_t>(a)},
                                {static_cast<uint64_t>(b)}, assoc);
    result->Success(flutter::EncodableValue(true));
}

void WorkspaceChannel::handleDissociateSurface(const flutter::EncodableMap& args,
    std::unique_ptr<flutter::MethodResult<flutter::EncodableValue>> result) {
    auto surfaceId = getInt(args, "surfaceId");
    runtime_.dissociateSurface({static_cast<uint64_t>(surfaceId)});
    result->Success(flutter::EncodableValue(true));
}

} // namespace morphic
