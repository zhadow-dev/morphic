#ifndef RUNNER_VALIDATION_ONTOLOGY_AUDITOR_H_
#define RUNNER_VALIDATION_ONTOLOGY_AUDITOR_H_

#include "runtime_events.h"

class EventBus;
class SurfaceShell;

// M2.1C — OntologyAuditor: the ONTOLOGY ENFORCEMENT LAYER.
//
// The runtime now defends its own constitution. The semantic laws (AUTHORITY_MAP.md;
// SPATIAL_RUNTIME_MIGRATION.md §4b/§4c; the firewall) were enforced only by discipline + grep.
// Here, illegal semantic crossings are made OBSERVABLE — so a SECOND APP (or future code) cannot
// violate them silently. These are CONSTITUTIONAL checks, not "bad state" checks.
//
// HONESTY ABOUT ENFORCEMENT — each law is enforced at its STRONGEST available layer, and this
// auditor only adds runtime detection where structure can't already guarantee it:
//   • FIREWALL (runtime never learns SurfaceKind) — STRUCTURAL / COMPILE-TIME. The runtime simply
//     does not include the SurfaceKind header. You cannot violate what you cannot name. No
//     runtime check is stronger; this auditor only NAMES the law.
//   • §4b (planes / visual never own input or activation) — STRUCTURAL. PlaneActivationModel and
//     PlaneVisualProjector hold NO router / capture handles — they literally cannot write
//     activation or route input. Stronger than any runtime assert; named here, not checked.
//   • §4c (geometry never mutates topology) — RUNTIME-DETECTABLE, so THIS auditor enforces it.
//     The only geometry→topology path is geometric docking → SurfaceGraph::Group (which emits
//     SurfaceGrouped / SurfaceDocked). With docking disabled (kEnableGeometricDocking=false) NO
//     group/dock may form; if one does, the law was bypassed → [ONTOLOGY VIOLATION].
//
// LOG-ONLY by default (auditors OBSERVE, never govern — a loud scream, not a behavior change),
// matching ActivationCoherencyAuditor / ProjectionAuditor. `kFatal` promotes a §4c violation to a
// hard abort (for making second-app integration FAIL HARD on a crossing).
class OntologyAuditor {
 public:
  // false (default) = observe-and-scream. true = a §4c violation aborts — flip when you want the
  // second app's first illegal crossing to be impossible to ignore.
  static constexpr bool kFatal = false;

  explicit OntologyAuditor(EventBus* bus);
  ~OntologyAuditor();

  OntologyAuditor(const OntologyAuditor&) = delete;
  OntologyAuditor& operator=(const OntologyAuditor&) = delete;

 private:
  void OnEvent(RuntimeEvent event, SurfaceShell* surface);

  EventBus* bus_;
  int bus_token_ = 0;
};

#endif  // RUNNER_VALIDATION_ONTOLOGY_AUDITOR_H_
