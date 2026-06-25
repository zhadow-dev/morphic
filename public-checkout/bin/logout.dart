// `dart run morphic:logout` — revoke the local session and forget credentials.
import 'dart:io';

import 'package:morphic/src/licensing/api_client.dart';
import 'package:morphic/src/licensing/auth_store.dart';

Future<void> main(List<String> args) async {
  final store = AuthStore();
  final auth = store.read();
  if (auth == null) {
    stdout.writeln('Already logged out.');
    exit(0);
  }
  final api = MorphicApi();
  try {
    await api.logout(auth.refreshToken);
  } catch (_) {
    // Best-effort server revoke; always clear local state below.
  } finally {
    api.close();
  }
  store.clear();
  stdout.writeln('✓ Logged out (${auth.email}).');
  exit(0);
}
