// Ensure dllexport when this translation unit is compiled (both DLL and test targets).
#ifndef FLUTTER_PLUGIN_IMPL
#define FLUTTER_PLUGIN_IMPL
#endif
#include "include/morphic/morphic_extension.h"

namespace morphic {

static ExtensionHandler g_extensionHandler;

void SetMorphicExtensionHandler(ExtensionHandler handler) {
  g_extensionHandler = std::move(handler);
}

void ClearMorphicExtensionHandler() {
  g_extensionHandler = nullptr;
}

const ExtensionHandler& GetMorphicExtensionHandler() {
  return g_extensionHandler;
}

}  // namespace morphic
