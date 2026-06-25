import 'package:flutter/material.dart';

import 'home_glass.dart';
import 'home_test_controls.dart';

/// SPATIAL HOME — the main panel, stripped to an interaction + redraw probe:
/// a tap counter, a close button, and a continuously-spinning square (if the
/// square keeps turning, the compositor is re-capturing Flutter frames).
@pragma('vm:entry-point')
void homePanelMain() => runApp(
      const HomeSurfaceApp(child: DiagnosticSurface(label: 'panel (main)', roomy: true)),
    );
