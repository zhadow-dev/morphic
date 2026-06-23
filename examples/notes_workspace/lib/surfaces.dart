import 'dart:async';

import 'package:flutter/material.dart';
import 'package:flutter/services.dart';
import 'package:morphic/morphic.dart';

import 'store.dart';

// AppBus topics shared by the three windows.
const _selected = 'note.selected'; // {id}  — list → editor + inspector
const _changed = 'notes.changed'; // {id}   — any write → everyone reloads
const _deleted = 'note.deleted'; // {id}    — a note was removed
const _focusTitle = 'note.focusTitle'; // {id} — new note → editor focuses title

// ── palette ──────────────────────────────────────────────────────────────────
const _bg = Color(0xFF0E1017);
const _accent = Color(0xFF7C5CFF);
const _txt = Color(0xFFECEDF1);
const _txt2 = Color(0x8AFFFFFF); // ~54%
const _txt3 = Color(0x59FFFFFF); // ~35%
const _hair = Color(0x12FFFFFF); // ~7%
const _hoverBg = Color(0x0AFFFFFF); // ~4%
const _selBg = Color(0x247C5CFF); // accent ~14%

String _ago(DateTime t) {
  final d = DateTime.now().difference(t);
  if (d.inSeconds < 45) return 'just now';
  if (d.inMinutes < 60) return '${d.inMinutes}m ago';
  if (d.inHours < 24) return '${d.inHours}h ago';
  if (d.inDays < 7) return '${d.inDays}d ago';
  return '${t.year}-${t.month.toString().padLeft(2, '0')}-'
      '${t.day.toString().padLeft(2, '0')}';
}

int _wordCount(String s) =>
    s.trim().isEmpty ? 0 : s.trim().split(RegExp(r'\s+')).length;

// ── shared chrome ─────────────────────────────────────────────────────────────
class _Header extends StatelessWidget {
  const _Header(this.title, {this.trailing = const [], this.subtitle});
  final String title;
  final String? subtitle;
  final List<Widget> trailing;

  @override
  Widget build(BuildContext context) {
    return Container(
      height: 46,
      padding: const EdgeInsets.only(left: 18, right: 8),
      decoration: const BoxDecoration(
        border: Border(bottom: BorderSide(color: _hair)),
      ),
      child: Row(
        children: [
          Text(
            title,
            style: const TextStyle(
              fontSize: 13.5,
              fontWeight: FontWeight.w600,
              letterSpacing: 0.2,
              color: _txt,
            ),
          ),
          if (subtitle != null) ...[
            const SizedBox(width: 8),
            Text(subtitle!, style: const TextStyle(fontSize: 12, color: _txt3)),
          ],
          const Spacer(),
          ...trailing,
        ],
      ),
    );
  }
}

class _IconBtn extends StatelessWidget {
  const _IconBtn(this.icon, this.tooltip, this.onTap);
  final IconData icon;
  final String tooltip;
  final VoidCallback onTap;

  @override
  Widget build(BuildContext context) {
    return IconButton(
      tooltip: tooltip,
      iconSize: 18,
      visualDensity: VisualDensity.compact,
      color: _txt2,
      hoverColor: _hoverBg,
      icon: Icon(icon),
      onPressed: onTap,
    );
  }
}

class _ShortcutBar extends StatelessWidget {
  const _ShortcutBar(this.items);
  final List<(String, String)> items; // (keys, label)

  @override
  Widget build(BuildContext context) {
    return Container(
      height: 30,
      padding: const EdgeInsets.symmetric(horizontal: 14),
      decoration: const BoxDecoration(
        border: Border(top: BorderSide(color: _hair)),
      ),
      child: Row(
        children: [
          for (final (keys, label) in items) ...[
            _Kbd(keys),
            const SizedBox(width: 5),
            Text(label, style: const TextStyle(fontSize: 10.5, color: _txt3)),
            const SizedBox(width: 14),
          ],
        ],
      ),
    );
  }
}

class _Kbd extends StatelessWidget {
  const _Kbd(this.text);
  final String text;
  @override
  Widget build(BuildContext context) => Container(
    padding: const EdgeInsets.symmetric(horizontal: 5, vertical: 1.5),
    decoration: BoxDecoration(
      color: _hoverBg,
      borderRadius: BorderRadius.circular(4),
      border: Border.all(color: _hair),
    ),
    child: Text(
      text,
      style: const TextStyle(
        fontSize: 10,
        height: 1.1,
        color: _txt2,
        fontFeatures: [FontFeature.tabularFigures()],
      ),
    ),
  );
}

// ── Window 1 — the notes list (the root workspace) ───────────────────────────
class NotesListSurface extends StatefulWidget {
  const NotesListSurface({super.key});
  @override
  State<NotesListSurface> createState() => _NotesListSurfaceState();
}

class _NotesListSurfaceState extends State<NotesListSurface> {
  List<Note> _notes = NotesStore.load();
  String? _selectedId;
  String? _renamingId;
  final _renameCtrl = TextEditingController();
  final _renameFocus = FocusNode();
  final _scroll = ScrollController();

  @override
  void initState() {
    super.initState();
    AppBus.on(_changed, (_) => _reload());
    // Open the most recent note on boot so the workspace feels alive — but after
    // a beat, so the editor/inspector engines have registered their listeners.
    WidgetsBinding.instance.addPostFrameCallback((_) {
      Future.delayed(const Duration(milliseconds: 450), () {
        if (mounted && _selectedId == null && _notes.isNotEmpty) {
          _open(_notes.first);
        }
      });
    });
  }

  @override
  void dispose() {
    _renameCtrl.dispose();
    _renameFocus.dispose();
    _scroll.dispose();
    super.dispose();
  }

  Note? get _current {
    for (final n in _notes) {
      if (n.id == _selectedId) return n;
    }
    return null;
  }

  void _reload() {
    setState(() {
      _notes = NotesStore.load();
      if (_selectedId != null && !_notes.any((n) => n.id == _selectedId)) {
        _selectedId = _notes.isNotEmpty ? _notes.first.id : null;
      }
    });
  }

  void _open(Note n) {
    setState(() => _selectedId = n.id);
    AppBus.broadcast(_selected, {'id': n.id});
  }

  void _newNote() {
    final n = NotesStore.create();
    setState(() => _notes = NotesStore.load());
    _open(n);
    AppBus.broadcast(_focusTitle, {'id': n.id});
  }

  void _delete(String id) {
    final res = NotesStore.delete(id);
    if (res == null) return;
    final (removed, idx) = res;
    setState(() => _notes = NotesStore.load());
    if (_selectedId == id) {
      if (_notes.isNotEmpty) {
        _open(_notes[idx.clamp(0, _notes.length - 1)]);
      } else {
        setState(() => _selectedId = null);
      }
    }
    ScaffoldMessenger.of(context)
      ..clearSnackBars()
      ..showSnackBar(
        SnackBar(
          behavior: SnackBarBehavior.floating,
          duration: const Duration(seconds: 4),
          content: Text(
            'Deleted "${removed.title.isEmpty ? 'Untitled' : removed.title}"',
          ),
          action: SnackBarAction(
            label: 'Undo',
            onPressed: () {
              NotesStore.insertAt(removed, idx);
              _reload();
              _open(removed);
            },
          ),
        ),
      );
  }

  void _move(int delta) {
    if (_notes.isEmpty || _renamingId != null) return;
    final cur = _notes.indexWhere((n) => n.id == _selectedId);
    final next = (cur < 0 ? 0 : cur + delta).clamp(0, _notes.length - 1);
    _open(_notes[next]);
  }

  void _renameStart(Note n) {
    setState(() {
      _renamingId = n.id;
      _renameCtrl.text = n.title;
    });
    WidgetsBinding.instance.addPostFrameCallback((_) {
      _renameFocus.requestFocus();
      _renameCtrl.selection = TextSelection(
        baseOffset: 0,
        extentOffset: _renameCtrl.text.length,
      );
    });
  }

  void _renameCommit() {
    final id = _renamingId;
    if (id != null) {
      final n = NotesStore.byId(id)..title = _renameCtrl.text.trim();
      NotesStore.upsert(n);
    }
    setState(() => _renamingId = null);
  }

  void _renameCancel() => setState(() => _renamingId = null);

  @override
  Widget build(BuildContext context) {
    return Scaffold(
      backgroundColor: _bg,
      body: CallbackShortcuts(
        bindings: {
          const SingleActivator(LogicalKeyboardKey.keyN, control: true):
              _newNote,
          const SingleActivator(LogicalKeyboardKey.f2): () {
            final n = _current;
            if (n != null && _renamingId == null) _renameStart(n);
          },
          const SingleActivator(LogicalKeyboardKey.delete): () {
            if (_renamingId == null && _selectedId != null) {
              _delete(_selectedId!);
            }
          },
          const SingleActivator(LogicalKeyboardKey.arrowDown): () => _move(1),
          const SingleActivator(LogicalKeyboardKey.arrowUp): () => _move(-1),
          const SingleActivator(LogicalKeyboardKey.escape): () {
            if (_renamingId != null) _renameCancel();
          },
        },
        child: Focus(
          autofocus: true,
          child: Column(
            children: [
              _Header(
                'Notes',
                subtitle: _notes.isEmpty ? null : '${_notes.length}',
                trailing: [
                  _IconBtn(Icons.add_rounded, 'New note (Ctrl+N)', _newNote),
                ],
              ),
              Expanded(child: _notes.isEmpty ? _empty() : _list()),
              const _ShortcutBar([
                ('Ctrl N', 'New'),
                ('F2', 'Rename'),
                ('Del', 'Delete'),
                ('↑↓', 'Move'),
              ]),
            ],
          ),
        ),
      ),
    );
  }

  Widget _empty() => Center(
    child: Column(
      mainAxisSize: MainAxisSize.min,
      children: [
        const Icon(Icons.notes_rounded, size: 34, color: _txt3),
        const SizedBox(height: 12),
        const Text('No notes yet', style: TextStyle(color: _txt2)),
        const SizedBox(height: 14),
        FilledButton.icon(
          style: FilledButton.styleFrom(backgroundColor: _accent),
          onPressed: _newNote,
          icon: const Icon(Icons.add_rounded, size: 18),
          label: const Text('New note'),
        ),
      ],
    ),
  );

  Widget _list() => ListView.builder(
    controller: _scroll,
    padding: const EdgeInsets.symmetric(vertical: 6),
    itemCount: _notes.length,
    itemBuilder: (_, i) {
      final n = _notes[i];
      if (n.id == _renamingId) return _renameRow(n);
      return _NoteRow(
        note: n,
        selected: n.id == _selectedId,
        onTap: () => _open(n),
        onRename: () => _renameStart(n),
        onDelete: () => _delete(n.id),
      );
    },
  );

  Widget _renameRow(Note n) => Container(
    color: _selBg,
    padding: const EdgeInsets.fromLTRB(16, 8, 12, 8),
    child: TextField(
      controller: _renameCtrl,
      focusNode: _renameFocus,
      style: const TextStyle(
        fontSize: 14.5,
        fontWeight: FontWeight.w600,
        color: _txt,
      ),
      cursorColor: _accent,
      decoration: const InputDecoration(
        isDense: true,
        contentPadding: EdgeInsets.zero,
        border: InputBorder.none,
        hintText: 'Note title',
        hintStyle: TextStyle(color: _txt3),
      ),
      onSubmitted: (_) => _renameCommit(),
      onTapOutside: (_) => _renameCommit(),
    ),
  );
}

class _NoteRow extends StatefulWidget {
  const _NoteRow({
    required this.note,
    required this.selected,
    required this.onTap,
    required this.onRename,
    required this.onDelete,
  });
  final Note note;
  final bool selected;
  final VoidCallback onTap;
  final VoidCallback onRename;
  final VoidCallback onDelete;

  @override
  State<_NoteRow> createState() => _NoteRowState();
}

class _NoteRowState extends State<_NoteRow> {
  bool _hover = false;

  @override
  Widget build(BuildContext context) {
    final n = widget.note;
    final preview = n.body.replaceAll('\n', ' ').trim();
    return MouseRegion(
      onEnter: (_) => setState(() => _hover = true),
      onExit: (_) => setState(() => _hover = false),
      child: GestureDetector(
        onTap: widget.onTap,
        onDoubleTap: widget.onRename,
        child: Container(
          color: widget.selected
              ? _selBg
              : (_hover ? _hoverBg : Colors.transparent),
          padding: const EdgeInsets.fromLTRB(16, 11, 8, 11),
          child: Row(
            crossAxisAlignment: CrossAxisAlignment.start,
            children: [
              Container(
                width: 3,
                height: 34,
                margin: const EdgeInsets.only(right: 13, top: 1),
                decoration: BoxDecoration(
                  color: widget.selected ? _accent : Colors.transparent,
                  borderRadius: BorderRadius.circular(2),
                ),
              ),
              Expanded(
                child: Column(
                  crossAxisAlignment: CrossAxisAlignment.start,
                  children: [
                    Text(
                      n.title.isEmpty ? 'Untitled' : n.title,
                      maxLines: 1,
                      overflow: TextOverflow.ellipsis,
                      style: TextStyle(
                        fontSize: 14.5,
                        fontWeight: FontWeight.w600,
                        color: n.title.isEmpty ? _txt3 : _txt,
                      ),
                    ),
                    const SizedBox(height: 3),
                    Text(
                      preview.isEmpty ? 'No additional text' : preview,
                      maxLines: 1,
                      overflow: TextOverflow.ellipsis,
                      style: const TextStyle(
                        fontSize: 12.5,
                        height: 1.2,
                        color: _txt2,
                      ),
                    ),
                    const SizedBox(height: 5),
                    Text(
                      _ago(n.updated),
                      style: const TextStyle(fontSize: 10.5, color: _txt3),
                    ),
                  ],
                ),
              ),
              SizedBox(
                width: 30,
                child: (_hover || widget.selected)
                    ? _IconBtn(
                        Icons.delete_outline_rounded,
                        'Delete',
                        widget.onDelete,
                      )
                    : null,
              ),
            ],
          ),
        ),
      ),
    );
  }
}

// ── Window 2 — the editor (its own window/engine) ────────────────────────────
enum _Save { idle, editing, saved }

class EditorSurface extends StatefulWidget {
  const EditorSurface({super.key});
  @override
  State<EditorSurface> createState() => _EditorSurfaceState();
}

class _EditorSurfaceState extends State<EditorSurface> {
  final _title = TextEditingController();
  final _body = TextEditingController();
  final _titleFocus = FocusNode();
  Note? _note;
  Timer? _debounce;
  _Save _state = _Save.idle;

  @override
  void initState() {
    super.initState();
    AppBus.on(_selected, (p) => _load(p['id'] as String));
    AppBus.on(_focusTitle, (p) {
      if (_note?.id == p['id']) _titleFocus.requestFocus();
    });
    AppBus.on(_deleted, (p) {
      if (_note?.id == p['id']) setState(() => _note = null);
    });
  }

  @override
  void dispose() {
    _debounce?.cancel();
    _title.dispose();
    _body.dispose();
    _titleFocus.dispose();
    super.dispose();
  }

  void _load(String id) {
    if (id.isEmpty) {
      setState(() => _note = null);
      return;
    }
    final n = NotesStore.byId(id);
    _debounce?.cancel();
    setState(() {
      _note = n;
      _title.text = n.title;
      _body.text = n.body;
      _state = _Save.idle;
    });
    if (n.title.isEmpty) {
      WidgetsBinding.instance.addPostFrameCallback(
        (_) => _titleFocus.requestFocus(),
      );
    }
  }

  void _onChanged() {
    if (_note == null) return;
    setState(() => _state = _Save.editing);
    _debounce?.cancel();
    _debounce = Timer(const Duration(milliseconds: 500), _save);
  }

  void _save() {
    final n = _note;
    if (n == null) return;
    n
      ..title = _title.text
      ..body = _body.text;
    NotesStore.upsert(n); // persists + broadcasts notes.changed
    if (!mounted) return;
    setState(() => _state = _Save.saved);
    Future.delayed(const Duration(milliseconds: 1400), () {
      if (mounted && _state == _Save.saved) setState(() => _state = _Save.idle);
    });
  }

  void _saveNow() {
    _debounce?.cancel();
    _save();
  }

  @override
  Widget build(BuildContext context) {
    final n = _note;
    return Scaffold(
      backgroundColor: _bg,
      body: CallbackShortcuts(
        bindings: {
          const SingleActivator(LogicalKeyboardKey.keyS, control: true):
              _saveNow,
          const SingleActivator(LogicalKeyboardKey.keyW, control: true):
              MorphicSurface.close,
        },
        child: Column(
          children: [
            _Header(
              'Editor',
              trailing: [
                _StatusChip(_state),
                const SizedBox(width: 2),
                _IconBtn(
                  Icons.close_rounded,
                  'Close window (Ctrl+W)',
                  MorphicSurface.close,
                ),
              ],
            ),
            Expanded(child: n == null ? _empty() : _editor()),
          ],
        ),
      ),
    );
  }

  Widget _empty() => const Center(
    child: Column(
      mainAxisSize: MainAxisSize.min,
      children: [
        Icon(Icons.edit_note_rounded, size: 36, color: _txt3),
        SizedBox(height: 10),
        Text('Select a note to edit', style: TextStyle(color: _txt2)),
        SizedBox(height: 4),
        Text(
          'or press Ctrl+N in the list',
          style: TextStyle(fontSize: 12, color: _txt3),
        ),
      ],
    ),
  );

  Widget _editor() => Padding(
    padding: const EdgeInsets.fromLTRB(28, 22, 28, 18),
    child: Column(
      crossAxisAlignment: CrossAxisAlignment.stretch,
      children: [
        TextField(
          controller: _title,
          focusNode: _titleFocus,
          cursorColor: _accent,
          style: const TextStyle(
            fontSize: 22,
            fontWeight: FontWeight.w600,
            color: _txt,
            height: 1.2,
          ),
          decoration: const InputDecoration(
            isDense: true,
            contentPadding: EdgeInsets.zero,
            hintText: 'Title',
            hintStyle: TextStyle(color: _txt3, fontWeight: FontWeight.w600),
            border: InputBorder.none,
          ),
          textInputAction: TextInputAction.next,
          onChanged: (_) => _onChanged(),
        ),
        const SizedBox(height: 14),
        const Divider(height: 1, color: _hair),
        const SizedBox(height: 14),
        Expanded(
          child: TextField(
            controller: _body,
            maxLines: null,
            expands: true,
            cursorColor: _accent,
            textAlignVertical: TextAlignVertical.top,
            style: const TextStyle(fontSize: 15, height: 1.55, color: _txt),
            decoration: const InputDecoration(
              hintText: 'Start writing…',
              hintStyle: TextStyle(color: _txt3),
              border: InputBorder.none,
              isCollapsed: true,
            ),
            onChanged: (_) => _onChanged(),
          ),
        ),
        const SizedBox(height: 10),
        Row(
          children: [
            Text(
              '${_wordCount(_body.text)} words',
              style: const TextStyle(fontSize: 11.5, color: _txt3),
            ),
            const Spacer(),
            const Text(
              'Ctrl+S to save · saves automatically',
              style: TextStyle(fontSize: 11.5, color: _txt3),
            ),
          ],
        ),
      ],
    ),
  );
}

class _StatusChip extends StatelessWidget {
  const _StatusChip(this.state);
  final _Save state;
  @override
  Widget build(BuildContext context) {
    final (label, color) = switch (state) {
      _Save.editing => ('Editing…', _txt3),
      _Save.saved => ('Saved', _accent),
      _Save.idle => ('', _txt3),
    };
    return AnimatedOpacity(
      opacity: label.isEmpty ? 0 : 1,
      duration: const Duration(milliseconds: 200),
      child: Padding(
        padding: const EdgeInsets.only(right: 6),
        child: Row(
          mainAxisSize: MainAxisSize.min,
          children: [
            if (state == _Save.saved)
              const Padding(
                padding: EdgeInsets.only(right: 4),
                child: Icon(Icons.check_rounded, size: 14, color: _accent),
              ),
            Text(
              label.isEmpty ? '·' : label,
              style: TextStyle(fontSize: 12, color: color),
            ),
          ],
        ),
      ),
    );
  }
}

// ── Window 3 — the inspector (live metadata for the selected note) ───────────
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
    AppBus.on(_deleted, (p) {
      if (_note?.id == p['id']) setState(() => _note = null);
    });
  }

  void _show(String id) {
    if (id.isEmpty) {
      setState(() => _note = null);
      return;
    }
    setState(() => _note = NotesStore.byId(id));
  }

  @override
  Widget build(BuildContext context) {
    final n = _note;
    return Scaffold(
      backgroundColor: _bg,
      body: Column(
        children: [
          const _Header('Inspector'),
          Expanded(
            child: n == null
                ? const Center(
                    child: Text(
                      'No note selected',
                      style: TextStyle(color: _txt2),
                    ),
                  )
                : ListView(
                    padding: const EdgeInsets.fromLTRB(18, 18, 18, 18),
                    children: [
                      _bigStat('${_wordCount(n.body)}', 'words'),
                      const SizedBox(height: 18),
                      _row('Title', n.title.isEmpty ? 'Untitled' : n.title),
                      _row('Characters', '${n.body.length}'),
                      _row(
                        'Reading time',
                        '${(_wordCount(n.body) / 200).ceil().clamp(1, 999)} min',
                      ),
                      _row('Updated', _ago(n.updated)),
                      _row(
                        'Edited',
                        n.updated.toLocal().toString().split('.').first,
                      ),
                      _row('ID', n.id),
                    ],
                  ),
          ),
        ],
      ),
    );
  }

  Widget _bigStat(String value, String unit) => Row(
    crossAxisAlignment: CrossAxisAlignment.baseline,
    textBaseline: TextBaseline.alphabetic,
    children: [
      Text(
        value,
        style: const TextStyle(
          fontSize: 34,
          fontWeight: FontWeight.w700,
          color: _txt,
          height: 1,
          fontFeatures: [FontFeature.tabularFigures()],
        ),
      ),
      const SizedBox(width: 7),
      Padding(
        padding: const EdgeInsets.only(bottom: 3),
        child: Text(unit, style: const TextStyle(fontSize: 13, color: _txt3)),
      ),
    ],
  );

  Widget _row(String k, String v) => Padding(
    padding: const EdgeInsets.symmetric(vertical: 9),
    child: Column(
      crossAxisAlignment: CrossAxisAlignment.start,
      children: [
        Text(
          k.toUpperCase(),
          style: const TextStyle(
            fontSize: 10,
            letterSpacing: 0.9,
            fontWeight: FontWeight.w600,
            color: _txt3,
          ),
        ),
        const SizedBox(height: 3),
        Text(v, style: const TextStyle(fontSize: 13.5, color: _txt)),
      ],
    ),
  );
}
