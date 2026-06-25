# The Flutter tooling requires that developers have a version of Visual Studio
# installed that includes CMake 3.14 or later. You should not increase this
# version, as doing so will cause the plugin to fail to compile for some
# customers of the plugin.
cmake_minimum_required(VERSION 3.14)

# Project-level configuration.
set(PROJECT_NAME "morphic")
project(${PROJECT_NAME} LANGUAGES CXX)

# Explicitly opt in to modern CMake behaviors to avoid warnings with recent
# versions of CMake.
cmake_policy(VERSION 3.14...3.25)

# === C++20 ===
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# This value is used when generating builds using this plugin, so it must
# not be changed
set(PLUGIN_NAME "morphic_plugin")

# Any new source files that you add to the plugin should be added here.
list(APPEND PLUGIN_SOURCES
  "morphic_plugin.cpp"
  "morphic_plugin.h"
  # Core
  "src/core/types.h"
  "src/core/visual_node.h"
  "src/core/composition_surface.h"
  "src/core/window_host.h"
  "src/core/window_host.cpp"
  "src/core/scene_graph.h"
  "src/core/thread_affinity.h"
  # Composition
  "src/composition/composition_clock.h"
  "src/composition/compositor.h"
  "src/composition/compositor.cpp"
  # Elevation
  "src/elevation/elevation_manager.h"
  # Display
  "src/display/display_manager.h"
  # Interaction
  "src/interaction/drag_controller.h"
  "src/interaction/resize_controller.h"
  "src/interaction/hit_test_policy.h"
  "src/interaction/message_router.h"
  "src/interaction/capture_manager.h"
  "src/interaction/focus_types.h"
  "src/interaction/focus_graph.h"
  "src/interaction/focus_graph.cpp"
  "src/interaction/input_router.h"
  "src/interaction/input_router.cpp"
  "src/interaction/interaction_transaction.cpp"
  # Debug
  "src/debug/debug_overlay.h"
  "src/debug/debug_overlay.cpp"
  "src/debug/metrics_collector.h"
  "src/debug/render_skew_tracker.h"
  "src/debug/input_photon_tracker.h"
  "src/debug/frame_pacer.h"
  "src/debug/zombie_auditor.h"
  "src/debug/gpu_profiler.h"
  "src/debug/thread_activity_auditor.h"
  "src/debug/zombie_decay_tracker.h"
  "src/debug/frame_cadence_monitor.h"
  # Testing
  "src/testing/invariant_checker.h"
  "src/testing/ordering_validator.h"
  "src/testing/replay_system.h"
  "src/testing/stress_harness.h"
  "src/testing/drift_detector.h"
  "src/testing/failure_injector.h"
  "src/testing/virtual_display.h"
  "src/testing/soak_test.h"
  "src/testing/temporal_validator.h"
  "src/testing/crash_recovery_test.h"
  "src/testing/render_resize_storm.h"
  "src/testing/scaling_validator.h"
  "src/testing/scaling_validator.cpp"
  # Rendering (Phase 2A)
  "src/rendering/renderer_surface.h"
  "src/rendering/flutter_renderer.h"
  "src/rendering/renderer_manager.h"
  # Phase 4: Runtime foundations
  "src/core/runtime_phase.h"
  "src/core/runtime_events.h"
  "src/core/runtime_capabilities.h"
  "src/core/runtime_subsystem.h"
  "src/core/runtime_transaction.h"
  "src/core/surface_presence.h"
  "src/core/mutation_authority.h"
  "src/core/runtime_scene_state.h"
  "src/core/runtime_scene_state.cpp"
  "src/core/runtime_commit_scheduler.h"
  "src/core/runtime_commit_scheduler.cpp"
  "src/core/runtime_mutation_intent.h"
  "src/core/runtime_surface_realizer.h"
  "src/core/win32_surface_realizer.h"
  "src/core/win32_surface_realizer.cpp"
  # Phase 4 Step 1: Authority extraction
  "src/core/topology_manager.h"
  "src/core/topology_manager.cpp"
  # Phase 4 Step 1: Authority extraction
  "src/core/surface_registry.h"
  "src/core/surface_registry.cpp"
  "src/interaction/activation_manager.h"
  "src/interaction/activation_manager.cpp"
  "src/composition/workspace_controller.h"
  "src/composition/workspace_controller.cpp"
  "src/rendering/renderer_health.h"
  "src/interaction/interaction_runtime.h"
  # Phase 6: Operational Runtime Hardening
  "src/core/runtime_pressure.h"
  "src/core/degraded_runtime_policy.h"
  "src/debug/runtime_diagnostics.h"
  "src/testing/fault_domain_validator.h"
  "src/testing/fault_domain_validator.cpp"
  "src/testing/scheduler_validator.h"
  "src/testing/scheduler_validator.cpp"
  "src/testing/runtime_drift_validator.h"
  "src/testing/runtime_drift_validator.cpp"
  "src/testing/operational_battlefield_validator.h"
  "src/testing/operational_battlefield_validator.cpp"
  # Phase 7: Human Workspace Semantics
  "src/composition/workspace_intent.h"
  "src/experimental/attention_economics.h"
  "src/composition/session_continuity.h"
  "src/composition/workflow_graph.h"
  "src/composition/adaptive_orchestration.h"
  "src/testing/session_battlefield_validator.h"
  "src/testing/session_battlefield_validator.cpp"
  # Phase 8: Consumer Reality Validation
  "include/morphic/morphic_api.h"
  "src/composition/session_persistence.h"
  "src/examples/workflow_patterns.h"
  # Phase 9: External Reality & Integration Hardening
  "src/core/kernel_trace.h"
  "src/core/kernel_trace.cpp"
  "src/testing/persistence_warfare_validator.h"
  "src/testing/persistence_warfare_validator.cpp"
  "src/testing/api_misuse_validator.h"
  "src/testing/api_misuse_validator.cpp"
  "src/testing/performance_envelope.h"
  "src/testing/performance_envelope.cpp"
  "src/testing/runtime_warfare_validator.h"
  "src/testing/runtime_warfare_validator.cpp"
  # Morphic Studio: Runtime Facade + Channels
  "src/api/runtime_bootstrap.h"
  "src/api/workspace_runtime.h"
  "src/api/morphic_runtime_impl.h"
  "src/api/morphic_runtime_impl.cpp"
  "src/api/validation_runtime.h"
  "src/api/validation_runtime.cpp"
  "src/api/workspace_channel.h"
  "src/api/workspace_channel.cpp"
  "src/api/session_channel.h"
  "src/api/session_channel.cpp"
  "src/api/diagnostics_channel.h"
  "src/api/diagnostics_channel.cpp"
  # Phase 8B.6: Temporal Integrity + Runtime Stress Infrastructure
  "src/core/interaction_phase.h"
  "src/testing/temporal_auditor.h"
  "src/testing/performance_budget.h"
  "src/testing/soak_harness.h"
  "src/debug/tick_coherence_probe.h"
  # Phase 10.2: Extension handler for product-layer method channel dispatch
  "morphic_extension.cpp"
)

# Define the plugin library target. Its name must not be changed (see comment
# on PLUGIN_NAME above).
add_library(${PLUGIN_NAME} SHARED
  "include/morphic/morphic_plugin_c_api.h"
  "morphic_plugin_c_api.cpp"
  ${PLUGIN_SOURCES}
)

# Apply a standard set of build settings that are configured in the
# application-level CMakeLists.txt. This can be removed for plugins that want
# full control over build settings.
apply_standard_settings(${PLUGIN_NAME})

# Symbols are hidden by default to reduce the chance of accidental conflicts
# between plugins. This should not be removed; any symbols that should be
# exported should be explicitly exported with the FLUTTER_PLUGIN_EXPORT macro.
set_target_properties(${PLUGIN_NAME} PROPERTIES
  CXX_VISIBILITY_PRESET hidden)
target_compile_definitions(${PLUGIN_NAME} PRIVATE FLUTTER_PLUGIN_IMPL)

# Source include directories and library dependencies.
target_include_directories(${PLUGIN_NAME} INTERFACE
  "${CMAKE_CURRENT_SOURCE_DIR}/include")

# Include our source directories for internal headers
target_include_directories(${PLUGIN_NAME} PRIVATE
  "${CMAKE_CURRENT_SOURCE_DIR}")

# Link Flutter and Windows system libraries
target_link_libraries(${PLUGIN_NAME} PRIVATE
  flutter
  flutter_wrapper_plugin
  dwmapi
  shcore
)

# List of absolute paths to libraries that should be bundled with the plugin.
# This list could contain prebuilt libraries, or libraries created by an
# external build triggered from this build file.
set(morphic_bundled_libraries
  ""
  PARENT_SCOPE
)

# === Tests ===
# These unit tests can be run from a terminal after building the example, or
# from Visual Studio after opening the generated solution file.

# Only enable test builds when building the example (which sets this variable)
# so that plugin clients aren't building the tests.
if (${include_${PROJECT_NAME}_tests})
set(TEST_RUNNER "${PROJECT_NAME}_test")
enable_testing()

# Add the Google Test dependency.
include(FetchContent)
FetchContent_Declare(
  googletest
  URL https://github.com/google/googletest/archive/release-1.11.0.zip
)
# Prevent overriding the parent project's compiler/linker settings
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
# Disable install commands for gtest so it doesn't end up in the bundle.
set(INSTALL_GTEST OFF CACHE BOOL "Disable installation of googletest" FORCE)
FetchContent_MakeAvailable(googletest)

# The plugin's C API is not very useful for unit testing, so build the sources
# directly into the test binary rather than using the DLL.
add_executable(${TEST_RUNNER}
  test/morphic_plugin_test.cpp
  ${PLUGIN_SOURCES}
)
apply_standard_settings(${TEST_RUNNER})
target_include_directories(${TEST_RUNNER} PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(${TEST_RUNNER} PRIVATE flutter_wrapper_plugin dwmapi shcore)
target_link_libraries(${TEST_RUNNER} PRIVATE gtest_main gmock)
# flutter_wrapper_plugin has link dependencies on the Flutter DLL.
add_custom_command(TARGET ${TEST_RUNNER} POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
  "${FLUTTER_LIBRARY}" $<TARGET_FILE_DIR:${TEST_RUNNER}>
)

# Enable automatic test discovery.
include(GoogleTest)
gtest_discover_tests(${TEST_RUNNER})
endif()
