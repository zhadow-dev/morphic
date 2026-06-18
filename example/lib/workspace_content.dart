import 'package:flutter/material.dart';
import 'package:flutter/services.dart';

import 'material_recipe.dart';
import 'plane_dimmable.dart';

/// PHASE 10.2 / 11D — Workspace surface entrypoint.
///
/// Runs in its own Flutter engine inside a Morphic surface HWND. The primary
/// inhabitable surface (root of a workspace).
///
/// PHASE 11D — stripped to a PLAIN TRANSPARENT window (the editor was removed) so it
/// serves as a clean, large acrylic test surface. The content paints transparent so the
/// DWM backdrop can show through. NOTE: until the embedder presents an alpha swapchain
/// (11D), this shows the material TINT only, not real desktop diffusion. A minimal
/// translucent header is kept so palette/inspector spawns still work for shared-drag.
@pragma('vm:entry-point')
void workspaceMain() {
  runApp(const _WorkspaceApp());
}

class _WorkspaceApp extends StatelessWidget {
  const _WorkspaceApp();

  @override
  Widget build(BuildContext context) {
    return MaterialApp(
      debugShowCheckedModeBanner: false,
      color: Colors.transparent,  // PHASE 11D — transparent root so the DWM backdrop shows
      theme: ThemeData.dark(useMaterial3: true).copyWith(
        scaffoldBackgroundColor: Colors.transparent,
      ),
      home: const PlaneDimmable(child: _WorkspaceContent()),
    );
  }
}

class _WorkspaceContent extends StatefulWidget {
  const _WorkspaceContent();

  @override
  State<_WorkspaceContent> createState() => _WorkspaceContentState();
}

class _WorkspaceContentState extends State<_WorkspaceContent> {
  static const _channel = MethodChannel('morphic');
  // SPATIAL MIGRATION (Stage M1) — material identity. C++ pushes setMaterial(token) on
  // 'morphic/plane_material' from the surface's native appearance: 'standard' (opaque/grounded,
  // the default) or a glass material 'mica'/'acrylic'/'tabbed'. We render the matching
  // MaterialRecipe over the (transparent) glass; 'standard' = opaque, no recipe.
  static const _materialChannel = MethodChannel('morphic/plane_material');
  String _material = 'standard';
  bool get _glass => _material != 'standard';
  int _engineId = -1;

  @override
  void initState() {
    super.initState();
    _engineId = identityHashCode(this) % 10000;
    _materialChannel.setMethodCallHandler((call) async {
      if (call.method == 'setMaterial') {
        final next = call.arguments is String ? call.arguments as String : 'standard';
        if (next != _material && mounted) setState(() => _material = next);
      }
      return null;
    });
  }

  void _spawnPalette() {
    _channel.invokeMethod('ecology.spawn', {
      'kind': 'tool_palette',
      'parentId': 'workspace_$_engineId',
    });
  }

  void _spawnInspector() {
    _channel.invokeMethod('ecology.spawn', {
      'kind': 'inspector',
      'parentId': 'workspace_$_engineId',
    });
  }

  @override
  Widget build(BuildContext context) {
    return Scaffold(
      // STANDARD (default) → opaque grounded surface; GLASS → transparent so the backdrop shows.
      backgroundColor: _glass ? Colors.transparent : const Color(0xFF13171E),
      body: Stack(
        fit: StackFit.expand,
        children: [
          // Standard (grounded) ambient — subtle top-pooled elevation so a normal workspace has
          // depth/soft layering instead of an OLED void. The first taste of environmental light.
          if (!_glass) const _StandardAmbient(),
          // Material identity (Stage M1) — the Morphic recipe (mica/acrylic/tabbed) rendered
          // OVER the native blur, BEHIND the UI. Inert for 'standard' (returns nothing).
          if (_glass) MaterialLayer(MaterialRecipe.forToken(_material)),
          Column(
        children: [
          // Header — solid in Standard, translucent in Glass (so the backdrop reads behind it).
          Container(
            height: 30,
            padding: const EdgeInsets.symmetric(horizontal: 12),
            decoration: BoxDecoration(
              color: _glass
                  ? const Color(0xFF161b22).withValues(alpha: 0.30)
                  : const Color(0xFF161b22),
              border: const Border(
                  bottom: BorderSide(color: Color(0x4430363d))),
            ),
            child: Row(
              children: [
                Container(
                  width: 6, height: 6,
                  decoration: const BoxDecoration(
                    shape: BoxShape.circle,
                    color: Color(0xFF58a6ff),
                  ),
                ),
                const SizedBox(width: 8),
                Text('WORKSPACE',
                    style: TextStyle(
                        color: const Color(0xFF58a6ff).withValues(alpha: 0.9),
                        fontSize: 9,
                        fontWeight: FontWeight.w700,
                        letterSpacing: 1.5)),
                const SizedBox(width: 12),
                Text('E#$_engineId',
                    style: const TextStyle(
                        color: Color(0xFF8b949e),
                        fontSize: 9,
                        fontFamily: 'Consolas')),
                const Spacer(),
                _tinyAction('+ Palette', const Color(0xFFf0883e), _spawnPalette),
                const SizedBox(width: 6),
                _tinyAction('+ Inspector', const Color(0xFFd2a8ff), _spawnInspector),
              ],
            ),
          ),
          // Plain transparent body — THIS is the acrylic test surface (no opaque fill).
          Expanded(
            child: Center(
              child: Text(
                'WORKSPACE  E#$_engineId\nplain · acrylic test',
                textAlign: TextAlign.center,
                style: TextStyle(
                    color: const Color(0xFFe6edf3).withValues(alpha: 0.45),
                    fontSize: 12,
                    fontFamily: 'Consolas',
                    height: 1.6),
              ),
            ),
          ),
        ],
          ), // Column
        ], // Stack children
      ), // Stack (body)
    );
  }

  Widget _tinyAction(String label, Color color, VoidCallback onTap) {
    return GestureDetector(
      onTap: onTap,
      child: Container(
        padding: const EdgeInsets.symmetric(horizontal: 6, vertical: 2),
        decoration: BoxDecoration(
          color: color.withValues(alpha: 0.12),
          borderRadius: BorderRadius.circular(3),
        ),
        child: Text(label,
            style: TextStyle(
                color: color, fontSize: 8, fontWeight: FontWeight.w600)),
      ),
    );
  }
}

/// Grounded-standard ambient: a soft radial pool of light near the top fading to a grounded
/// dark — subtle elevation/depth for a normal opaque workspace (VSCode/Linear/Arc feel) instead
/// of a flat black void. The minimal first step of the environmental-lighting direction.
class _StandardAmbient extends StatelessWidget {
  const _StandardAmbient();

  @override
  Widget build(BuildContext context) {
    return const DecoratedBox(
      decoration: BoxDecoration(
        gradient: RadialGradient(
          center: Alignment(0, -0.7), // light pooled near the top edge
          radius: 1.4,
          colors: [Color(0xFF232C39), Color(0xFF13171E)],
          stops: [0.0, 1.0],
        ),
      ),
    );
  }
}
