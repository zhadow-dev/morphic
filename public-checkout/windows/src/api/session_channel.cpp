#include "session_channel.h"
#include "morphic_runtime_impl.h"
#include "../../include/morphic/morphic_api.h"

namespace morphic {

std::string SessionChannel::getString(const flutter::EncodableMap& map,
                                        const std::string& key,
                                        const std::string& def) {
    auto it = map.find(flutter::EncodableValue(key));
    if (it != map.end()) {
        if (auto* v = std::get_if<std::string>(&it->second)) return *v;
    }
    return def;
}

void SessionChannel::handleSaveSession(const flutter::EncodableMap& args,
    std::unique_ptr<flutter::MethodResult<flutter::EncodableValue>> result) {

    auto reasonStr = getString(args, "reason", "intentionalPause");

    api::InterruptionReason reason = api::InterruptionReason::IntentionalPause;
    if (reasonStr == "intentionalPause") reason = api::InterruptionReason::IntentionalPause;
    else if (reasonStr == "forcedSuspend") reason = api::InterruptionReason::ForcedSuspend;
    else if (reasonStr == "crashRecovery") reason = api::InterruptionReason::CrashRecovery;
    else if (reasonStr == "temporaryDiversion") reason = api::InterruptionReason::TemporaryDiversion;
    else if (reasonStr == "urgentInterruption") reason = api::InterruptionReason::UrgentInterruption;
    else if (reasonStr == "sessionEnd") reason = api::InterruptionReason::SessionEnd;

    runtime_.saveSession(reason);
    result->Success(flutter::EncodableValue(true));
}

void SessionChannel::handleRestoreSession(
    std::unique_ptr<flutter::MethodResult<flutter::EncodableValue>> result) {
    runtime_.restoreSession();
    result->Success(flutter::EncodableValue(true));
}

} // namespace morphic
