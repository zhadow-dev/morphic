#pragma once

#include <flutter/method_channel.h>
#include <flutter/standard_method_codec.h>
#include <memory>

namespace morphic {

class MorphicRuntimeImpl;

// Session Channel — save/restore session operations.
// Transport only.

class SessionChannel {
public:
    SessionChannel(MorphicRuntimeImpl& runtime) : runtime_(runtime) {}

    void handleSaveSession(const flutter::EncodableMap& args,
        std::unique_ptr<flutter::MethodResult<flutter::EncodableValue>> result);

    void handleRestoreSession(
        std::unique_ptr<flutter::MethodResult<flutter::EncodableValue>> result);

private:
    MorphicRuntimeImpl& runtime_;
    static std::string getString(const flutter::EncodableMap& map, const std::string& key, const std::string& def = "");
};

} // namespace morphic
