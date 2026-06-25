#pragma once

#include <flutter/method_channel.h>
#include <flutter/standard_method_codec.h>
#include <memory>

namespace morphic {

class MorphicRuntimeImpl;

// Diagnostics Channel — runtime health and validation.
// Transport only. No validator knowledge.

class DiagnosticsChannel {
public:
    DiagnosticsChannel(MorphicRuntimeImpl& runtime) : runtime_(runtime) {}

    void handleGetDiagnostics(
        std::unique_ptr<flutter::MethodResult<flutter::EncodableValue>> result);

    void handleRunValidation(
        std::unique_ptr<flutter::MethodResult<flutter::EncodableValue>> result);

    void handleGetBootstrapPhase(
        std::unique_ptr<flutter::MethodResult<flutter::EncodableValue>> result);

private:
    MorphicRuntimeImpl& runtime_;
};

} // namespace morphic
