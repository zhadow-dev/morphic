// Morphic CLI auth storage (internal). Pure Dart — no Flutter imports — so it
// runs under `dart run`. Stores the long-lived REFRESH token (not the access
// token), per the licensing design: access tokens are short-lived and fetched
// on demand from the refresh token.
import 'dart:convert';
import 'dart:io';

/// Persisted CLI credentials.
class MorphicAuth {
  const MorphicAuth({
    required this.uid,
    required this.email,
    required this.plan,
    required this.refreshToken,
  });

  final String uid;
  final String email;
  final String plan;
  final String refreshToken;

  Map<String, Object?> toJson() => {
    'uid': uid,
    'email': email,
    'plan': plan,
    'refreshToken': refreshToken,
  };

  static MorphicAuth fromJson(Map<String, Object?> j) => MorphicAuth(
    uid: (j['uid'] ?? '') as String,
    email: (j['email'] ?? '') as String,
    plan: (j['plan'] ?? 'developer') as String,
    refreshToken: (j['refreshToken'] ?? '') as String,
  );
}

/// Reads/writes `auth.json` in the OS-appropriate config directory.
class AuthStore {
  AuthStore({Map<String, String>? env}) : _env = env ?? Platform.environment;

  final Map<String, String> _env;

  /// Platform config directory for Morphic:
  ///  - Windows: %APPDATA%\Morphic
  ///  - macOS:   ~/Library/Application Support/Morphic
  ///  - Linux:   $XDG_CONFIG_HOME/morphic or ~/.config/morphic
  Directory get configDir {
    if (Platform.isWindows) {
      final appData = _env['APPDATA'];
      final base = (appData != null && appData.isNotEmpty)
          ? appData
          : '${_env['USERPROFILE']}\\AppData\\Roaming';
      return Directory('$base\\Morphic');
    }
    final home = _env['HOME'] ?? '';
    if (Platform.isMacOS) {
      return Directory('$home/Library/Application Support/Morphic');
    }
    final xdg = _env['XDG_CONFIG_HOME'];
    final base = (xdg != null && xdg.isNotEmpty) ? xdg : '$home/.config';
    return Directory('$base/morphic');
  }

  File get authFile => File('${configDir.path}${Platform.pathSeparator}auth.json');

  MorphicAuth? read() {
    final f = authFile;
    if (!f.existsSync()) return null;
    try {
      final j = jsonDecode(f.readAsStringSync()) as Map<String, Object?>;
      final auth = MorphicAuth.fromJson(j);
      return auth.refreshToken.isEmpty ? null : auth;
    } catch (_) {
      return null;
    }
  }

  void write(MorphicAuth auth) {
    configDir.createSync(recursive: true);
    authFile.writeAsStringSync(
      const JsonEncoder.withIndent('  ').convert(auth.toJson()),
    );
  }

  void clear() {
    if (authFile.existsSync()) authFile.deleteSync();
  }
}
