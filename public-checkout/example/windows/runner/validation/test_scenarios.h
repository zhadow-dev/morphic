#ifndef RUNNER_VALIDATION_TEST_SCENARIOS_H_
#define RUNNER_VALIDATION_TEST_SCENARIOS_H_

#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

class InteractionSimulator;
class SurfaceManager;

// PHASE 7D — TestScenarios
//
// Named scripted scenarios for the InteractionSimulator. Registry-based so
// they're addressable from the validation harness ("RunScenario('rapid_drag')")
// and can be enumerated for RunAllScenarios().
//
// Scenarios in 7D (starter set, deliberately small):
//   "rapid_drag"           — straight-line drag at high tick cadence
//   "circular_drag"        — drag along a circle
//   "direction_reversal"   — drag back-and-forth (stresses interp 2B)
//   "resize_corner_spam"   — corner resize against min-size clamp
//   "rapid_begin_end"      — Begin/End spam (stresses lifecycle 1B/1C)
//   "long_drag"            — multi-second drag for starvation observability
//
// More scenarios slot in by registering with `Register()`; harness picks them
// up automatically.
class TestScenarios {
 public:
  using Factory = std::function<bool(InteractionSimulator&, SurfaceManager&)>;

  TestScenarios(InteractionSimulator* sim, SurfaceManager* manager);

  TestScenarios(const TestScenarios&) = delete;
  TestScenarios& operator=(const TestScenarios&) = delete;

  void Register(const std::string& name, Factory factory);
  bool Run(const std::string& name);
  std::vector<std::string> Names() const;

 private:
  void RegisterBuiltins();

  InteractionSimulator* sim_;
  SurfaceManager* manager_;
  std::unordered_map<std::string, Factory> registry_;
};

#endif  // RUNNER_VALIDATION_TEST_SCENARIOS_H_
