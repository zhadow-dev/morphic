#include "resource_counters.h"

#include <dxgi1_4.h>
#include <psapi.h>

#include <atomic>
#include <cstdio>

namespace morphic::res {
namespace {

std::atomic<long> g_wgc{0};
std::atomic<long> g_dcomp{0};

struct EnumCtx {
  DWORD pid;
  long count;
};

BOOL CALLBACK CountWindowsProc(HWND hwnd, LPARAM lp) {
  auto* ctx = reinterpret_cast<EnumCtx*>(lp);
  DWORD pid = 0;
  GetWindowThreadProcessId(hwnd, &pid);
  if (pid == ctx->pid) ++ctx->count;
  return TRUE;
}

}  // namespace

void IncWgcSession() { g_wgc.fetch_add(1); }
void DecWgcSession() { g_wgc.fetch_sub(1); }
long WgcSessions() { return g_wgc.load(); }
void IncDcompHost() { g_dcomp.fetch_add(1); }
void DecDcompHost() { g_dcomp.fetch_sub(1); }
long DcompHosts() { return g_dcomp.load(); }

Snapshot Capture(long topo_nodes, void* dxgi_adapter3) {
  Snapshot s;

  EnumCtx ctx{GetCurrentProcessId(), 0};
  EnumWindows(CountWindowsProc, reinterpret_cast<LPARAM>(&ctx));
  s.hwnds = ctx.count;

  DWORD handles = 0;
  if (GetProcessHandleCount(GetCurrentProcess(), &handles)) {
    s.handles = static_cast<long>(handles);
  }
  s.gdi = static_cast<long>(GetGuiResources(GetCurrentProcess(), GR_GDIOBJECTS));
  s.user =
      static_cast<long>(GetGuiResources(GetCurrentProcess(), GR_USEROBJECTS));

  PROCESS_MEMORY_COUNTERS_EX pmc{};
  pmc.cb = sizeof(pmc);
  if (GetProcessMemoryInfo(GetCurrentProcess(),
                           reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&pmc),
                           sizeof(pmc))) {
    s.private_kb = static_cast<long long>(pmc.PrivateUsage / 1024);
  }

  if (dxgi_adapter3 != nullptr) {
    auto* adapter = reinterpret_cast<IDXGIAdapter3*>(dxgi_adapter3);
    DXGI_QUERY_VIDEO_MEMORY_INFO vm{};
    if (SUCCEEDED(adapter->QueryVideoMemoryInfo(
            0, DXGI_MEMORY_SEGMENT_GROUP_LOCAL, &vm))) {
      s.vram_kb = static_cast<long long>(vm.CurrentUsage / 1024);
    }
  }

  s.wgc = g_wgc.load();
  s.dcomp = g_dcomp.load();
  s.topo = topo_nodes;
  return s;
}

std::string Format(const Snapshot& s) {
  char b[256];
  _snprintf_s(b, sizeof(b), _TRUNCATE,
              "hwnd=%ld handle=%ld gdi=%ld user=%ld priv=%lldKB vram=%lldKB "
              "wgc=%ld dcomp=%ld topo=%ld",
              s.hwnds, s.handles, s.gdi, s.user, s.private_kb, s.vram_kb, s.wgc,
              s.dcomp, s.topo);
  return b;
}

}  // namespace morphic::res
