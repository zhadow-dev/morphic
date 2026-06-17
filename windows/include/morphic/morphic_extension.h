#ifndef MORPHIC_EXTENSION_HANDLER_H_
#define MORPHIC_EXTENSION_HANDLER_H_

// PHASE 10.2 — Exported extension handler for product-layer method channel
// commands. The example app's runtime calls SetMorphicExtensionHandler() to
// register a callback that the plugin DLL invokes for unrecognized methods.
//
// These are proper DLL-exported symbols (dllexport in the DLL, dllimport in
// the EXE) so the handler pointer lives in the DLL's static storage — the
// same binary where HandleMethodCall reads it.
//
// Dependency arrow: example → library, never reversed.

#include <flutter/method_channel.h>
#include <flutter/standard_method_codec.h>

#include <functional>
#include <memory>
#include <string>

#ifdef FLUTTER_PLUGIN_IMPL
#define MORPHIC_EXT_EXPORT __declspec(dllexport)
#else
#define MORPHIC_EXT_EXPORT __declspec(dllimport)
#endif

namespace morphic {

using ExtensionHandler = std::function<void(
    const std::string& method,
    const flutter::EncodableValue* args,
    std::unique_ptr<flutter::MethodResult<flutter::EncodableValue>> result)>;

// Set the process-wide extension handler. Called by the example app's
// MorphicRuntime during Create().
MORPHIC_EXT_EXPORT void SetMorphicExtensionHandler(ExtensionHandler handler);

// Clear the extension handler. Called during Destroy().
MORPHIC_EXT_EXPORT void ClearMorphicExtensionHandler();

// Get the current extension handler (used by the plugin's HandleMethodCall).
MORPHIC_EXT_EXPORT const ExtensionHandler& GetMorphicExtensionHandler();

}  // namespace morphic

#endif  // MORPHIC_EXTENSION_HANDLER_H_
