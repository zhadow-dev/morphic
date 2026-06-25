#ifndef RUNNER_QUALIFICATION_PROBE_H_
#define RUNNER_QUALIFICATION_PROBE_H_

// PHASE 11G-PROBE — DWM material qualification probe (THROWAWAY / NON-COMMITTAL).
//
// THE QUESTION (the only thing this answers): is Morphic's shell fundamentally
// popup-based or compositor-qualified? Specifically — does a FULLY qualified
// overlapped window (WS_OVERLAPPEDWINDOW incl. WS_CAPTION + WS_THICKFRAME) unlock
// REAL, DISTINCT SYSTEMBACKDROP materials (Mica/Acrylic/Tabbed) in OUR exact shell,
// AND can 7A runtime-owned interaction survive the native frame?
//
// History: probes 2P (1px NCCALCSIZE), 2Q (WS_THICKFRAME + NCCALCSIZE→0), and 2R
// (WS_THICKFRAME + surviving frame) ALL stayed fallback-grey. The ONE untested
// variable is WS_CAPTION/overlapped — the documented "main window" qualifier. This
// probe tests it head-on, no half-measures.
//
// When kQualificationProbe == true, the shell + appearance projection switch to:
//   - WS_OVERLAPPEDWINDOW (real caption + sizing frame) at CreateWindowEx
//   - WM_NCCALCSIZE lets the real non-client frame SURVIVE (DefWindowProc)
//   - real DWMWA_SYSTEMBACKDROP_TYPE (Mica=MAINWINDOW / Acrylic=TRANSIENTWINDOW /
//     Tabbed=TABBEDWINDOW); the legacy ACCENT path is DISABLED so it can't
//     contaminate the result
//   - WM_SYSCOMMAND eats SC_MOVE / SC_SIZE so the overlapped frame can NEVER drag
//     Morphic into the native modal loops 7A exists to kill
//
// PROBE PHILOSOPHY: ugly-but-truthful. The native caption WILL be visible. Do NOT
// beautify — that is downstream of the architectural decision this probe settles.
//
// ACCEPTANCE (binary, either outcome is a win):
//   PASS  → distinct materials appear + interaction/z/activation survive + no modal
//           loops → overlapped-qualified shell becomes a real (not mandatory) candidate.
//   FAIL  → still grey, OR interaction destabilizes → popup-frameless is the PERMANENT
//           architecture and this question is closed forever.
//
// REVERT: delete this header + its three `#include` lines + the three
// `if constexpr (morphic::probe::kQualificationProbe)` blocks (surface_shell.cpp x2,
// native_appearance_projection.cpp x1). Default OFF — never ships as production.
// CONCLUDED (Stage 0): the probe qualified for SYSTEMBACKDROP but Mica/Acrylic/Tabbed collapsed
// into indistinguishable output AND the overlapped swap degraded 7A interaction. Verdict:
// OS-material differentiation is insufficient to justify the risk. Qualified-shell work is
// PARKED (see SPATIAL_RUNTIME_MIGRATION_PLAN.md Stage 5). Flag stays false; experiment retained.
namespace morphic::probe {
inline constexpr bool kQualificationProbe = false;
}  // namespace morphic::probe

#endif  // RUNNER_QUALIFICATION_PROBE_H_
