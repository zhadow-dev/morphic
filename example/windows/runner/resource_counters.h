#ifndef RUNNER_RESOURCE_COUNTERS_H_
#define RUNNER_RESOURCE_COUNTERS_H_

#include <windows.h>

#include <string>

// PHASE 11 — RESOURCE COUNTERS. The measurement foundation for leak/drift
// detection: process-level resource samples + live counters for the heavy
// compositor objects (WGC capture sessions, DComp composition hosts). The churn
// harness samples these continuously and detects upward SLOPE over long runs —
// the delayed/async leaks that baseline snapshots miss.
namespace morphic::res {

// Live counters, bumped at create/destroy of the heavy objects. One place to
// leak-check capture sessions / composition hosts.
void IncWgcSession();
void DecWgcSession();
long WgcSessions();
void IncDcompHost();
void DecDcompHost();
long DcompHosts();

struct Snapshot {
  long hwnds = 0;            // top-level windows owned by this process
  long handles = 0;         // kernel handle count
  long gdi = 0;             // GDI objects
  long user = 0;            // USER objects
  long long private_kb = 0; // private commit (PrivateUsage)
  long long vram_kb = 0;    // local VRAM current usage (0 if unavailable)
  long wgc = 0;             // live WGC capture sessions
  long dcomp = 0;           // live DComp composition hosts
  long topo = 0;            // topology node count (caller-supplied)
};

// Capture a snapshot now. [topo_nodes] is the caller's live graph node count;
// [dxgi_adapter3] is an IDXGIAdapter3* (as void*, may be null) for VRAM.
Snapshot Capture(long topo_nodes, void* dxgi_adapter3);

// Compact one-line form for the trace.
std::string Format(const Snapshot& s);

}  // namespace morphic::res

#endif  // RUNNER_RESOURCE_COUNTERS_H_
