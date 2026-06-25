// `dart run morphic:login` — authenticate the Morphic CLI.
//
// Default: opens the browser, you sign in with Google, the CLI receives a
// session automatically (no key pasting). For local/dev/CI against the mock
// backend, pass `--email you@example.com` to sign in without a browser.
import 'dart:io';

import 'package:morphic/src/licensing/api_client.dart';
import 'package:morphic/src/licensing/auth_store.dart';
import 'package:morphic/src/licensing/login_flow.dart';

Future<void> main(List<String> args) async {
  final email = _flag(args, '--email');
  final api = MorphicApi();
  final store = AuthStore();

  try {
    final ExchangeResult result;
    if (email != null) {
      stdout.writeln('Signing in as $email (dev) …');
      result = await api.exchangeDev(email);
    } else {
      stdout.writeln('Opening your browser to sign in …');
      result = await LoopbackLogin(api).run(
        onUrl: (url) => stdout.writeln('\nIf it doesn\'t open, visit:\n  $url\n'),
      );
    }

    store.write(
      MorphicAuth(
        uid: result.uid,
        email: result.email,
        plan: result.plan,
        refreshToken: result.refreshToken,
      ),
    );

    stdout.writeln('\n✓ Logged in as ${result.email}');
    stdout.writeln('  Plan: ${result.plan}');
    stdout.writeln('  Next: dart run morphic:init --spatial --apply');
    exit(0);
  } on Object catch (e) {
    stderr.writeln('\nLogin failed: $e');
    exit(1);
  } finally {
    api.close();
  }
}

String? _flag(List<String> args, String name) {
  final i = args.indexOf(name);
  if (i >= 0 && i + 1 < args.length) return args[i + 1];
  for (final a in args) {
    if (a.startsWith('$name=')) return a.substring(name.length + 1);
  }
  return null;
}
