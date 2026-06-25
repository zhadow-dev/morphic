#ifndef FLUTTER_PLUGIN_MORPHIC_PLUGIN_C_API_H_
#define FLUTTER_PLUGIN_MORPHIC_PLUGIN_C_API_H_

#include <flutter_plugin_registrar.h>

#ifdef FLUTTER_PLUGIN_IMPL
#define FLUTTER_PLUGIN_EXPORT __declspec(dllexport)
#else
#define FLUTTER_PLUGIN_EXPORT __declspec(dllimport)
#endif

#if defined(__cplusplus)
extern "C" {
#endif

FLUTTER_PLUGIN_EXPORT void MorphicPluginCApiRegisterWithRegistrar(
    FlutterDesktopPluginRegistrarRef registrar);

// Phase 4 Step 2: Native Runtime callback.
// Allows the MorphicRuntime native shell to notify the plugin of main window activation,
// removing the need for the plugin to subclass the main window.
FLUTTER_PLUGIN_EXPORT void MorphicPlugin_OnMainWindowActivated(bool active);

// Phase 4 Step 3: Global keyboard interception.
// Routes key events from the native shell to the Morphic InputRouter before
// Flutter processes them.
FLUTTER_PLUGIN_EXPORT bool MorphicPlugin_OnGlobalKey(unsigned int msg, unsigned long long wParam, long long lParam);

#if defined(__cplusplus)
}  // extern "C"
#endif

#endif  // FLUTTER_PLUGIN_MORPHIC_PLUGIN_C_API_H_
