import 'dart:io';
import 'package:path/path.dart';
import 'package:path_provider/path_provider.dart';
import 'package:sqflite_common_ffi/sqflite_ffi.dart';

class MorphicDb {
  static final MorphicDb instance = MorphicDb._init();
  static Database? _database;

  MorphicDb._init();

  Future<Database> get database async {
    if (_database != null) return _database!;
    _database = await _initDB('morphic_workspace.db');
    return _database!;
  }

  Future<Database> _initDB(String filePath) async {
    sqfliteFfiInit();
    databaseFactory = databaseFactoryFfi;

    final docPath = await getApplicationDocumentsDirectory();
    final path = join(docPath.path, 'Morphic', filePath);

    // Ensure directory exists
    final dir = Directory(dirname(path));
    if (!await dir.exists()) {
      await dir.create(recursive: true);
    }

    return await databaseFactory.openDatabase(
      path,
      options: OpenDatabaseOptions(
        version: 1,
        onCreate: _createDB,
      ),
    );
  }

  Future _createDB(Database db, int version) async {
    await db.execute('''
      CREATE TABLE workspaces (
          id TEXT PRIMARY KEY,
          label TEXT NOT NULL,
          archetype TEXT NOT NULL,
          active INTEGER DEFAULT 0
      )
    ''');

    await db.execute('''
      CREATE TABLE panes (
          id TEXT PRIMARY KEY,
          workspace_id TEXT,
          pane_type TEXT NOT NULL,
          split_ratio REAL NOT NULL,
          focus_index INTEGER NOT NULL,
          FOREIGN KEY(workspace_id) REFERENCES workspaces(id)
      )
    ''');

    await db.execute('''
      CREATE TABLE editor_state (
          pane_id TEXT PRIMARY KEY,
          file_path TEXT,
          cursor_position INTEGER DEFAULT 0,
          scroll_offset REAL DEFAULT 0.0,
          is_dirty INTEGER DEFAULT 0,
          recovery_draft_path TEXT,
          FOREIGN KEY(pane_id) REFERENCES panes(id)
      )
    ''');

    await db.execute('''
      CREATE TABLE session_state (
          session_id TEXT PRIMARY KEY,
          last_shutdown_clean INTEGER DEFAULT 1,
          recovery_state TEXT NOT NULL,
          restore_confidence TEXT NOT NULL,
          last_panic_epoch INTEGER DEFAULT 0,
          failed_restore_attempts INTEGER DEFAULT 0
      )
    ''');

    await db.execute('''
      CREATE TABLE workspace_events (
          id INTEGER PRIMARY KEY AUTOINCREMENT,
          timestamp INTEGER NOT NULL,
          event_type TEXT NOT NULL, 
          severity TEXT NOT NULL,
          recoverability TEXT NOT NULL,
          user_visible INTEGER DEFAULT 1,
          source_workspace_id TEXT,
          target_workspace_id TEXT,
          focused_pane_id TEXT,
          interruption_reason TEXT,
          details TEXT
      )
    ''');

    await db.execute('''
      CREATE TABLE session_fatigue_signals (
          session_id TEXT PRIMARY KEY,
          last_calculated INTEGER NOT NULL,
          workspace_churn_rate REAL DEFAULT 0.0,
          focus_instability_burst INTEGER DEFAULT 0,
          repeated_manual_override_count INTEGER DEFAULT 0,
          recovery_dependency_frequency INTEGER DEFAULT 0,
          detach_reversal_rate REAL DEFAULT 0.0,
          FOREIGN KEY(session_id) REFERENCES session_state(session_id)
      )
    ''');
  }

  Future<void> saveEditorState(String paneId, String? filePath, int cursorPosition, double scrollOffset, bool isDirty, String? draftPath) async {
    try {
      final db = await instance.database;
      await db.insert(
        'editor_state',
        {
          'pane_id': paneId,
          'file_path': filePath,
          'cursor_position': cursorPosition,
          'scroll_offset': scrollOffset,
          'is_dirty': isDirty ? 1 : 0,
          'recovery_draft_path': draftPath,
        },
        conflictAlgorithm: ConflictAlgorithm.replace,
      );
    } catch (e) {
      // Multi-engine SQLite contention: each engine has its own FFI connection
      // to the same database file. Silently drop the write — the next save
      // attempt will succeed when the lock is released.
    }
  }

  Future<Map<String, dynamic>?> getEditorState(String paneId) async {
    try {
      final db = await instance.database;
      final maps = await db.query(
        'editor_state',
        columns: ['file_path', 'cursor_position', 'scroll_offset', 'is_dirty', 'recovery_draft_path'],
        where: 'pane_id = ?',
        whereArgs: [paneId],
      );

      if (maps.isNotEmpty) {
        return maps.first;
      } else {
        return null;
      }
    } catch (e) {
      return null;
    }
  }

  Future<void> logEvent(String type, String severity, String recoverability, {String details = ''}) async {
    try {
      final db = await instance.database;
      await db.insert('workspace_events', {
        'timestamp': DateTime.now().millisecondsSinceEpoch,
        'event_type': type,
        'severity': severity,
        'recoverability': recoverability,
        'details': details,
      });
    } catch (e) {
      // Multi-engine contention — silently drop
    }
  }
}
