cmake_minimum_required(VERSION 3.14)
project(runner LANGUAGES CXX)

# Define the application target. To change its name, change BINARY_NAME in the
# top-level CMakeLists.txt, not the value here, or `flutter run` will no longer
# work.
#
# Any new source files that you add to the application should be added here.
add_executable(${BINARY_NAME} WIN32
  "morphic_runtime.cpp"
  "surface_manager.cpp"
  "surface_model.cpp"
  "surface_shell.cpp"
  "interaction_router.cpp"
  "fault_injector.cpp"
  "frame_clock.cpp"
  "runtime_events.cpp"
  "surface_lifecycle_relay.cpp"  # M2.1D — surface identity + lifecycle projection (generic)
  "validation/runtime_telemetry.cpp"
  "validation/forensic_logger.cpp"
  "validation/tick_monitor.cpp"
  "validation/projection_auditor.cpp"
  "validation/integrity_auditor.cpp"
  "validation/activation_coherency_auditor.cpp"  # PHASE 10.4B (kind-agnostic, log-only)
  "validation/ontology_auditor.cpp"  # M2.1C — ontology enforcement layer (constitutional)
  "validation/interaction_simulator.cpp"
  "validation/test_scenarios.cpp"
  "validation/session_report_writer.cpp"
  "validation/validation_harness.cpp"
  "validation/soak_runner.cpp"
  "multisurface/surface_relationship.cpp"
  "multisurface/surface_graph.cpp"
  "multisurface/orchestration_debugger.cpp"
  "multisurface/interaction_session.cpp"
  # PHASE 8C — extraction semantics. extraction_model.h is header-only (pure
  # math); the epoch has a translation unit.
  "multisurface/interaction_epoch.cpp"
  # PHASE 8B.7 — epoch runtime. The other temporal/*.h are header-only
  # (frame_epoch, epoch_budget, epoch_reentrancy_guard, critical_path_trace,
  # epoch_telemetry_adapter); only the scheduler has a translation unit.
  "temporal/frame_scheduler.cpp"
  # PHASE 8E — presentation coherence (settle-only layer).
  "presentation/presentation_coordinator.cpp"
  # PHASE 9 — compositor runtime (projection seam; HWND backend today). The
  # surface_visual.h + projection_backend.h are header-only.
  "compositor/compositor_runtime.cpp"
  # PHASE 10.1 — surface ecology (product policy layer ABOVE the runtime).
  # surface_kind.h / surface_descriptor.h / workspace_model.h are header-only.
  "surface_policy/surface_registry.cpp"
  "surface_policy/surface_policy.cpp"
  "surface_policy/surface_ecology.cpp"
  # PHASE 10.3 — native surface behavior policy (SurfaceKind → Win32 shell flags).
  # native_surface_behavior.h is header-only.
  "surface_policy/native_surface_policy.cpp"
  "surface_policy/activation_policy.cpp"
  # PHASE 11A/11B — native appearance (DWM shadow/corners/dark mode). The headers
  # surface_appearance.h / composition_types.h / surface_appearance_policy.h are
  # header-only; only these two have translation units.
  "surface_policy/surface_appearance_policy.cpp"
  "surface_policy/native_appearance_projection.cpp"
  "compositor_ng/projection_reconciler.cpp" # MORPHIC NG Phase 1
  "compositor_ng/d2d_effect_guids.cpp"   # MORPHIC NG Phase 3 (GUID defs)
  "compositor_ng/composition_effects.cpp" # MORPHIC NG Phase 3
  "compositor_ng/spatial_shapes.cpp"      # MORPHIC NG Phase 3
  "compositor_ng/gpu_context.cpp"        # MORPHIC NG Stage 3 — shared GPU stack
  "compositor_ng/spatial_visual_host.cpp" # MORPHIC NG Stage 4+6 — shaped visual + input routing
  "compositor_ng/spatial_compositor.cpp"  # MORPHIC NG Stage 5 — hybrid backend orchestrator
  "compositor_ng/backdrop_probe.cpp"      # PROBE P-0 — scene-capture feasibility (PASSED; kept as record)
  "compositor_ng/visibility_probe.cpp"    # DIAGNOSTIC — visibility-state matrix (DWM→WGC starvation)
  "compositor_ng/backdrop_scene.cpp"      # NG — scene-authoritative backdrop (the ownership pivot)
  "scene/scene_graph.cpp"   # MORPHIC NG Stage 1 — retained scene graph (pure data)
  "scene/scene_mirror.cpp"  # MORPHIC NG Stage 1 — graph as observer of ecology truth
  "workspace/workspace_manager.cpp"
  # PHASE 12B/12D — workspace composition (plane graph + shared-drag movement projector).
  # composition_plane.h is header-only.
  "workspace/workspace_composition_graph.cpp"
  "workspace/composition_projector.cpp"
  # SPATIAL MIGRATION Stage 1 — plane activation model (semantic activation only).
  "workspace/plane_activation_model.cpp"
  # SPATIAL MIGRATION Stage 2A — content-level plane visual projection.
  "workspace/plane_visual_projector.cpp"
  # M2.3G — relationship gravity (companion reposition on root maximize; product layer).
  "workspace/relationship_gravity_controller.cpp"
  # M2.8.3 — scene zoom (camera-move composition reprojection; product layer).
  "workspace/scene_zoom_controller.cpp"
  "forensic_trace.cpp"
  "window_topology.cpp"  # WINDOW TOPOLOGY — role model + shell/composition auditor
  "resource_counters.cpp"  # PHASE 11 — resource sampler + live WGC/DComp counters
  "experiment_config.cpp"  # PHASE 11 — env-driven churn/teardown sweep config
  "churn_harness.cpp"  # PHASE 11 — churn/leak stress harness
  "crash_handler.cpp"  # FIELD SAFETY — local crash capture (minidump + crash log)
  "main.cpp"
  "utils.cpp"
  "${FLUTTER_MANAGED_DIR}/generated_plugin_registrant.cc"
  "Runner.rc"
  "runner.exe.manifest"
)

# Apply the standard set of build settings. This can be removed for applications
# that need different build settings.
apply_standard_settings(${BINARY_NAME})
# PHASE 10.2 — upgrade to C++20 to match the morphic plugin (which uses
# designated initializers). Required because the runner now includes
# morphic_plugin.h for the extension handler mechanism.
set_target_properties(${BINARY_NAME} PROPERTIES CXX_STANDARD 20)
set_target_properties(${BINARY_NAME} PROPERTIES CXX_STANDARD_REQUIRED ON)

# Add preprocessor definitions for the build version.
target_compile_definitions(${BINARY_NAME} PRIVATE "FLUTTER_VERSION=\"${FLUTTER_VERSION}\"")
target_compile_definitions(${BINARY_NAME} PRIVATE "FLUTTER_VERSION_MAJOR=${FLUTTER_VERSION_MAJOR}")
target_compile_definitions(${BINARY_NAME} PRIVATE "FLUTTER_VERSION_MINOR=${FLUTTER_VERSION_MINOR}")
target_compile_definitions(${BINARY_NAME} PRIVATE "FLUTTER_VERSION_PATCH=${FLUTTER_VERSION_PATCH}")
target_compile_definitions(${BINARY_NAME} PRIVATE "FLUTTER_VERSION_BUILD=${FLUTTER_VERSION_BUILD}")

# Disable Windows macros that collide with C++ standard library functions.
target_compile_definitions(${BINARY_NAME} PRIVATE "NOMINMAX")

# MORPHIC NG — the example is the full SPATIAL build (the compositor dev app).
# Defining MORPHIC_SPATIAL compiles the compositor_ng wiring in morphic_runtime.
# A consumer project that vendors only the native tier omits this define (and the
# compositor_ng sources), so the runtime compiles spatial-free. (Premium asset
# gate, Phase 1 — see doc/PREMIUM.md.)
target_compile_definitions(${BINARY_NAME} PRIVATE "MORPHIC_SPATIAL")

# Add dependency libraries and include directories. Add any application-specific
# dependencies here.
target_link_libraries(${BINARY_NAME} PRIVATE flutter flutter_wrapper_app)
target_link_libraries(${BINARY_NAME} PRIVATE "dwmapi.lib")
# FIELD SAFETY — MiniDumpWriteDump for the crash handler.
target_link_libraries(${BINARY_NAME} PRIVATE "dbghelp.lib")
# PHASE 11 — GetProcessMemoryInfo for the resource sampler.
target_link_libraries(${BINARY_NAME} PRIVATE "psapi.lib")
# PHASE 7E rev4 — timeBeginPeriod/timeEndPeriod live in winmm.
target_link_libraries(${BINARY_NAME} PRIVATE "winmm.lib")
# MORPHIC NG — spatial compositor GPU stack (D3D11/D2D/DComp + WinRT capture).
target_link_libraries(${BINARY_NAME} PRIVATE "d3d11.lib")
target_link_libraries(${BINARY_NAME} PRIVATE "d2d1.lib")
target_link_libraries(${BINARY_NAME} PRIVATE "dcomp.lib")
target_link_libraries(${BINARY_NAME} PRIVATE "windowsapp.lib")
target_include_directories(${BINARY_NAME} PRIVATE "${CMAKE_SOURCE_DIR}")
# PHASE 7D — let `#include "validation/foo.h"` resolve from anywhere in the
# tree, not just from files that happen to live in runner/. Without this, a
# .cpp inside runner/validation/ doing `#include "validation/bar.h"` would
# look for runner/validation/validation/bar.h. Adding runner/ as a root makes
# the prefix absolute relative to the runner module.
target_include_directories(${BINARY_NAME} PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
# PHASE 10.2 — let the runner include morphic_plugin.h for the extension handler.
# The plugin's source root is accessible via Flutter's plugin symlink directory.
target_include_directories(${BINARY_NAME} PRIVATE
  "${CMAKE_SOURCE_DIR}/flutter/ephemeral/.plugin_symlinks/morphic/windows")

# Run the Flutter tool portions of the build. This must not be removed.
add_dependencies(${BINARY_NAME} flutter_assemble)
