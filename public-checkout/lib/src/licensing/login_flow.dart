// Browser loopback login (internal, pure Dart).
//
// Flow (no key pasting):
//   1. CLI starts a localhost server on an ephemeral port.
//   2. CLI opens the browser to ${siteUrl}/cli-login?port=NNNN&state=XXXX
//   3. User signs in with Google on the web page.
//   4. The page POSTs the Morphic refresh token back to http://127.0.0.1:NNNN/callback
//   5. CLI validates `state`, returns the session.
import 'dart:async';
import 'dart:convert';
import 'dart:io';
import 'dart:math';

import 'api_client.dart';

class LoopbackLogin {
  LoopbackLogin(this.api);
  final MorphicApi api;

  String _randomState() {
    final r = Random.secure();
    return List.generate(24, (_) => r.nextInt(256))
        .map((b) => b.toRadixString(16).padLeft(2, '0'))
        .join();
  }

  /// Runs the loopback flow and returns the session. Throws on timeout/cancel.
  Future<ExchangeResult> run({
    Duration timeout = const Duration(minutes: 3),
    void Function(String url)? onUrl,
  }) async {
    final state = _randomState();
    final server = await HttpServer.bind(InternetAddress.loopbackIPv4, 0);
    final port = server.port;
    final completer = Completer<ExchangeResult>();

    final loginUrl = '${api.siteUrl}/cli-login?port=$port&state=$state';
    onUrl?.call(loginUrl);
    _openBrowser(loginUrl);

    final sub = server.listen((HttpRequest req) async {
      // CORS for the web page's fetch.
      req.response.headers
        ..add('Access-Control-Allow-Origin', '*')
        ..add('Access-Control-Allow-Methods', 'POST, GET, OPTIONS')
        ..add('Access-Control-Allow-Headers', 'Content-Type');

      if (req.method == 'OPTIONS') {
        req.response.statusCode = 204;
        await req.response.close();
        return;
      }
      if (!req.uri.path.startsWith('/callback')) {
        req.response.statusCode = 404;
        await req.response.close();
        return;
      }

      try {
        Map<String, Object?> data;
        if (req.method == 'POST') {
          data = jsonDecode(await utf8.decodeStream(req)) as Map<String, Object?>;
        } else {
          data = req.uri.queryParameters;
        }
        if ((data['state'] ?? '') != state) {
          throw const FormatException('state mismatch');
        }
        final result = ExchangeResult(
          uid: (data['uid'] ?? '') as String,
          email: (data['email'] ?? '') as String,
          plan: (data['plan'] ?? 'developer') as String,
          refreshToken: (data['refreshToken'] ?? '') as String,
        );
        if (result.refreshToken.isEmpty) {
          throw const FormatException('missing refresh token');
        }
        req.response
          ..statusCode = 200
          ..headers.contentType = ContentType.html
          ..write(_successHtml);
        await req.response.close();
        if (!completer.isCompleted) completer.complete(result);
      } catch (e) {
        req.response
          ..statusCode = 400
          ..write('Login failed: $e');
        await req.response.close();
        if (!completer.isCompleted) completer.completeError(StateError('$e'));
      }
    });

    try {
      return await completer.future.timeout(timeout);
    } finally {
      await sub.cancel();
      await server.close(force: true);
    }
  }

  void _openBrowser(String url) {
    try {
      if (Platform.isWindows) {
        // PowerShell Start-Process with a single-quoted URL reliably preserves
        // '&' in the query string. (cmd's `start` splits on unquoted '&', and
        // explorer.exe opens a file window instead of the browser.)
        Process.run('powershell', [
          '-NoProfile',
          '-Command',
          "Start-Process '$url'",
        ]);
      } else if (Platform.isMacOS) {
        Process.run('open', [url]);
      } else {
        Process.run('xdg-open', [url]);
      }
    } catch (_) {
      // Non-fatal: the URL is printed for manual opening.
    }
  }

  static const _successHtml = '''
<!doctype html><html><head><meta charset="utf-8"><title>Morphic</title>
<style>body{font-family:system-ui;background:#07080c;color:#eceef3;display:grid;
place-items:center;height:100vh;margin:0}</style></head>
<body><div style="text-align:center">
<h2>You're signed in to Morphic.</h2>
<p style="color:#989fb2">You can close this window and return to your terminal.</p>
</div></body></html>''';
}
