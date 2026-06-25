import 'dart:convert';
import 'dart:io';

import 'package:morphic/morphic.dart';

/// A single note, persisted as JSON.
class Note {
  Note({
    required this.id,
    this.title = 'Untitled',
    this.body = '',
    DateTime? updated,
  }) : updated = updated ?? DateTime.now();

  final String id;
  String title;
  String body;
  DateTime updated;

  Map<String, Object?> toJson() => {
    'id': id,
    'title': title,
    'body': body,
    'updated': updated.toIso8601String(),
  };

  static Note fromJson(Map<String, Object?> j) => Note(
    id: j['id'] as String,
    title: (j['title'] ?? 'Untitled') as String,
    body: (j['body'] ?? '') as String,
    updated: DateTime.tryParse((j['updated'] ?? '') as String) ?? DateTime.now(),
  );
}

/// File-backed store shared by every surface.
///
/// Each surface is its own Flutter engine, but they run in one process and
/// share the filesystem — so a JSON file is the single source of truth. After
/// any write we announce `notes.changed` over [AppBus]; surfaces reload. That's
/// the whole multi-window data model: **persist + broadcast, never reach across
/// windows directly.**
class NotesStore {
  static File get _file {
    final home =
        Platform.environment['APPDATA'] ??
        Platform.environment['HOME'] ??
        Directory.systemTemp.path;
    final dir = Directory('$home${Platform.pathSeparator}.morphic_notes')
      ..createSync(recursive: true);
    return File('${dir.path}${Platform.pathSeparator}notes.json');
  }

  static List<Note> load() {
    final f = _file;
    if (!f.existsSync()) return _seed();
    try {
      final raw = (jsonDecode(f.readAsStringSync()) as List)
          .cast<Map<String, Object?>>();
      final notes = raw.map(Note.fromJson).toList();
      return notes.isEmpty ? _seed() : notes;
    } catch (_) {
      return _seed();
    }
  }

  static Note byId(String id) =>
      load().firstWhere((n) => n.id == id, orElse: () => Note(id: id));

  static void _saveAll(List<Note> notes) =>
      _file.writeAsStringSync(jsonEncode([for (final n in notes) n.toJson()]));

  /// Insert or update, bump the timestamp, and tell every surface.
  static void upsert(Note note) {
    final notes = load();
    final i = notes.indexWhere((n) => n.id == note.id);
    note.updated = DateTime.now();
    if (i >= 0) {
      notes[i] = note;
    } else {
      notes.insert(0, note);
    }
    _saveAll(notes);
    AppBus.broadcast('notes.changed', {'id': note.id});
  }

  /// A fresh, untitled note at the top of the list.
  static Note create() {
    final note = Note(id: 'n${DateTime.now().millisecondsSinceEpoch}', title: '');
    _saveAll(load()..insert(0, note));
    AppBus.broadcast('notes.changed', {'id': note.id});
    return note;
  }

  /// Remove a note. Returns it (with its old index) so a delete can be undone.
  static (Note, int)? delete(String id) {
    final notes = load();
    final i = notes.indexWhere((n) => n.id == id);
    if (i < 0) return null;
    final removed = notes.removeAt(i);
    _saveAll(notes);
    AppBus.broadcast('notes.changed', {'id': id});
    AppBus.broadcast('note.deleted', {'id': id});
    return (removed, i);
  }

  /// Re-insert a note at a position (the inverse of [delete], for Undo).
  static void insertAt(Note note, int index) {
    final notes = load();
    notes.insert(index.clamp(0, notes.length), note);
    _saveAll(notes);
    AppBus.broadcast('notes.changed', {'id': note.id});
  }

  static List<Note> _seed() {
    final seed = [
      Note(
        id: 'n1',
        title: 'Welcome to Morphic Notes',
        body:
            'This is a real multi-window app.\n\nThe list, editor and inspector '
            'are three separate windows talking over AppBus — not panels in one '
            'window.',
      ),
      Note(
        id: 'n2',
        title: 'Try it',
        body:
            'Pick a note → it opens in the Editor window and the Inspector '
            'updates live. Edits persist across relaunch.',
      ),
    ];
    _saveAll(seed);
    return seed;
  }
}
