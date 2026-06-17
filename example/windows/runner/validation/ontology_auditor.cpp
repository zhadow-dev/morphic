#include "validation/ontology_auditor.h"

#include <cstdlib>  // abort (kFatal path)
#include <string>

#include "forensic_trace.h"
#include "runtime_events.h"
#include "spatial_config.h"  // morphic::config::kEnableGeometricDocking
#include "surface_shell.h"

OntologyAuditor::OntologyAuditor(EventBus* bus) : bus_(bus) {
  // State the constitutional baseline at boot — the laws and HOW each is enforced.
  forensic::Log("ONTOLOGY",
                "enforcement layer up — firewall=STRUCTURAL  §4b(input)=STRUCTURAL  "
                "§4c(geometry≠topology)=RUNTIME(this)");
  if constexpr (morphic::config::kEnableGeometricDocking) {
    forensic::Log("ONTOLOGY WARN",
                  "§4c SUSPENDED by config — kEnableGeometricDocking=true: geometry MAY mutate "
                  "topology (docking experiment). Group/dock events below are EXPECTED.");
  } else {
    forensic::Log("ONTOLOGY",
                  "§4c enforced — geometric docking OFF; no group/dock may form (geometry never "
                  "mutates topology).");
  }
  if (bus_) {
    bus_token_ = bus_->Subscribe(
        [this](RuntimeEvent e, SurfaceShell* s) { OnEvent(e, s); });
  }
}

OntologyAuditor::~OntologyAuditor() {
  if (bus_ && bus_token_ != 0) {
    bus_->Unsubscribe(bus_token_);
    bus_token_ = 0;
  }
}

void OntologyAuditor::OnEvent(RuntimeEvent event, SurfaceShell* surface) {
  // §4c tripwire. The ONLY geometry→topology path is docking → SurfaceGraph::Group, which emits
  // these events. With docking disabled they must NEVER fire; one firing means the law was
  // circumvented (some path bypassed SurfacePolicy::IsGroupable).
  if (event != RuntimeEvent::SurfaceGrouped &&
      event != RuntimeEvent::SurfaceDocked) {
    return;
  }
  if constexpr (morphic::config::kEnableGeometricDocking) {
    return;  // law intentionally suspended (experiment) — group/dock is expected, not a violation
  }

  const std::string id = surface ? surface->id() : std::string("?");
  forensic::Log(
      "ONTOLOGY VIOLATION",
      "law=§4c source=geometry attempted_write=topology-mutation event=" +
          std::string(ToString(event)) + " surface=" + id +
          " — a group/dock formed while geometric docking is DISABLED: geometry mutated "
          "topology. blocked_by=§4c. This should be IMPOSSIBLE — find the path that bypassed "
          "SurfacePolicy::IsGroupable and close it.");

  if constexpr (kFatal) {
    std::abort();  // make the crossing impossible to ignore (second-app integration)
  }
}
