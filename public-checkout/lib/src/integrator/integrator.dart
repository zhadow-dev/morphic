// Morphic INTEGRATOR (internal) — the runner-transformer used by the
// `dart run morphic:init` tooling. NOT part of the public SDK barrel.
/// Installer/integrator for the Morphic Windows runtime.
///
/// Layered in two tiers:
///  - `src/core/` — pure integration logic (manifest, install engine, project
///    inspection, file transforms). No printing, no process exit; everything
///    returns values or throws. Usable programmatically (tooling, CI, agents).
///  - `src/cli/` — thin command adapters over core, parameterized by
///    [CliEnvironment] so they are fully testable.
library;

export 'cli/cli_environment.dart';
export 'cli/doctor_command.dart';
export 'cli/init_command.dart';
export 'cli/remove_command.dart';
export 'cli/runner.dart';
export 'core/assets.dart';
export 'core/install_engine.dart';
export 'core/manifest.dart';
export 'core/project.dart';
export 'core/transforms.dart';
