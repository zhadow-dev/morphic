// `dart run morphic:whoami` — show the signed-in account and spatial access.
import 'dart:io';

import 'package:morphic/src/licensing/api_client.dart';
import 'package:morphic/src/licensing/auth_store.dart';

Future<void> main(List<String> args) async {
  final store = AuthStore();
  final auth = store.read();
  if (auth == null) {
    stdout.writeln('Not logged in.\n\n  Run:  dart run morphic:login');
    exit(0);
  }

  final api = MorphicApi();
  try {
    final accessToken = await api.accessToken(auth.refreshToken);
    final me = await api.me(accessToken);
    final spatial = (me['spatialAccess'] as bool?) ?? false;
    stdout.writeln('Logged in as:');
    stdout.writeln('  ${me['email']}');
    stdout.writeln('');
    stdout.writeln('Plan:');
    stdout.writeln('  ${me['plan']}');
    stdout.writeln('');
    stdout.writeln('Spatial Access:');
    stdout.writeln('  ${spatial ? 'Enabled' : 'Not included'}');
    exit(0);
  } on MorphicApiException catch (e) {
    stderr.writeln('Session problem: $e');
    stderr.writeln('\n  Run:  dart run morphic:login');
    exit(1);
  } finally {
    api.close();
  }
}
