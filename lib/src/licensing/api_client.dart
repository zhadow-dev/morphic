// Morphic licensing API client (internal, pure Dart).
// Talks the same HTTP contract the backend serves in both mock and firebase
// modes. Base URL is env-overridable so local development points at the mock.
import 'dart:convert';
import 'dart:io';

import 'package:http/http.dart' as http;

/// Endpoints + identity returned by the backend.
class MorphicApi {
  MorphicApi({String? apiUrl, String? siteUrl, http.Client? client})
    : apiUrl =
          apiUrl ??
          Platform.environment['MORPHIC_API_URL'] ??
          'https://api.morphic.dev',
      siteUrl =
          siteUrl ??
          Platform.environment['MORPHIC_SITE_URL'] ??
          'https://morphic.dev',
      _http = client ?? http.Client();

  final String apiUrl;
  final String siteUrl;
  final http.Client _http;

  Uri _u(String path) => Uri.parse('$apiUrl$path');

  /// mock/dev sign-in (no browser). firebase mode rejects this.
  Future<ExchangeResult> exchangeDev(String email, {String? displayName}) async {
    final res = await _http.post(
      _u('/cli/exchange'),
      headers: _json,
      body: jsonEncode({'email': email, 'displayName': displayName}),
    );
    _ensure(res, 'sign in');
    return ExchangeResult.fromJson(_decode(res));
  }

  /// Browser loopback handoff: exchange a Firebase ID token for a session.
  Future<ExchangeResult> exchangeIdToken(String idToken) async {
    final res = await _http.post(
      _u('/cli/exchange'),
      headers: _json,
      body: jsonEncode({'idToken': idToken}),
    );
    _ensure(res, 'sign in');
    return ExchangeResult.fromJson(_decode(res));
  }

  /// Trade a refresh token for a fresh short-lived access token.
  Future<String> accessToken(String refreshToken) async {
    final res = await _http.post(
      _u('/cli/token'),
      headers: _json,
      body: jsonEncode({'refreshToken': refreshToken}),
    );
    _ensure(res, 'refresh session');
    return _decode(res)['accessToken'] as String;
  }

  Future<void> logout(String refreshToken) async {
    await _http.post(
      _u('/cli/logout'),
      headers: _json,
      body: jsonEncode({'refreshToken': refreshToken}),
    );
  }

  Future<Map<String, Object?>> me(String accessToken) async {
    final res = await _http.get(_u('/me'), headers: _bearer(accessToken));
    _ensure(res, 'load account');
    return _decode(res);
  }

  Future<bool> spatialAccess(String accessToken) async {
    final res = await _http.post(
      _u('/spatial/access'),
      headers: _bearer(accessToken),
    );
    _ensure(res, 'check spatial access');
    return (_decode(res)['authorized'] as bool?) ?? false;
  }

  Future<SignedDownload> spatialDownload(String accessToken) async {
    final res = await _http.post(
      _u('/spatial/download'),
      headers: _bearer(accessToken),
    );
    _ensure(res, 'request spatial runtime');
    return SignedDownload.fromJson(_decode(res));
  }

  /// Streams the signed-URL artifact to [dest], returning the bytes count.
  Future<List<int>> download(String url) async {
    final res = await _http.get(Uri.parse(url));
    if (res.statusCode != 200) {
      throw MorphicApiException('download failed (${res.statusCode})');
    }
    return res.bodyBytes;
  }

  void close() => _http.close();

  static const _json = {'Content-Type': 'application/json'};
  Map<String, String> _bearer(String t) => {'Authorization': 'Bearer $t'};

  Map<String, Object?> _decode(http.Response r) =>
      jsonDecode(r.body) as Map<String, Object?>;

  void _ensure(http.Response r, String action) {
    if (r.statusCode >= 200 && r.statusCode < 300) return;
    String detail = r.body;
    try {
      detail = (jsonDecode(r.body)['error'] ?? r.body).toString();
    } catch (_) {}
    throw MorphicApiException('Could not $action: $detail (${r.statusCode})');
  }
}

class ExchangeResult {
  ExchangeResult({
    required this.uid,
    required this.email,
    required this.plan,
    required this.refreshToken,
  });
  final String uid;
  final String email;
  final String plan;
  final String refreshToken;

  static ExchangeResult fromJson(Map<String, Object?> j) => ExchangeResult(
    uid: j['uid'] as String,
    email: j['email'] as String,
    plan: (j['plan'] ?? 'developer') as String,
    refreshToken: j['refreshToken'] as String,
  );
}

class SignedDownload {
  SignedDownload({required this.url, required this.sha256, required this.version});
  final String url;
  final String sha256;
  final String version;

  static SignedDownload fromJson(Map<String, Object?> j) => SignedDownload(
    url: j['url'] as String,
    sha256: j['sha256'] as String,
    version: j['version'] as String,
  );
}

class MorphicApiException implements Exception {
  MorphicApiException(this.message);
  final String message;
  @override
  String toString() => message;
}
