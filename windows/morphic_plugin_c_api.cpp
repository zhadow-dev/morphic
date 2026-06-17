#include "include/morphic/morphic_plugin_c_api.h"

#include <flutter/plugin_registrar_windows.h>

#include "morphic_plugin.h"

void MorphicPluginCApiRegisterWithRegistrar(
    FlutterDesktopPluginRegistrarRef registrar) {
  morphic::MorphicPlugin::RegisterWithRegistrar(
      flutter::PluginRegistrarManager::GetInstance()
          ->GetRegistrar<flutter::PluginRegistrarWindows>(registrar));
}
