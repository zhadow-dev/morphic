import 'package:flutter/foundation.dart';

import 'app_bus.dart';

/// MORPHIC SECOND APP (M2.2D) — NotesSession: the app's session model. EARNED by multiple
/// documents (registry + active doc + the cracks). App-SPECIFIC on purpose — NOT the generic
/// `AppSession` (still too early; one workflow). It will generalize only when a *second* app's
/// session pain matches this shape.
///
/// HONEST MULTI-ENGINE MODEL: there is no single shared object across engines. This is a
/// per-engine **replica** built from the [AppBus] event stream — the "central" truth is the stream
/// of `notes.docOpened` / `notes.activeDoc` / `notes.docClosed`, not a shared instance. Any surface
/// that needs the session (the inspector today, a command palette tomorrow) holds its own.
///
/// SPACE: it operates entirely in DOCUMENT space (doc ids). It never touches surface ids, planes,
/// HWNDs, or input — the editor surface bridges doc-space ↔ surface-space. NotesSession is
/// runtime-agnostic; the firewall holds upward too (the app doesn't reach into the runtime).
class NotesSession extends ChangeNotifier {
  final Map<String, String> _docs = {}; // docId → title (the registry)
  final Map<String, String> _surfaceToDoc = {}; // surfaceId → docId (M2.1D lifecycle mapping)
  String? _activeDocId;
  Map _activeDoc = const {}; // active doc's full payload (title/words/outline)

  Map<String, String> get docs => Map.unmodifiable(_docs);
  String? get activeDocId => _activeDocId;
  Map get activeDoc => _activeDoc;
  bool get hasActive => _activeDocId != null;

  /// The native surface id hosting [docId] (for directed activation — "switch to this doc").
  /// Null if not yet known (the doc's surfaceId handshake hasn't landed).
  String? surfaceForDoc(String docId) {
    for (final e in _surfaceToDoc.entries) {
      if (e.value == docId) return e.key;
    }
    return null;
  }

  NotesSession() {
    AppBus.on('notes.docOpened', (p) {
      final id = p['docId'] as String?;
      if (id == null) return;
      _docs.putIfAbsent(id, () => (p['title'] as String?) ?? 'Untitled');
      final sid = p['surfaceId'] as String?;
      if (sid != null) _surfaceToDoc[sid] = id; // so surface.destroyed can find this doc
      notifyListeners();
    });
    AppBus.on('notes.activeDoc', (p) {
      final id = p['docId'] as String?;
      if (id == null) return;
      // M2.2E.1 — SELECTION ONLY: activeDoc marks WHICH doc is active; it never creates existence.
      // Existence comes exclusively from docOpened/docClosed. (Pre-fix, putIfAbsent here let docs
      // "appear" only when focused = the empty-command-palette bug: a fresh session saw nothing
      // until each surface was manually focused.) Refresh the title only if the doc is known.
      final t = p['title'] as String?;
      if (t != null && t.isNotEmpty && _docs.containsKey(id)) _docs[id] = t;
      _activeDocId = id;
      _activeDoc = p;
      notifyListeners();
    });
    // Fast path: an editor announced its own close (soft dispose).
    AppBus.on('notes.docClosed', (p) => _removeDoc(p['docId'] as String?));
    // Reliable path (M2.1D): the runtime says a surface died → remove the doc it hosted, even on a
    // hard HWND destroy where Dart dispose never ran. This is the ghost-doc fix.
    AppBus.on('surface.destroyed', (p) {
      final sid = p['surfaceId'] as String?;
      if (sid == null) return;
      _removeDoc(_surfaceToDoc.remove(sid));
    });
    // M2.2E.1 — ROSTER SYNC (late-join catch-up). The AppBus is fire-and-forget with no retention,
    // so a session born AFTER the editors (a command palette spawned on Ctrl+K) misses every prior
    // notes.docOpened and starts empty. Subscriptions are wired above FIRST, then we ask live
    // editors to re-announce; each responds with a fresh notes.docOpened. Pure DATA, app-layer,
    // zero runtime change — the runtime never learns what a "doc" is. Idempotent (putIfAbsent), so
    // multiple sessions requesting is harmless.
    AppBus.broadcast('notes.syncRequest', const {});
  }

  void _removeDoc(String? docId) {
    if (docId == null || !_docs.containsKey(docId)) return;
    _docs.remove(docId);
    _surfaceToDoc.removeWhere((sid, d) => d == docId);
    if (_activeDocId == docId) {
      // Active-doc FALLBACK — never leave the active pointer at a dead doc.
      _activeDocId = _docs.keys.isNotEmpty ? _docs.keys.last : null;
      _activeDoc = const {};
    }
    notifyListeners();
  }
}
