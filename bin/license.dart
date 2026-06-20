// `dart run morphic:license` — show your Morphic license / Developer Preview status.
import 'dart:io';

import 'package:morphic/src/licensing/api_client.dart';
import 'package:morphic/src/licensing/auth_store.dart';

Future<void> main(List<String> args) async {
  final store = AuthStore();
  final auth = store.read();
  if (auth == null) {
    stdout.writeln('No license on this machine.\n\n  Sign in:  dart run morphic:login');
    exit(0);
  }

  final api = MorphicApi();
  try {
    final accessToken = await api.accessToken(auth.refreshToken);
    final me = await api.me(accessToken);
    final spatial = (me['spatialAccess'] as bool?) ?? false;
    final tier = _tierLabel((me['plan'] ?? 'developer').toString());

    stdout.writeln('Morphic License\n');
    stdout.writeln('  Account:   ${me['email']}');
    stdout.writeln('  Tier:      $tier');
    stdout.writeln('  Projects:  Unlimited');
    stdout.writeln('  Expires:   Never');
    stdout.writeln('  Spatial:   ${spatial ? 'Enabled' : 'Not included'}');
    stdout.writeln('  Status:    Activated');
    if (spatial) {
      stdout.writeln('\nInstall the spatial runtime:  dart run morphic:init --spatial');
    }
    exit(0);
  } on MorphicApiException catch (e) {
    stderr.writeln('Could not read license: $e');
    stderr.writeln('\n  Sign in again:  dart run morphic:login');
    exit(1);
  } finally {
    api.close();
  }
}

String _tierLabel(String plan) {
  switch (plan) {
    case 'developer':
      return 'Developer Preview';
    case 'free':
      return 'Free';
    default:
      return plan.isEmpty ? plan : plan[0].toUpperCase() + plan.substring(1);
  }
}
