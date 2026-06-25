import 'dart:convert';
import 'dart:io';

import 'package:path/path.dart' as p;

import 'manifest.dart';

/// The reversal/integrity engine. Every project mutation goes through
/// [backup] / [materialize] here so it can be undone exactly.
///
/// State lives in `<project>/.morphic/install.json` (the install record):
/// installed runtime version, every materialized file with its hash, and the
/// backups taken — so [reverse] restores the exact pre-install bytes.
class InstallEngine {
  InstallEngine(this.projectRoot, {this.assetsRoot});

  final String projectRoot;

  /// The package's `runtime_assets/` directory. Only required by
  /// [materialize]; reversal and drift detection work without it.
  final String? assetsRoot;

  String get morphicDir => p.join(projectRoot, '.morphic');
  String get recordPath => p.join(morphicDir, 'install.json');
  String get backupDir => p.join(morphicDir, 'backup');

  bool get isInstalled => File(recordPath).existsSync();

  InstallRecord? readRecord() {
    final file = File(recordPath);
    if (!file.existsSync()) return null;
    return InstallRecord.fromJson(
      jsonDecode(file.readAsStringSync()) as Map<String, dynamic>,
    );
  }

  /// Backs up an existing project file to `.morphic/backup/<relpath>` so it
  /// can be restored byte-identically. If the file does not exist, records a
  /// "created" marker so [reverse] deletes it instead of restoring.
  BackupEntry backup(String projectRelPath) {
    final src = File(p.join(projectRoot, projectRelPath));
    if (!src.existsSync()) {
      return BackupEntry(
        path: projectRelPath,
        existedBefore: false,
        sha256: null,
      );
    }
    final dst = File(p.join(backupDir, projectRelPath));
    dst.parent.createSync(recursive: true);
    src.copySync(dst.path);
    return BackupEntry(
      path: projectRelPath,
      existedBefore: true,
      sha256: hashFile(src),
    );
  }

  /// Copies one manifest entry's canonical source into the project, verifying
  /// the source hash first so a corrupted asset is never materialized.
  MaterializedFile materialize(ManifestEntry entry) {
    final root = assetsRoot;
    if (root == null) {
      throw StateError('InstallEngine has no assetsRoot; cannot materialize.');
    }
    final src = File(p.join(root, entry.source));
    if (!src.existsSync()) {
      throw StateError('asset missing: ${entry.source}');
    }
    final actual = hashFile(src);
    if (actual != entry.sha256) {
      throw StateError(
        'asset hash mismatch for ${entry.source} '
        '(manifest ${entry.sha256}, actual $actual) — regenerate manifest.',
      );
    }
    final dst = File(p.join(projectRoot, entry.target));
    dst.parent.createSync(recursive: true);
    src.copySync(dst.path);
    return MaterializedFile(target: entry.target, sha256: entry.sha256);
  }

  void writeRecord(InstallRecord record) {
    Directory(morphicDir).createSync(recursive: true);
    File(recordPath).writeAsStringSync(
      '${const JsonEncoder.withIndent('  ').convert(record.toJson())}\n',
    );
  }

  /// Reverses a previous install: deletes materialized runtime files, restores
  /// backups (or deletes files that did not exist before), then removes
  /// `.morphic/`. Returns a human-readable list of actions taken.
  List<String> reverse() {
    final record = readRecord();
    if (record == null) return ['nothing to reverse (no install record).'];
    final actions = <String>[];

    for (final target in record.materializedTargets) {
      final file = File(p.join(projectRoot, target));
      if (file.existsSync()) {
        file.deleteSync();
        actions.add('deleted $target');
      }
    }
    _pruneEmptyDirs(p.join(projectRoot, 'windows', 'runner'));

    for (final b in record.backups) {
      final target = File(p.join(projectRoot, b.path));
      if (b.existedBefore) {
        final bak = File(p.join(backupDir, b.path));
        if (bak.existsSync()) {
          target.parent.createSync(recursive: true);
          bak.copySync(target.path);
          actions.add('restored ${b.path}');
        } else {
          actions.add('WARNING: backup missing for ${b.path} (left as-is)');
        }
      } else if (target.existsSync()) {
        target.deleteSync();
        actions.add('removed created ${b.path}');
      }
    }

    final dir = Directory(morphicDir);
    if (dir.existsSync()) {
      dir.deleteSync(recursive: true);
      actions.add('removed .morphic/');
    }
    return actions;
  }

  /// Drift detection: which materialized files were edited since install
  /// (current hash != recorded hash). Used to warn before clobbering hand
  /// edits.
  List<String> detectDrift() {
    final record = readRecord();
    if (record == null) return const [];
    final drifted = <String>[];
    for (final m in record.materialized) {
      final file = File(p.join(projectRoot, m.target));
      if (file.existsSync() && hashFile(file) != m.sha256) {
        drifted.add(m.target);
      }
    }
    return drifted;
  }

  void _pruneEmptyDirs(String root) {
    final dir = Directory(root);
    if (!dir.existsSync()) return;
    for (final sub in dir.listSync().whereType<Directory>()) {
      _pruneEmptyDirs(sub.path);
    }
    if (dir.listSync().isEmpty && p.basename(root) != 'runner') {
      try {
        dir.deleteSync();
      } on FileSystemException {
        // Best-effort: a locked or concurrently-written dir is left in place.
      }
    }
  }
}

/// Persisted install state at `<project>/.morphic/install.json`.
class InstallRecord {
  InstallRecord({
    required this.runtimeVersion,
    required this.installedAt,
    required this.materialized,
    required this.backups,
  });

  final String runtimeVersion;
  final String installedAt;

  /// Files copied into the project, with the hash they were written at.
  final List<MaterializedFile> materialized;

  /// Files backed up before mutation.
  final List<BackupEntry> backups;

  List<String> get materializedTargets =>
      materialized.map((m) => m.target).toList();

  Map<String, Object> toJson() => {
    'runtimeVersion': runtimeVersion,
    'installedAt': installedAt,
    'materialized': materialized.map((m) => m.toJson()).toList(),
    'backups': backups.map((b) => b.toJson()).toList(),
  };

  factory InstallRecord.fromJson(Map<String, dynamic> json) => InstallRecord(
    runtimeVersion: json['runtimeVersion'] as String,
    installedAt: json['installedAt'] as String,
    materialized:
        (json['materialized'] as List)
            .map((e) => MaterializedFile.fromJson(e as Map<String, dynamic>))
            .toList(),
    backups:
        (json['backups'] as List)
            .map((e) => BackupEntry.fromJson(e as Map<String, dynamic>))
            .toList(),
  );
}

class MaterializedFile {
  MaterializedFile({required this.target, required this.sha256});

  final String target;
  final String sha256;

  Map<String, Object> toJson() => {'target': target, 'sha256': sha256};

  factory MaterializedFile.fromJson(Map<String, dynamic> json) =>
      MaterializedFile(
        target: json['target'] as String,
        sha256: json['sha256'] as String,
      );
}

class BackupEntry {
  BackupEntry({
    required this.path,
    required this.existedBefore,
    required this.sha256,
  });

  final String path;
  final bool existedBefore;
  final String? sha256;

  Map<String, Object> toJson() => {
    'path': path,
    'existedBefore': existedBefore,
    if (sha256 != null) 'sha256': sha256!,
  };

  factory BackupEntry.fromJson(Map<String, dynamic> json) => BackupEntry(
    path: json['path'] as String,
    existedBefore: json['existedBefore'] as bool,
    sha256: json['sha256'] as String?,
  );
}
