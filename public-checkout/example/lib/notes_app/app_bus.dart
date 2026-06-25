// AppBus moved to the platform authoring layer (morphic_app/) in M2.1D — it's not Notes-specific
// (MorphicSurface needs it too). Re-exported here so existing `import 'app_bus.dart'` sites keep
// resolving to the SAME single AppBus class (two copies would each grab the 'morphic/app' handler
// and only one would win). New code should import 'package .../morphic_app/app_bus.dart'.
export 'package:morphic/morphic.dart';
