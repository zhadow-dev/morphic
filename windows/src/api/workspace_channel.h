#pragma once

#include <flutter/method_channel.h>
#include <flutter/standard_method_codec.h>
#include <memory>

namespace morphic {

class MorphicRuntimeImpl;

// Workspace Channel — handles workspace lifecycle and semantic operations.
// Plugin layer is TRANSPORT ONLY. All logic in MorphicRuntimeImpl.

class WorkspaceChannel {
public:
    WorkspaceChannel(MorphicRuntimeImpl& runtime) : runtime_(runtime) {}

    void handleCreateWorkspace(const flutter::EncodableMap& args,
        std::unique_ptr<flutter::MethodResult<flutter::EncodableValue>> result);

    void handleDestroyWorkspace(const flutter::EncodableMap& args,
        std::unique_ptr<flutter::MethodResult<flutter::EncodableValue>> result);

    void handleSwitchWorkspace(const flutter::EncodableMap& args,
        std::unique_ptr<flutter::MethodResult<flutter::EncodableValue>> result);

    void handleActiveWorkspace(
        std::unique_ptr<flutter::MethodResult<flutter::EncodableValue>> result);

    void handleSetWorkspaceIntent(const flutter::EncodableMap& args,
        std::unique_ptr<flutter::MethodResult<flutter::EncodableValue>> result);

    void handleSetSurfaceAttention(const flutter::EncodableMap& args,
        std::unique_ptr<flutter::MethodResult<flutter::EncodableValue>> result);

    void handleAssociateSurfaces(const flutter::EncodableMap& args,
        std::unique_ptr<flutter::MethodResult<flutter::EncodableValue>> result);

    void handleDissociateSurface(const flutter::EncodableMap& args,
        std::unique_ptr<flutter::MethodResult<flutter::EncodableValue>> result);

private:
    MorphicRuntimeImpl& runtime_;
    static int64_t getInt(const flutter::EncodableMap& map, const std::string& key, int64_t def = 0);
    static std::string getString(const flutter::EncodableMap& map, const std::string& key, const std::string& def = "");
};

} // namespace morphic
