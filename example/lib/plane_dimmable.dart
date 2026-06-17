import 'package:flutter/material.dart';
import 'package:flutter/services.dart';

/// SPATIAL MIGRATION — Environmental Lighting (step 1): active-plane lighting.
/// When a plane is active, its surfaces get a FAINT top-edge ambient lift (light catching the
/// active plane) so activation reads as "lit/present", not just "not-dimmed". Deliberately tiny —
/// environmental, NOT a glow. Flip false to A/B; tune the two constants below to taste.
/// Restraint is the point: this is a presence cue, not decoration.
const bool kEnableActivePlaneLighting = true;
const double kActiveLiftOpacity = 0.039; // ~10/255 white at the top edge, fading to nothing
const double kActiveLiftHeight = 48.0;

/// M2.3H — SEMANTIC DEPTH (role, NOT activation). A surface's STRUCTURAL importance, independent of
/// whether its plane is active. `primary` = the dominant cognitive field (the editor): full presence.
/// `contextual` = supporting instruments (inspector, palette): a slightly reduced contrast FLOOR so
/// they read as quieter SOVEREIGN surfaces — never reaching the primary's presence, even when focused.
///
/// CRITICAL (the whole point): this is the PRESENCE axis and it is ORTHOGONAL to the active/inactive
/// VITALITY axis above. Role-depth is CONSTANT across activation — a contextual surface that gains
/// focus gets more vitality but its presence floor stays below the primary's, so the hierarchy never
/// collapses on a click. NOT dimming, NOT blur, NOT translucency — just "less assertive".
enum SurfaceDepth { primary, contextual }

/// THE single taste knob: how far a contextual surface's contrast is pulled in (1.0 = identical to
/// primary). Keep it a WHISPER. Tune here.
const double kContextualContrast = 0.93;

// Contrast-reduction matrix from kContextualContrast: out = c*in + (1-c)*128 per RGB channel (alpha
// untouched, so transparent stays transparent and the DWM backdrop still shows through).
final List<double> _kContextualMatrix = () {
  const double c = kContextualContrast;
  const double t = (1 - c) * 128;
  return <double>[
    c, 0, 0, 0, t, //
    0, c, 0, 0, t, //
    0, 0, c, 0, t, //
    0, 0, 0, 1, 0, //
  ];
}();

/// SPATIAL MIGRATION — Stage 2A: content-level plane dimming.
///
/// Wraps a surface's content and renders it dimmed + slightly desaturated when its
/// composition plane is NOT the active one. The active/inactive boolean is PUSHED from
/// C++ (the product-layer PlaneVisualProjector) over `MethodChannel('morphic/plane')` —
/// each surface engine has its own channel instance, so a surface only ever hears about
/// ITSELF (no id needed).
///
/// This is the perceptual proof of the whole migration: when two surfaces belong to one
/// plane, activating EITHER keeps BOTH bright while unrelated planes recede. It is done
/// entirely in Flutter content — no DWM material, no native call, substrate-independent.
///
/// CRITICAL: this is VISUAL ONLY (SPATIAL_RUNTIME_MIGRATION.md §4b). It changes pixels,
/// never input. Keyboard/focus/capture stay HWND-native; a dimmed surface is still fully
/// interactive. Inactive = dim + slight desaturate (recede); active = pristine + a faint
/// top-edge ambient lift (environmental lighting step 1). Still NO animation, NO glow — restraint
/// is the point: does shared visual activation feel spatial, present, and lit — not decorated?
class PlaneDimmable extends StatefulWidget {
  final Widget child;
  final SurfaceDepth depth; // M2.3H — structural role (presence), independent of activation.
  // M2.6 — VITALITY: graded active-state radiance (orthogonal to depth). 1.0 = full radiance (anchor);
  // <1 = a quieter awakening (companions catch LESS of the activation light, NOT equal). Scales the
  // active-plane lift only — never the inactive dim, never the constant presence floor.
  final double vitality;
  const PlaneDimmable(
      {super.key,
      required this.child,
      this.depth = SurfaceDepth.primary,
      this.vitality = 1.0});

  @override
  State<PlaneDimmable> createState() => _PlaneDimmableState();
}

class _PlaneDimmableState extends State<PlaneDimmable> {
  static const _channel = MethodChannel('morphic/plane');

  // Stable identity for the wrapped content. CRITICAL: without this, toggling active state
  // changes the tree DEPTH at the child's slot (pristine vs Opacity>ColorFiltered>child),
  // which remounts the surface's content — re-running its initState and RESETTING all
  // in-surface state (engine id, palette selections, search text/focus, scroll). The
  // GlobalKey makes Flutter MOVE the existing element (preserving its State) instead.
  final GlobalKey _contentKey = GlobalKey();

  // Default ACTIVE/full until told otherwise — a surface looks normal until some other
  // plane takes activation. (No active plane yet → everything reads active.)
  bool _active = true;

  @override
  void initState() {
    super.initState();
    _channel.setMethodCallHandler((call) async {
      if (call.method == 'setActive') {
        final next = call.arguments == true;
        if (next != _active && mounted) {
          setState(() => _active = next);
        }
      }
      return null;
    });
  }

  @override
  Widget build(BuildContext context) {
    final raw = KeyedSubtree(key: _contentKey, child: widget.child);
    // ROLE-DEPTH (presence) — applied CONSTANTLY here, BEFORE the active/inactive branches, so it
    // never couples to activation. Contextual surfaces sit at a lower contrast floor (quieter); the
    // editor (primary) is untouched and stays the dominant field. Vitality (lift / dim) modulates on
    // top of this, never erasing it. _contentKey (on `raw`) preserves child State across both axes.
    final Widget content = widget.depth == SurfaceDepth.contextual
        ? ColorFiltered(
            colorFilter: ColorFilter.matrix(_kContextualMatrix), child: raw)
        : raw;
    // Active plane: pristine child + a faint top-edge ambient lift (active-plane lighting).
    // The lift is IgnorePointer + a low-alpha top gradient fading to nothing, so it never blocks
    // interaction and never muddies the body. _contentKey preserves child State across the move.
    if (_active) {
      if (!kEnableActivePlaneLighting) return content;
      return Stack(
        fit: StackFit.expand,
        children: [
          content,
          Positioned(
            top: 0,
            left: 0,
            right: 0,
            height: kActiveLiftHeight,
            child: IgnorePointer(
              child: DecoratedBox(
                decoration: BoxDecoration(
                  gradient: LinearGradient(
                    begin: Alignment.topCenter,
                    end: Alignment.bottomCenter,
                    colors: [
                      // M2.6 — graded by the surface's vitality: the anchor radiates full light,
                      // companions catch proportionally less (a dominant surface radiates vitality).
                      const Color(0xFFFFFFFF)
                          .withOpacity(kActiveLiftOpacity * widget.vitality),
                      const Color(0x00FFFFFF),
                    ],
                  ),
                ),
              ),
            ),
          ),
        ],
      );
    }
    // Inactive plane: reduced opacity + slight desaturation (saturation ~0.6). Instant,
    // no animation (Stage 2A). The alpha row is identity so transparent stays transparent
    // (the DWM backdrop still shows through). State is preserved by _contentKey above.
    return Opacity(
      opacity: 0.88,
      child: ColorFiltered(
        colorFilter: const ColorFilter.matrix(_kDesaturate60),
        child: content,
      ),
    );
  }
}

// Luminance-weighted saturation matrix for s = 0.6 (1 - s = 0.4). Standard Rec.709
// weights r=0.2126, g=0.7152, b=0.0722. Alpha untouched.
const List<double> _kDesaturate60 = <double>[
  0.68504, 0.28608, 0.02888, 0, 0,
  0.08504, 0.88608, 0.02888, 0, 0,
  0.08504, 0.28608, 0.62888, 0, 0,
  0, 0, 0, 1, 0,
];
