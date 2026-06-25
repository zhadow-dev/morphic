import 'dart:convert';
import 'dart:io';

import 'package:morphic/src/integrator/integrator.dart';
import 'package:path/path.dart' as p;

const String vanillaMainCpp = '''
#include <flutter/dart_project.h>
#include <windows.h>

int APIENTRY wWinMain(HINSTANCE instance, HINSTANCE prev, wchar_t* command_line, int show_command) {
  return EXIT_SUCCESS;
}
''';

const String vanillaCmake = r'''
cmake_minimum_required(VERSION 3.14)
project(runner LANGUAGES CXX)

add_executable(${BINARY_NAME} WIN32
  "flutter_window.cpp"
  "main.cpp"
  "utils.cpp"
  "win32_window.cpp"
  "${FLUTTER_MANAGED_DIR}/generated_plugin_registrant.cc"
  "Runner.rc"
  "runner.exe.manifest"
)
apply_standard_settings(${BINARY_NAME})
target_link_libraries(${BINARY_NAME} PRIVATE flutter flutter_wrapper_app)
''';

const String vanillaPubspec = '''
name: scratch_app
description: A scratch Flutter project.
publish_to: none
version: 1.0.0+1

environment:
  sdk: ^3.7.0

dependencies:
  flutter:
    sdk: flutter
  cupertino_icons: ^1.0.8

dev_dependencies:
  flutter_test:
    sdk: flutter

flutter:
  uses-material-design: true
''';

const String fakeRuntimeCpp = '''
#include "morphic_runtime.h"
int morphic_boot() { return 7; }
''';

const String fakeRuntimeHeader = '''
#pragma once
int morphic_boot();
''';

const String fakeMainReplacement = '''
#include "morphic_runtime.h"
int APIENTRY wWinMain(...) { return morphic_boot(); }
''';

/// Builds a minimal but structurally faithful `runtime_assets/` tree (two
/// runtime sources in a nested dir + a main replacement) and its manifest.
/// Returns the assets root path.
String writeFakeAssets(Directory sandbox) {
  final assetsRoot = p.join(sandbox.path, 'runtime_assets');
  final runnerSrc = p.join(assetsRoot, 'windows', 'runner_morphic');

  File(p.join(runnerSrc, 'morphic_runtime.cpp'))
    ..createSync(recursive: true)
    ..writeAsStringSync(fakeRuntimeCpp);
  File(p.join(runnerSrc, 'multisurface', 'surface_graph.h'))
    ..createSync(recursive: true)
    ..writeAsStringSync(fakeRuntimeHeader);
  File(p.join(assetsRoot, 'windows', 'runner_main_morphic.cpp'))
    ..createSync(recursive: true)
    ..writeAsStringSync(fakeMainReplacement);

  final manifest = RuntimeManifest(
    runtimeVersion: '9.9.9-test',
    mainReplacement: ManifestEntry(
      source: 'windows/runner_main_morphic.cpp',
      target: 'windows/runner/main.cpp',
      sha256: hashBytes(utf8.encode(fakeMainReplacement)),
      kind: ManifestEntry.kindMainReplacement,
    ),
    files: [
      ManifestEntry(
        source: 'windows/runner_morphic/morphic_runtime.cpp',
        target: 'windows/runner/morphic_runtime.cpp',
        sha256: hashBytes(utf8.encode(fakeRuntimeCpp)),
        kind: ManifestEntry.kindRuntimeSource,
      ),
      ManifestEntry(
        source: 'windows/runner_morphic/multisurface/surface_graph.h',
        target: 'windows/runner/multisurface/surface_graph.h',
        sha256: hashBytes(utf8.encode(fakeRuntimeHeader)),
        kind: ManifestEntry.kindRuntimeSource,
      ),
    ],
  );
  File(
    p.join(assetsRoot, 'manifest.json'),
  ).writeAsStringSync(jsonEncode(manifest.toJson()));
  return assetsRoot;
}

/// Builds a vanilla Flutter-Windows-shaped project. Returns the project root.
String writeFakeProject(
  Directory sandbox, {
  String name = 'scratch_app',
  String? pubspec,
}) {
  final root = p.join(sandbox.path, name);
  File(p.join(root, 'pubspec.yaml'))
    ..createSync(recursive: true)
    ..writeAsStringSync(pubspec ?? vanillaPubspec);
  File(p.join(root, 'windows', 'runner', 'main.cpp'))
    ..createSync(recursive: true)
    ..writeAsStringSync(vanillaMainCpp);
  File(
    p.join(root, 'windows', 'runner', 'CMakeLists.txt'),
  ).writeAsStringSync(vanillaCmake);
  return root;
}

class SandboxCli {
  SandboxCli({required String projectRoot, required String assetsRoot}) {
    env = CliEnvironment(
      workingDirectory: projectRoot,
      variables: const {},
      out: out,
      err: err,
      assetsRoot: assetsRoot,
      runProcess: (exe, args) async => ProcessResult(0, 0, '$exe stub\n', ''),
    );
  }

  final StringBuffer out = StringBuffer();
  final StringBuffer err = StringBuffer();
  late final CliEnvironment env;

  Future<int> run(List<String> args) async =>
      await buildMorphicRunner(environment: env).run(args) ?? 0;
}

String readProjectFile(String root, String relPath) =>
    File(p.join(root, relPath)).readAsStringSync();

bool projectFileExists(String root, String relPath) =>
    File(p.join(root, relPath)).existsSync();
