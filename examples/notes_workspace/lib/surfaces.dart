import 'package:flutter/material.dart';
import 'package:morphic/morphic.dart';

import 'store.dart';

// AppBus topics shared by the three windows.
const _selected = 'note.selected'; // {id}  — list → editor + inspector
const _changed = 'notes.changed'; // {id}  — any write → everyone reloads

/// Window 1 — the notes list (the root workspace).
class NotesListSurface extends StatefulWidget {
  const NotesListSurface({super.key});
  @override
  State<NotesListSurface> createState() => _NotesListSurfaceState();
}

class _NotesListSurfaceState extends State<NotesListSurface> {
  List<Note> _notes = NotesStore.load();
  String? _selectedId;

  @override
  void initState() {
    super.initState();
    AppBus.on(_changed, (_) => setState(() => _notes = NotesStore.load()));
  }

  void _open(Note n) {
    setState(() => _selectedId = n.id);
    AppBus.broadcast(_selected, {'id': n.id});
  }

  @override
  Widget build(BuildContext context) {
    return Scaffold(
      appBar: AppBar(
        title: const Text('Notes'),
        actions: [
          IconButton(
            tooltip: 'New note',
            icon: const Icon(Icons.add),
            onPressed: () => _open(NotesStore.create()),
          ),
        ],
      ),
      body: ListView.separated(
        itemCount: _notes.length,
        separatorBuilder: (_, __) => const Divider(height: 1),
        itemBuilder: (_, i) {
          final n = _notes[i];
          return ListTile(
            selected: n.id == _selectedId,
            title: Text(
              n.title.isEmpty ? 'Untitled' : n.title,
              maxLines: 1,
              overflow: TextOverflow.ellipsis,
            ),
            subtitle: Text(
              n.body.replaceAll('\n', ' '),
              maxLines: 1,
              overflow: TextOverflow.ellipsis,
            ),
            onTap: () => _open(n),
          );
        },
      ),
    );
  }
}

/// Window 2 — the editor (its own window/engine).
class EditorSurface extends StatefulWidget {
  const EditorSurface({super.key});
  @override
  State<EditorSurface> createState() => _EditorSurfaceState();
}

class _EditorSurfaceState extends State<EditorSurface> {
  final _title = TextEditingController();
  final _body = TextEditingController();
  Note? _note;

  @override
  void initState() {
    super.initState();
    // Open whichever note the list selects.
    AppBus.on(_selected, (p) => _load(p['id'] as String));
  }

  void _load(String id) {
    final n = NotesStore.byId(id);
    setState(() {
      _note = n;
      _title.text = n.title;
      _body.text = n.body;
    });
  }

  void _save() {
    final n = _note;
    if (n == null) return;
    n
      ..title = _title.text
      ..body = _body.text;
    NotesStore.upsert(n); // persists + broadcasts notes.changed
  }

  @override
  Widget build(BuildContext context) {
    if (_note == null) {
      return const Scaffold(
        body: Center(child: Text('Select a note from the list →')),
      );
    }
    return Scaffold(
      appBar: AppBar(
        title: const Text('Editor'),
        actions: [
          IconButton(
            tooltip: 'Close this window',
            icon: const Icon(Icons.close),
            onPressed: MorphicSurface.close, // a surface only acts on itself
          ),
        ],
      ),
      body: Padding(
        padding: const EdgeInsets.all(16),
        child: Column(
          crossAxisAlignment: CrossAxisAlignment.stretch,
          children: [
            TextField(
              controller: _title,
              style: Theme.of(context).textTheme.titleLarge,
              decoration: const InputDecoration(
                hintText: 'Title',
                border: InputBorder.none,
              ),
              onChanged: (_) => _save(),
            ),
            const SizedBox(height: 8),
            Expanded(
              child: TextField(
                controller: _body,
                maxLines: null,
                expands: true,
                textAlignVertical: TextAlignVertical.top,
                decoration: const InputDecoration(
                  hintText: 'Start writing…',
                  border: InputBorder.none,
                ),
                onChanged: (_) => _save(),
              ),
            ),
          ],
        ),
      ),
    );
  }
}

/// Window 3 — the inspector (live metadata for the selected note).
class InspectorSurface extends StatefulWidget {
  const InspectorSurface({super.key});
  @override
  State<InspectorSurface> createState() => _InspectorSurfaceState();
}

class _InspectorSurfaceState extends State<InspectorSurface> {
  Note? _note;

  @override
  void initState() {
    super.initState();
    AppBus.on(_selected, (p) => _show(p['id'] as String));
    AppBus.on(_changed, (p) {
      if (_note != null && p['id'] == _note!.id) _show(_note!.id);
    });
  }

  void _show(String id) => setState(() => _note = NotesStore.byId(id));

  @override
  Widget build(BuildContext context) {
    final n = _note;
    return Scaffold(
      appBar: AppBar(title: const Text('Inspector')),
      body: n == null
          ? const Center(child: Text('No note selected'))
          : ListView(
              padding: const EdgeInsets.all(16),
              children: [
                _row('Title', n.title.isEmpty ? 'Untitled' : n.title),
                _row('Words', '${_words(n.body)}'),
                _row('Characters', '${n.body.length}'),
                _row('Updated', n.updated.toLocal().toString().split('.').first),
                _row('ID', n.id),
              ],
            ),
    );
  }

  int _words(String s) =>
      s.trim().isEmpty ? 0 : s.trim().split(RegExp(r'\s+')).length;

  Widget _row(String k, String v) => Padding(
    padding: const EdgeInsets.symmetric(vertical: 10),
    child: Column(
      crossAxisAlignment: CrossAxisAlignment.start,
      children: [
        Text(k, style: const TextStyle(fontSize: 11, color: Colors.white54)),
        const SizedBox(height: 2),
        Text(v),
      ],
    ),
  );
}
