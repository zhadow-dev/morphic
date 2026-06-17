import 'dart:async';
import 'dart:io';
import 'package:flutter/material.dart';
import 'package:path_provider/path_provider.dart';
import 'package:path/path.dart' as p;
import 'morphic_db.dart';

class EditorPane extends StatefulWidget {
  final int engineId;
  const EditorPane({super.key, required this.engineId});

  @override
  State<EditorPane> createState() => _EditorPaneState();
}

class _EditorPaneState extends State<EditorPane> {
  final TextEditingController _controller = TextEditingController();
  final ScrollController _scrollController = ScrollController();
  final FocusNode _focusNode = FocusNode();

  String? _filePath;
  String? _draftPath;
  bool _isDirty = false;
  bool _showAutosaveCheck = false;
  String? _saveError;
  Timer? _draftTimer;
  Timer? _autosaveCheckTimer;
  Timer? _idleTimer;

  bool _initialized = false;
  String _paneId = '';

  @override
  void initState() {
    super.initState();
    _paneId = 'pane_${widget.engineId}';
    _initEditor();

    // Autosave draft every 10 seconds
    _draftTimer = Timer.periodic(const Duration(seconds: 10), (timer) {
      if (_isDirty) {
        _saveDraft();
      }
    });

    _focusNode.addListener(_onFocusChange);
    _controller.addListener(_onTextChanged);
    _scrollController.addListener(_onScrollChanged);
  }

  Future<void> _initEditor() async {
    final docDir = await getApplicationDocumentsDirectory();
    final morphicDir = Directory(p.join(docDir.path, 'Morphic'));
    if (!await morphicDir.exists()) {
      await morphicDir.create(recursive: true);
    }
    
    // Default file path for this pane
    _filePath = p.join(morphicDir.path, 'workspace_${widget.engineId}.txt');
    _draftPath = p.join(morphicDir.path, 'workspace_${widget.engineId}.morphic_draft.tmp');

    // Attempt to restore from DB
    final state = await MorphicDb.instance.getEditorState(_paneId);
    if (state != null) {
      final savedFilePath = state['file_path'] as String?;
      if (savedFilePath != null && savedFilePath.isNotEmpty) {
        _filePath = savedFilePath;
      }
      
      final savedDraftPath = state['recovery_draft_path'] as String?;
      if (savedDraftPath != null && savedDraftPath.isNotEmpty) {
        _draftPath = savedDraftPath;
      }
      
      final isDirty = state['is_dirty'] == 1;
      
      // Load content
      bool loadedFromDraft = false;
      if (isDirty && _draftPath != null && await File(_draftPath!).exists()) {
        final draftContent = await File(_draftPath!).readAsString();
        _controller.text = draftContent;
        _isDirty = true;
        loadedFromDraft = true;
        MorphicDb.instance.logEvent('recovery_fallback_used', 'Medium', 'Assisted', details: 'Loaded from draft: $_draftPath');
      } else if (_filePath != null && await File(_filePath!).exists()) {
        _controller.text = await File(_filePath!).readAsString();
        _isDirty = false;
      } else {
        _controller.text = '// Morphic Editor Buffer\n';
        _isDirty = true;
      }

      // Restore cursor & scroll
      if (state['cursor_position'] != null) {
        final pos = state['cursor_position'] as int;
        if (pos >= 0 && pos <= _controller.text.length) {
          _controller.selection = TextSelection.collapsed(offset: pos);
        }
      }
      
      if (state['scroll_offset'] != null) {
        final offset = state['scroll_offset'] as double;
        // Deferred stabilization restore: wait for text metrics and pane bounds to settle
        Future.delayed(const Duration(milliseconds: 200), () {
          if (mounted && _scrollController.hasClients) {
            // Check if offset is within bounds after layout
            final maxScroll = _scrollController.position.maxScrollExtent;
            _scrollController.jumpTo(offset > maxScroll ? maxScroll : offset);
          }
        });
      }
      
      if (loadedFromDraft) {
        _showSaveConfirmation();
      }
    } else {
      // New buffer
      _controller.text = '// Morphic Editor Buffer\n';
      _isDirty = true;
    }

    setState(() {
      _initialized = true;
    });
  }

  void _onTextChanged() {
    if (!_isDirty) {
      setState(() {
        _isDirty = true;
      });
      _persistStateToDb();
    }
    // Idle stability threshold (Tier 2 Canonical Persistence)
    _idleTimer?.cancel();
    _idleTimer = Timer(const Duration(seconds: 30), () {
      if (_isDirty) _saveToMainFile();
    });
  }

  void _onFocusChange() {
    // Focus loss NO LONGER saves to canonical file directly.
    // It is not semantically trustworthy (workspace shifts, alt-tabs).
    // We only persist the cursor/scroll state to SQLite.
    _persistStateToDb();
  }
  
  void _onScrollChanged() {
    // We don't want to spam DB on every pixel scroll, but we'll persist state 
    // maybe throttling would be better, but we'll do it on focus lost or periodic draft save
  }

  Future<void> _saveDraft() async {
    if (_draftPath == null) return;
    try {
      await File(_draftPath!).writeAsString(_controller.text);
      _persistStateToDb();
      if (mounted && _saveError != null) setState(() => _saveError = null);
      // Draft saves remain silent, no checkmarks.
    } catch (e) {
      if (mounted) setState(() => _saveError = 'Draft Save Pending');
      debugPrint('Failed to save draft: $e');
    }
  }

  Future<void> _saveToMainFile() async {
    if (_filePath == null) return;
    try {
      await File(_filePath!).writeAsString(_controller.text);
      
      // Cleanup draft
      if (_draftPath != null && await File(_draftPath!).exists()) {
        await File(_draftPath!).delete();
      }
      
      if (mounted) {
        setState(() {
          _isDirty = false;
          _saveError = null;
        });
      }
      _persistStateToDb();
      _showSaveConfirmation();
    } catch (e) {
      if (mounted) setState(() => _saveError = 'Recovery Delayed');
      debugPrint('Failed to save file: $e');
    }
  }

  Future<void> _persistStateToDb() async {
    await MorphicDb.instance.saveEditorState(
      _paneId,
      _filePath,
      _controller.selection.baseOffset,
      _scrollController.hasClients ? _scrollController.offset : 0.0,
      _isDirty,
      _draftPath,
    );
  }

  void _showSaveConfirmation() {
    setState(() {
      _showAutosaveCheck = true;
    });
    _autosaveCheckTimer?.cancel();
    _autosaveCheckTimer = Timer(const Duration(seconds: 2), () {
      if (mounted) {
        setState(() {
          _showAutosaveCheck = false;
        });
      }
    });
  }

  @override
  void dispose() {
    _persistStateToDb();
    _draftTimer?.cancel();
    _autosaveCheckTimer?.cancel();
    _idleTimer?.cancel();
    _controller.dispose();
    _scrollController.dispose();
    _focusNode.dispose();
    super.dispose();
  }

  @override
  Widget build(BuildContext context) {
    if (!_initialized) {
      return const Scaffold(
        backgroundColor: Color(0xFF0a0a0f),
        body: Center(child: CircularProgressIndicator(color: Color(0xFF3fb950))),
      );
    }

    final filename = _filePath != null ? p.basename(_filePath!) : 'untitled.txt';

    return Scaffold(
      backgroundColor: const Color(0xFF0d1117), // Deep IDE background
      body: Column(
        children: [
          // Header
          Container(
            height: 36,
            color: const Color(0xFF161b22),
            padding: const EdgeInsets.symmetric(horizontal: 16),
            child: Row(
              children: [
                Icon(Icons.description, size: 14, color: const Color(0xFF8b949e)),
                const SizedBox(width: 8),
                Text(
                  filename,
                  style: const TextStyle(
                    color: Color(0xFFc9d1d9),
                    fontSize: 12,
                    fontFamily: 'Consolas',
                  ),
                ),
                if (_isDirty)
                  Container(
                    margin: const EdgeInsets.only(left: 6),
                    width: 6,
                    height: 6,
                    decoration: const BoxDecoration(
                      color: Color(0xFFe3b341), // Amber dot
                      shape: BoxShape.circle,
                    ),
                  ),
                const Spacer(),
                if (_saveError != null)
                  Container(
                    margin: const EdgeInsets.only(right: 12),
                    padding: const EdgeInsets.symmetric(horizontal: 6, vertical: 2),
                    decoration: BoxDecoration(
                      color: const Color(0xFFda3633).withOpacity(0.15),
                      borderRadius: BorderRadius.circular(4),
                    ),
                    child: Text(_saveError!, style: const TextStyle(color: Color(0xFFff7b72), fontSize: 9, fontWeight: FontWeight.w600)),
                  ),
                AnimatedOpacity(
                  opacity: _showAutosaveCheck ? 1.0 : 0.0,
                  duration: const Duration(milliseconds: 300),
                  curve: Curves.easeOut,
                  child: const Row(
                    children: [
                      Icon(Icons.check, size: 14, color: Color(0xFF3fb950)),
                      SizedBox(width: 4),
                      Text('Saved', style: TextStyle(color: Color(0xFF3fb950), fontSize: 10)),
                    ],
                  ),
                ),
              ],
            ),
          ),
          // Editor
          Expanded(
            child: Padding(
              padding: const EdgeInsets.all(12.0),
              child: TextField(
                controller: _controller,
                scrollController: _scrollController,
                focusNode: _focusNode,
                maxLines: null,
                expands: true,
                style: const TextStyle(
                  color: Color(0xFFc9d1d9),
                  fontSize: 14,
                  fontFamily: 'Consolas',
                  height: 1.5,
                ),
                decoration: const InputDecoration(
                  border: InputBorder.none,
                  isDense: true,
                  contentPadding: EdgeInsets.zero,
                ),
                cursorColor: const Color(0xFF58a6ff),
              ),
            ),
          ),
        ],
      ),
    );
  }
}
