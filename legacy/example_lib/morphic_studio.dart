import 'dart:async';
import 'package:flutter/material.dart';
import 'package:morphic/morphic.dart';

/// Morphic Studio — Real consumer workspace application.
///
/// Creates DIFFERENT types of native surfaces:
///   workspace  — compositor-managed, hidden from Alt+Tab
///   detached   — independent, shows in Alt+Tab/taskbar
///   toolPalette — floating utility, non-resizable
///   overlay    — topmost HUD, non-interactive

class MorphicStudioPage extends StatefulWidget {
  final MorphicController morphic;
  const MorphicStudioPage({super.key, required this.morphic});

  @override
  State<MorphicStudioPage> createState() => _MorphicStudioPageState();
}

class _MorphicStudioPageState extends State<MorphicStudioPage> {
  final List<_LiveWorkspace> _workspaces = [];
  int _activeWorkspaceId = 0;

  Map<String, dynamic> _health = {};
  String _bootstrapPhase = 'Unknown';
  String _status = 'Ready — click "Create Full Studio" to spawn workspace surfaces';
  Timer? _healthPoller;
  bool _validationRunning = false;
  Map<String, dynamic> _validationResult = {};
  bool _creating = false;

  @override
  void initState() {
    super.initState();
    _refreshHealth();
    _healthPoller = Timer.periodic(
        const Duration(seconds: 2), (_) => _refreshHealth());
  }

  @override
  void dispose() {
    _healthPoller?.cancel();
    super.dispose();
  }

  Future<void> _refreshHealth() async {
    try {
      final health = await widget.morphic.getDiagnostics();
      final phase = await widget.morphic.getBootstrapPhase();
      final activeWs = await widget.morphic.activeWorkspace();
      if (mounted) {
        setState(() {
          _health = health;
          _bootstrapPhase = phase;
          _activeWorkspaceId = activeWs;
        });
      }
    } catch (_) {}
  }

  // ==========================================================================
  //  WORKSPACE CREATION — Different surface roles
  // ==========================================================================

  /// Each workspace type has its own SurfaceRole, size, and position.
  static const _workspaceTemplates = <_WorkspaceTemplate>[
    _WorkspaceTemplate(
      label: 'Main Editor',
      activity: 'editing',
      disposition: 'persistent',
      role: SurfaceRole.workspace,
      color: 0x0F3460,
      width: 500, height: 550,
      x: 80, y: 80,
      description: 'Workspace surface — hidden from Alt+Tab, owned by shell',
    ),
    _WorkspaceTemplate(
      label: 'Detached Inspector',
      activity: 'inspecting',
      disposition: 'interruptSensitive',
      role: SurfaceRole.detached,
      color: 0x533483,
      width: 420, height: 500,
      x: 600, y: 80,
      description: 'Detached surface — shows in Alt+Tab, independent activation',
    ),
    _WorkspaceTemplate(
      label: 'Monitor Panel',
      activity: 'monitoring',
      disposition: 'backgroundDominant',
      role: SurfaceRole.workspace,
      color: 0x16C79A,
      width: 380, height: 300,
      x: 80, y: 650,
      description: 'Workspace surface — background monitoring, low attention',
    ),
    _WorkspaceTemplate(
      label: 'Debug Tools',
      activity: 'debugging',
      disposition: 'transient',
      role: SurfaceRole.toolPalette,
      color: 0xE94560,
      width: 280, height: 350,
      x: 1050, y: 80,
      description: 'Tool palette — floating utility, non-resizable, no Alt+Tab',
    ),
    _WorkspaceTemplate(
      label: 'Status Overlay',
      activity: 'monitoring',
      disposition: 'transient',
      role: SurfaceRole.overlay,
      color: 0xF7B731,
      width: 300, height: 180,
      x: 480, y: 580,
      description: 'Overlay — topmost HUD, non-interactive, always-on-top',
    ),
  ];

  Future<void> _createWorkspaceFromTemplate(_WorkspaceTemplate tmpl) async {
    if (_creating) return;
    setState(() {
      _creating = true;
      _status = 'Creating "${tmpl.label}" (${tmpl.role.name})...';
    });

    try {
      // 1. Semantic workspace
      final wsId = await widget.morphic.createWorkspace(
        activity: tmpl.activity,
        disposition: tmpl.disposition,
        label: tmpl.label,
      );

      // 2. Real surface with specific ROLE
      final surface = await widget.morphic.createSurface(SurfaceConfig(
        x: tmpl.x,
        y: tmpl.y,
        width: tmpl.width,
        height: tmpl.height,
        color: tmpl.color,
        role: tmpl.role,
        minWidth: tmpl.role == SurfaceRole.overlay ? tmpl.width : 150,
        minHeight: tmpl.role == SurfaceRole.overlay ? tmpl.height : 150,
      ));

      setState(() =>
          _status = 'Surface #${surface.id} (${tmpl.role.name}) — attaching Flutter...');

      // 3. Yield for message pump stability
      await Future.delayed(const Duration(milliseconds: 300));

      // 4. Attach Flutter renderer
      final r = await widget.morphic.attachRenderer(surface.id, type: 'flutter');
      final attached = r['success'] == true;

      // 5. Declare attention
      String attention = 'active';
      if (tmpl.activity == 'monitoring') attention = 'passiveMonitoring';
      if (tmpl.activity == 'reference') attention = 'background';
      await widget.morphic.setSurfaceAttention(surface.id, attention);

      setState(() {
        _workspaces.add(_LiveWorkspace(
          workspaceId: wsId,
          surfaceId: surface.id,
          template: tmpl,
          rendererAttached: attached,
        ));
        _creating = false;
        _status = '✓ "${tmpl.label}" [${tmpl.role.name}] → SRF#${surface.id} '
            '${attached ? "with Flutter ✓" : "NO RENDERER ✗"}';
      });
      await _refreshHealth();
    } catch (e) {
      setState(() {
        _creating = false;
        _status = '✗ Failed: $e';
      });
    }
  }

  Future<void> _createFullStudio() async {
    if (_workspaces.isNotEmpty || _creating) return;

    for (final tmpl in _workspaceTemplates) {
      await _createWorkspaceFromTemplate(tmpl);
      // Longer delay between engines to prevent activation storms
      await Future.delayed(const Duration(milliseconds: 500));
    }

    // Associate related surfaces
    if (_workspaces.length >= 2) {
      await widget.morphic.associateSurfaces(
          _workspaces[0].surfaceId, _workspaces[1].surfaceId, 'inspecting');
    }
    if (_workspaces.length >= 4) {
      await widget.morphic.associateSurfaces(
          _workspaces[0].surfaceId, _workspaces[3].surfaceId, 'coEditing');
    }

    setState(() => _status =
        '✓ Full Studio — ${_workspaces.length} surfaces (${_workspaces.where((w) => w.rendererAttached).length} with Flutter)');
  }

  Future<void> _destroyWorkspace(_LiveWorkspace ws) async {
    setState(() => _status = 'Destroying "${ws.template.label}"...');
    try {
      await widget.morphic.dissociateSurface(ws.surfaceId);
      if (ws.rendererAttached) {
        await widget.morphic.detachRenderer(ws.surfaceId);
      }
      await widget.morphic.destroySurface(ws.surfaceId);
      await widget.morphic.destroyWorkspace(ws.workspaceId);
      setState(() {
        _workspaces.remove(ws);
        _status = '✓ Destroyed "${ws.template.label}"';
      });
      await _refreshHealth();
    } catch (e) {
      setState(() => _status = '✗ Destroy failed: $e');
    }
  }

  Future<void> _destroyAll() async {
    for (final ws in List.of(_workspaces)) {
      await _destroyWorkspace(ws);
      await Future.delayed(const Duration(milliseconds: 150));
    }
  }

  Future<void> _switchWorkspace(int wsId) async {
    try {
      await widget.morphic.switchWorkspace(wsId);
      setState(() {
        _activeWorkspaceId = wsId;
        _status = 'Switched to workspace #$wsId';
      });
    } catch (e) {
      setState(() => _status = 'Switch failed: $e');
    }
  }

  Future<void> _saveSession(String reason) async {
    try {
      await widget.morphic.saveSession(reason);
      setState(() => _status = '✓ Session saved ($reason)');
    } catch (e) {
      setState(() => _status = '✗ Save failed: $e');
    }
  }

  Future<void> _restoreSession() async {
    try {
      await widget.morphic.restoreSession();
      setState(() => _status = '✓ Session restored');
      await _refreshHealth();
    } catch (e) {
      setState(() => _status = '✗ Restore failed: $e');
    }
  }

  Future<void> _runValidation() async {
    if (_validationRunning) return;
    setState(() {
      _validationRunning = true;
      _status = 'Running 6 validation suites...';
    });
    try {
      final r = await widget.morphic.runValidation();
      setState(() {
        _validationResult = r;
        _validationRunning = false;
        _status = 'Validation: ${r['totalPassed']}/${r['totalTests']} passed';
      });
    } catch (e) {
      setState(() {
        _validationRunning = false;
        _status = 'Validation error: $e';
      });
    }
  }

  // ==========================================================================
  //  UI
  // ==========================================================================

  @override
  Widget build(BuildContext context) {
    return Scaffold(
      backgroundColor: const Color(0xFF0d1117),
      body: Column(
        children: [
          _buildHeader(),
          _buildStatusBar(),
          Expanded(
            child: Row(
              crossAxisAlignment: CrossAxisAlignment.start,
              children: [
                // Left: Commands
                SizedBox(width: 280, child: _buildCommandPanel()),
                Container(width: 1, color: const Color(0xFF30363d)),
                // Center: Topology
                Expanded(child: _buildTopologyView()),
                Container(width: 1, color: const Color(0xFF30363d)),
                // Right: Health
                SizedBox(width: 240, child: _buildHealthPanel()),
              ],
            ),
          ),
        ],
      ),
    );
  }

  Widget _buildHeader() {
    return Container(
      height: 44,
      padding: const EdgeInsets.symmetric(horizontal: 16),
      decoration: const BoxDecoration(
        color: Color(0xFF161b22),
        border: Border(bottom: BorderSide(color: Color(0xFF30363d))),
      ),
      child: Row(
        children: [
          Container(
            width: 8, height: 8,
            decoration: BoxDecoration(
              shape: BoxShape.circle,
              color: _bootstrapPhase == 'SteadyState'
                  ? const Color(0xFF3fb950) : const Color(0xFFf0883e),
              boxShadow: [BoxShadow(
                color: (_bootstrapPhase == 'SteadyState'
                    ? const Color(0xFF3fb950) : const Color(0xFFf0883e))
                    .withOpacity(0.5),
                blurRadius: 6,
              )],
            ),
          ),
          const SizedBox(width: 10),
          const Text('MORPHIC STUDIO',
              style: TextStyle(color: Color(0xFFe6edf3), fontWeight: FontWeight.w700,
                  fontSize: 13, letterSpacing: 1.5)),
          const SizedBox(width: 16),
          _pill(_bootstrapPhase, const Color(0xFF58a6ff)),
          const Spacer(),
          _hCounter('WS', '${_health['workspaceCount'] ?? 0}'),
          _hCounter('SRF', '${_health['surfaceCount'] ?? 0}'),
          _hCounter('RND', '${_health['healthyRenderers'] ?? 0}/${_health['totalRenderers'] ?? 0}'),
          if (_health['underPressure'] == true)
            _pill('PRESSURE', const Color(0xFFf85149)),
          const SizedBox(width: 8),
          IconButton(
            icon: const Icon(Icons.arrow_back, color: Color(0xFF8b949e), size: 18),
            onPressed: () => Navigator.of(context).pop(),
            tooltip: 'Back',
          ),
        ],
      ),
    );
  }

  Widget _pill(String text, Color color) {
    return Container(
      margin: const EdgeInsets.only(left: 8),
      padding: const EdgeInsets.symmetric(horizontal: 6, vertical: 2),
      decoration: BoxDecoration(
        color: color.withOpacity(0.15),
        borderRadius: BorderRadius.circular(4),
      ),
      child: Text(text, style: TextStyle(color: color, fontSize: 9,
          fontWeight: FontWeight.w700, fontFamily: 'Consolas')),
    );
  }

  Widget _hCounter(String label, String value) {
    return Padding(
      padding: const EdgeInsets.only(left: 12),
      child: Row(children: [
        Text(label, style: const TextStyle(color: Color(0xFF8b949e), fontSize: 9, fontWeight: FontWeight.w600)),
        const SizedBox(width: 4),
        Text(value, style: const TextStyle(color: Color(0xFFe6edf3), fontSize: 12,
            fontWeight: FontWeight.w700, fontFamily: 'Consolas')),
      ]),
    );
  }

  Widget _buildStatusBar() {
    return Container(
      width: double.infinity,
      padding: const EdgeInsets.symmetric(horizontal: 16, vertical: 6),
      color: const Color(0xFF0d1117),
      child: Text(_status, style: const TextStyle(
          color: Color(0xFF3fb950), fontSize: 11, fontFamily: 'Consolas')),
    );
  }

  // === COMMAND PANEL ===
  Widget _buildCommandPanel() {
    return SingleChildScrollView(
      padding: const EdgeInsets.all(12),
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.stretch,
        children: [
          _secLabel('LAUNCH'),
          const SizedBox(height: 6),
          _cmd('▶ Create Full Studio (5 surfaces)',
              _createFullStudio, const Color(0xFF238636),
              _workspaces.isEmpty && !_creating),
          const SizedBox(height: 4),
          _cmd('✕ Destroy All', _destroyAll,
              const Color(0xFFda3633), _workspaces.isNotEmpty && !_creating),

          const SizedBox(height: 16),
          _secLabel('ADD INDIVIDUAL SURFACE'),
          const SizedBox(height: 6),
          ..._workspaceTemplates.map((tmpl) => Padding(
            padding: const EdgeInsets.only(bottom: 3),
            child: _cmd(
              '+ ${tmpl.label} [${tmpl.role.name}]',
              () => _createWorkspaceFromTemplate(tmpl),
              const Color(0xFF21262d),
              !_creating,
            ),
          )),

          const SizedBox(height: 16),
          _secLabel('SESSION'),
          const SizedBox(height: 6),
          _cmd('💾 Save Session', () => _saveSession('intentionalPause'),
              const Color(0xFF21262d), true),
          const SizedBox(height: 3),
          _cmd('🔄 Restore Session', _restoreSession,
              const Color(0xFF21262d), true),

          const SizedBox(height: 16),
          _secLabel('VALIDATION'),
          const SizedBox(height: 6),
          _cmd(
            _validationRunning ? '⏳ Running...' : '🔬 Run All Validators',
            _runValidation, const Color(0xFF1f6feb), !_validationRunning,
          ),
          if (_validationResult.isNotEmpty) ...[
            const SizedBox(height: 8),
            _validationSummary(),
          ],
        ],
      ),
    );
  }

  Widget _validationSummary() {
    final suites = (_validationResult['suites'] as List?)?.cast<Map>() ?? [];
    final allPassed = _validationResult['allPassed'] == true;
    return Container(
      padding: const EdgeInsets.all(8),
      decoration: BoxDecoration(
        color: (allPassed ? const Color(0xFF3fb950) : const Color(0xFFf85149))
            .withOpacity(0.08),
        borderRadius: BorderRadius.circular(6),
        border: Border.all(color:
            (allPassed ? const Color(0xFF3fb950) : const Color(0xFFf85149))
                .withOpacity(0.3)),
      ),
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          Text('${_validationResult['totalPassed']}/${_validationResult['totalTests']} passed',
              style: TextStyle(
                  color: allPassed ? const Color(0xFF3fb950) : const Color(0xFFf85149),
                  fontSize: 12, fontWeight: FontWeight.w600)),
          const SizedBox(height: 4),
          ...suites.map((s) {
            final p = (s['failed'] ?? 0) == 0;
            return Padding(
              padding: const EdgeInsets.only(top: 2),
              child: Row(children: [
                Icon(p ? Icons.check_circle : Icons.cancel, size: 12,
                    color: p ? const Color(0xFF3fb950) : const Color(0xFFf85149)),
                const SizedBox(width: 6),
                Expanded(child: Text('${s['name']}',
                    style: const TextStyle(color: Color(0xFF8b949e), fontSize: 10))),
                Text('${s['passed']}/${s['total']}',
                    style: const TextStyle(color: Color(0xFF8b949e), fontSize: 10, fontFamily: 'Consolas')),
              ]),
            );
          }),
        ],
      ),
    );
  }

  // === TOPOLOGY VIEW (center) ===
  Widget _buildTopologyView() {
    if (_workspaces.isEmpty) {
      return Center(
        child: Column(
          mainAxisSize: MainAxisSize.min,
          children: [
            Icon(Icons.layers_outlined, size: 48,
                color: const Color(0xFF30363d).withOpacity(0.5)),
            const SizedBox(height: 12),
            const Text('No active surfaces',
                style: TextStyle(color: Color(0xFF8b949e), fontSize: 14)),
            const SizedBox(height: 8),
            const Text(
              '"Create Full Studio" spawns 5 real native surfaces:\n'
              '  • 2× workspace (hidden from Alt+Tab)\n'
              '  • 1× detached (shows in Alt+Tab)\n'
              '  • 1× toolPalette (floating, non-resizable)\n'
              '  • 1× overlay (topmost HUD)',
              textAlign: TextAlign.center,
              style: TextStyle(color: Color(0xFF484f58), fontSize: 11,
                  fontFamily: 'Consolas', height: 1.6),
            ),
          ],
        ),
      );
    }

    return SingleChildScrollView(
      padding: const EdgeInsets.all(16),
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          _secLabel('ACTIVE SURFACES — ${_workspaces.length}'),
          const SizedBox(height: 12),
          ..._workspaces.map((ws) => _surfaceCard(ws)),
          const SizedBox(height: 20),
          _secLabel('RUNTIME TOPOLOGY'),
          const SizedBox(height: 8),
          _runtimeTree(),
        ],
      ),
    );
  }

  Widget _surfaceCard(_LiveWorkspace ws) {
    final isActive = ws.workspaceId == _activeWorkspaceId;
    final c = Color(ws.template.color | 0xFF000000);
    final roleColor = _roleColor(ws.template.role);

    return Container(
      margin: const EdgeInsets.only(bottom: 8),
      decoration: BoxDecoration(
        color: const Color(0xFF161b22),
        borderRadius: BorderRadius.circular(8),
        border: Border.all(color: isActive ? const Color(0xFF1f6feb) : const Color(0xFF30363d),
            width: isActive ? 2 : 1),
      ),
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          // Color bar
          Container(height: 4, decoration: BoxDecoration(
            color: c, borderRadius: const BorderRadius.vertical(top: Radius.circular(7)),
          )),
          Padding(
            padding: const EdgeInsets.all(10),
            child: Row(
              children: [
                Icon(_actIcon(ws.template.activity), size: 16, color: c),
                const SizedBox(width: 8),
                Expanded(
                  child: Column(
                    crossAxisAlignment: CrossAxisAlignment.start,
                    children: [
                      Row(children: [
                        Text(ws.template.label, style: const TextStyle(
                            color: Color(0xFFe6edf3), fontSize: 12, fontWeight: FontWeight.w600)),
                        const SizedBox(width: 8),
                        Container(
                          padding: const EdgeInsets.symmetric(horizontal: 5, vertical: 1),
                          decoration: BoxDecoration(
                            color: roleColor.withOpacity(0.15),
                            borderRadius: BorderRadius.circular(3),
                          ),
                          child: Text(ws.template.role.name.toUpperCase(),
                              style: TextStyle(color: roleColor, fontSize: 8,
                                  fontWeight: FontWeight.w700, letterSpacing: 0.5)),
                        ),
                        if (isActive) ...[
                          const SizedBox(width: 4),
                          Container(
                            padding: const EdgeInsets.symmetric(horizontal: 5, vertical: 1),
                            decoration: BoxDecoration(
                              color: const Color(0xFF3fb950).withOpacity(0.15),
                              borderRadius: BorderRadius.circular(3),
                            ),
                            child: const Text('ACTIVE', style: TextStyle(
                                color: Color(0xFF3fb950), fontSize: 8, fontWeight: FontWeight.w700)),
                          ),
                        ],
                      ]),
                      const SizedBox(height: 3),
                      Text(
                        'SRF#${ws.surfaceId}  WS#${ws.workspaceId}  '
                        '${ws.template.width}×${ws.template.height}  '
                        '${ws.rendererAttached ? "Flutter ✓" : "No renderer ✗"}',
                        style: const TextStyle(color: Color(0xFF8b949e), fontSize: 9, fontFamily: 'Consolas'),
                      ),
                      const SizedBox(height: 2),
                      Text(ws.template.description,
                          style: const TextStyle(color: Color(0xFF484f58), fontSize: 9)),
                    ],
                  ),
                ),
                if (!isActive)
                  _tinyBtn('Switch', const Color(0xFF1f6feb),
                      () => _switchWorkspace(ws.workspaceId)),
                const SizedBox(width: 4),
                _tinyBtn('✕', const Color(0xFFda3633),
                    () => _destroyWorkspace(ws)),
              ],
            ),
          ),
        ],
      ),
    );
  }

  Widget _runtimeTree() {
    return Container(
      padding: const EdgeInsets.all(12),
      decoration: BoxDecoration(
        color: const Color(0xFF0d1117),
        borderRadius: BorderRadius.circular(8),
        border: Border.all(color: const Color(0xFF30363d)),
      ),
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          _tLine('MORPHIC_WORKSPACE_WINDOW (native shell)', const Color(0xFF58a6ff)),
          _tLine('├── Compositor', const Color(0xFF8b949e)),
          _tLine('│   ├── SurfaceRegistry (${_health['surfaceCount'] ?? 0} surfaces)', const Color(0xFF8b949e)),
          _tLine('│   ├── RendererManager (${_health['totalRenderers'] ?? 0} engines)', const Color(0xFF8b949e)),
          _tLine('│   └── ActivationManager', const Color(0xFF8b949e)),
          _tLine('├── WorkspaceRuntime (${_health['workspaceCount'] ?? 0} workspaces)', const Color(0xFFd2a8ff)),
          _tLine('├── SessionRuntime', const Color(0xFFf0883e)),
          _tLine('└── FlutterRenderer(s)', const Color(0xFF3fb950)),
          _tLine('    ├── ControlPanel [this view]', const Color(0xFF3fb950)),
          ..._workspaces.map((ws) {
            final rc = _roleColor(ws.template.role);
            return _tLine(
              '    ├── ${ws.template.label} [${ws.template.role.name}] SRF#${ws.surfaceId}'
              '${ws.rendererAttached ? " ✓" : " ✗"}',
              rc,
            );
          }),
        ],
      ),
    );
  }

  Widget _tLine(String text, Color color) {
    return Padding(
      padding: const EdgeInsets.symmetric(vertical: 1),
      child: Text(text, style: TextStyle(
          color: color, fontSize: 10, fontFamily: 'Consolas', height: 1.4)),
    );
  }

  // === HEALTH PANEL ===
  Widget _buildHealthPanel() {
    return SingleChildScrollView(
      padding: const EdgeInsets.all(12),
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          _secLabel('RUNTIME HEALTH'),
          const SizedBox(height: 8),
          _hRow('Workspaces', '${_health['workspaceCount'] ?? 0}'),
          _hRow('Surfaces', '${_health['surfaceCount'] ?? 0}'),
          _hRow('Renderers', '${_health['healthyRenderers'] ?? 0}/${_health['totalRenderers'] ?? 0}'),
          _hRow('Pressure', _health['underPressure'] == true ? 'YES' : 'NO',
              alert: _health['underPressure'] == true),
          _hRow('Degraded', _health['degradedMode'] == true ? 'YES' : 'NO',
              alert: _health['degradedMode'] == true),
          _hRow('Bootstrap', _bootstrapPhase),

          const SizedBox(height: 16),
          _secLabel('SURFACE ROLES'),
          const SizedBox(height: 6),
          _roleRow('workspace', 'Hidden from Alt+Tab, shell-owned', const Color(0xFF58a6ff)),
          _roleRow('detached', 'Alt+Tab participant, taskbar entry', const Color(0xFFd2a8ff)),
          _roleRow('toolPalette', 'Floating utility, non-resizable', const Color(0xFFf0883e)),
          _roleRow('overlay', 'Topmost HUD, non-interactive', const Color(0xFFf7d731)),

          const SizedBox(height: 16),
          _secLabel('SUMMARY'),
          const SizedBox(height: 6),
          Container(
            padding: const EdgeInsets.all(8),
            decoration: BoxDecoration(
              color: const Color(0xFF0d1117),
              borderRadius: BorderRadius.circular(6),
            ),
            child: Text(_health['summary']?.toString() ?? 'No data',
                style: const TextStyle(color: Color(0xFF8b949e), fontSize: 10, fontFamily: 'Consolas')),
          ),
        ],
      ),
    );
  }

  Widget _hRow(String label, String value, {bool alert = false}) {
    return Padding(
      padding: const EdgeInsets.symmetric(vertical: 3),
      child: Row(children: [
        Expanded(child: Text(label, style: const TextStyle(color: Color(0xFF8b949e), fontSize: 11))),
        Text(value, style: TextStyle(
            color: alert ? const Color(0xFFf85149) : const Color(0xFFe6edf3),
            fontSize: 12, fontWeight: FontWeight.w600, fontFamily: 'Consolas')),
      ]),
    );
  }

  Widget _roleRow(String role, String desc, Color color) {
    return Padding(
      padding: const EdgeInsets.only(bottom: 6),
      child: Row(
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          Container(width: 4, height: 4, margin: const EdgeInsets.only(top: 4),
            decoration: BoxDecoration(shape: BoxShape.circle, color: color)),
          const SizedBox(width: 6),
          Expanded(child: Column(
            crossAxisAlignment: CrossAxisAlignment.start,
            children: [
              Text(role, style: TextStyle(color: color, fontSize: 10, fontWeight: FontWeight.w600)),
              Text(desc, style: const TextStyle(color: Color(0xFF484f58), fontSize: 9)),
            ],
          )),
        ],
      ),
    );
  }

  // === SHARED ===
  Widget _secLabel(String t) => Text(t, style: const TextStyle(
      color: Color(0xFF8b949e), fontSize: 10, fontWeight: FontWeight.w700, letterSpacing: 1));

  Widget _cmd(String label, VoidCallback onPressed, Color bg, bool enabled) {
    return SizedBox(
      width: double.infinity, height: 30,
      child: ElevatedButton(
        onPressed: enabled ? onPressed : null,
        style: ElevatedButton.styleFrom(
          backgroundColor: bg, foregroundColor: const Color(0xFFe6edf3),
          disabledBackgroundColor: const Color(0xFF161b22),
          disabledForegroundColor: const Color(0xFF484f58),
          padding: const EdgeInsets.symmetric(horizontal: 10),
          shape: RoundedRectangleBorder(borderRadius: BorderRadius.circular(6)),
          elevation: 0,
        ),
        child: Align(alignment: Alignment.centerLeft,
            child: Text(label, style: const TextStyle(fontSize: 11))),
      ),
    );
  }

  Widget _tinyBtn(String label, Color color, VoidCallback onPressed) {
    return GestureDetector(
      onTap: onPressed,
      child: Container(
        padding: const EdgeInsets.symmetric(horizontal: 6, vertical: 2),
        decoration: BoxDecoration(
          color: color.withOpacity(0.15), borderRadius: BorderRadius.circular(4)),
        child: Text(label, style: TextStyle(color: color, fontSize: 9, fontWeight: FontWeight.w600)),
      ),
    );
  }

  Color _roleColor(SurfaceRole role) {
    switch (role) {
      case SurfaceRole.workspace: return const Color(0xFF58a6ff);
      case SurfaceRole.detached: return const Color(0xFFd2a8ff);
      case SurfaceRole.toolPalette: return const Color(0xFFf0883e);
      case SurfaceRole.overlay: return const Color(0xFFf7d731);
    }
  }

  IconData _actIcon(String a) {
    switch (a) {
      case 'editing': return Icons.edit;
      case 'debugging': return Icons.bug_report;
      case 'monitoring': return Icons.monitor;
      case 'inspecting': return Icons.search;
      case 'reference': return Icons.menu_book;
      default: return Icons.workspaces;
    }
  }
}

class _WorkspaceTemplate {
  final String label;
  final String activity;
  final String disposition;
  final SurfaceRole role;
  final int color;
  final int width, height, x, y;
  final String description;

  const _WorkspaceTemplate({
    required this.label, required this.activity, required this.disposition,
    required this.role, required this.color,
    required this.width, required this.height, required this.x, required this.y,
    required this.description,
  });
}

class _LiveWorkspace {
  final int workspaceId;
  final int surfaceId;
  final _WorkspaceTemplate template;
  final bool rendererAttached;

  _LiveWorkspace({
    required this.workspaceId, required this.surfaceId,
    required this.template, required this.rendererAttached,
  });
}
