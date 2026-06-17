#include "forensic_trace.h"

#include <dwmapi.h>

#include <cstdio>
#include <fstream>
#include <mutex>
#include <unordered_set>

#ifndef DWMWA_NCRENDERING_ENABLED
#define DWMWA_NCRENDERING_ENABLED 1
#endif
#ifndef DWMWA_CLOAKED
#define DWMWA_CLOAKED 14
#endif
#ifndef DWMWA_EXTENDED_FRAME_BOUNDS
#define DWMWA_EXTENDED_FRAME_BOUNDS 9
#endif

namespace forensic {
namespace {

LARGE_INTEGER g_freq{};
LARGE_INTEGER g_start{};
std::mutex g_mutex;
std::string g_log_path;
std::unordered_set<UINT> g_seen;

std::string LogPath() {
  char module_path[MAX_PATH] = {0};
  DWORD len = GetModuleFileNameA(nullptr, module_path, MAX_PATH);
  std::string path(module_path, len);
  size_t slash = path.find_last_of("\\/");
  std::string dir = (slash == std::string::npos) ? std::string() : path.substr(0, slash + 1);
  return dir + "morphic_forensic.log";
}

// Append `name` to `out` (pipe-separated) when (bits & flag) is set.
void AppendFlag(std::string& out, DWORD bits, DWORD flag, const char* name) {
  if ((bits & flag) == flag && flag != 0) {
    if (!out.empty()) out += "|";
    out += name;
  }
}

}  // namespace

void Init() {
  QueryPerformanceFrequency(&g_freq);
  QueryPerformanceCounter(&g_start);
  g_log_path = LogPath();
  std::lock_guard<std::mutex> lock(g_mutex);
  // FIELD SAFETY: preserve the previous run's log as <name>.prev.log before
  // truncating. Without this, a crash log is destroyed the instant the user
  // relaunches — exactly when you need it most. One generation back is enough
  // to retrieve a crash from the run that produced it.
  std::string prev_path = g_log_path;
  size_t dot = prev_path.find_last_of('.');
  prev_path = (dot == std::string::npos ? prev_path : prev_path.substr(0, dot)) +
              ".prev.log";
  MoveFileExA(g_log_path.c_str(), prev_path.c_str(), MOVEFILE_REPLACE_EXISTING);
  std::ofstream f(g_log_path, std::ios::trunc);
  if (f) {
    f << "=== MORPHIC FORENSIC TRACE (native) ===\n";
  }
}

void LogCrash(const char* subsystem, const char* message) {
  SYSTEMTIME st{};
  GetLocalTime(&st);
  char head[96];
  _snprintf_s(head, sizeof(head), _TRUNCATE, "[CRASH][T:%lu][%s] ",
              static_cast<unsigned long>(GetCurrentThreadId()), subsystem);
  char wall[48];
  _snprintf_s(wall, sizeof(wall), _TRUNCATE, "  @wall=%02d:%02d:%02d.%03d",
              st.wHour, st.wMinute, st.wSecond, st.wMilliseconds);

  // Try to take the lock; if a faulting thread already holds it, write anyway
  // rather than deadlock the crash path.
  bool locked = g_mutex.try_lock();
  {
    std::ofstream f(g_log_path, std::ios::app);
    if (f) {
      f << head << message << wall << "\n";
      f.flush();
    }
  }
  if (locked) g_mutex.unlock();
}

void Log(const char* subsystem, const std::string& message) {
  LARGE_INTEGER now{};
  QueryPerformanceCounter(&now);
  double sec = g_freq.QuadPart
                   ? static_cast<double>(now.QuadPart - g_start.QuadPart) /
                         static_cast<double>(g_freq.QuadPart)
                   : 0.0;

  SYSTEMTIME st{};
  GetLocalTime(&st);

  char head[192];
  _snprintf_s(head, sizeof(head), _TRUNCATE, "[+%07.3f][T:%lu][%s] ", sec,
              static_cast<unsigned long>(GetCurrentThreadId()), subsystem);

  char wall[48];
  _snprintf_s(wall, sizeof(wall), _TRUNCATE, "  @wall=%02d:%02d:%02d.%03d",
              st.wHour, st.wMinute, st.wSecond, st.wMilliseconds);

  std::lock_guard<std::mutex> lock(g_mutex);
  std::ofstream f(g_log_path, std::ios::app);
  if (f) {
    f << head << message << wall << "\n";
  }
}

bool FirstSeen(UINT message) {
  std::lock_guard<std::mutex> lock(g_mutex);
  return g_seen.insert(message).second;
}

const char* MessageName(UINT message) {
  switch (message) {
    case WM_CREATE: return "WM_CREATE";
    case WM_NCCREATE: return "WM_NCCREATE";
    case WM_NCCALCSIZE: return "WM_NCCALCSIZE";
    case WM_NCPAINT: return "WM_NCPAINT";
    case WM_NCACTIVATE: return "WM_NCACTIVATE";
    case WM_NCHITTEST: return "WM_NCHITTEST";
    case WM_GETMINMAXINFO: return "WM_GETMINMAXINFO";
    case WM_STYLECHANGED: return "WM_STYLECHANGED";
    case WM_STYLECHANGING: return "WM_STYLECHANGING";
    case WM_WINDOWPOSCHANGING: return "WM_WINDOWPOSCHANGING";
    case WM_WINDOWPOSCHANGED: return "WM_WINDOWPOSCHANGED";
    case WM_SHOWWINDOW: return "WM_SHOWWINDOW";
    case WM_ACTIVATE: return "WM_ACTIVATE";
    case WM_ACTIVATEAPP: return "WM_ACTIVATEAPP";
    case WM_SIZE: return "WM_SIZE";
    case WM_MOVE: return "WM_MOVE";
    case WM_PAINT: return "WM_PAINT";
    case WM_ERASEBKGND: return "WM_ERASEBKGND";
    case WM_DPICHANGED: return "WM_DPICHANGED";
    case WM_GETOBJECT: return "WM_GETOBJECT";
    case WM_SETCURSOR: return "WM_SETCURSOR";
    case WM_DESTROY: return "WM_DESTROY";
    case WM_NCDESTROY: return "WM_NCDESTROY";
    default: return nullptr;
  }
}

void DumpWindowStyles(const char* subsystem, HWND hwnd, const char* tag) {
  DWORD style = static_cast<DWORD>(GetWindowLongPtr(hwnd, GWL_STYLE));
  DWORD ex = static_cast<DWORD>(GetWindowLongPtr(hwnd, GWL_EXSTYLE));

  std::string sflags;
  AppendFlag(sflags, style, WS_POPUP, "WS_POPUP");
  AppendFlag(sflags, style, WS_CHILD, "WS_CHILD");
  AppendFlag(sflags, style, WS_CAPTION, "WS_CAPTION");      // WS_BORDER|WS_DLGFRAME
  AppendFlag(sflags, style, WS_THICKFRAME, "WS_THICKFRAME");
  AppendFlag(sflags, style, WS_SYSMENU, "WS_SYSMENU");
  AppendFlag(sflags, style, WS_MINIMIZEBOX, "WS_MINIMIZEBOX");
  AppendFlag(sflags, style, WS_MAXIMIZEBOX, "WS_MAXIMIZEBOX");
  AppendFlag(sflags, style, WS_VISIBLE, "WS_VISIBLE");
  AppendFlag(sflags, style, WS_CLIPCHILDREN, "WS_CLIPCHILDREN");
  AppendFlag(sflags, style, WS_CLIPSIBLINGS, "WS_CLIPSIBLINGS");

  std::string xflags;
  AppendFlag(xflags, ex, WS_EX_APPWINDOW, "WS_EX_APPWINDOW");
  AppendFlag(xflags, ex, WS_EX_TOOLWINDOW, "WS_EX_TOOLWINDOW");
  AppendFlag(xflags, ex, WS_EX_LAYERED, "WS_EX_LAYERED");
  AppendFlag(xflags, ex, WS_EX_CLIENTEDGE, "WS_EX_CLIENTEDGE");
  AppendFlag(xflags, ex, WS_EX_WINDOWEDGE, "WS_EX_WINDOWEDGE");
  AppendFlag(xflags, ex, WS_EX_NOREDIRECTIONBITMAP, "WS_EX_NOREDIRECTIONBITMAP");

  char buf[256];
  _snprintf_s(buf, sizeof(buf), _TRUNCATE,
              "%s hwnd=0x%p STYLE=0x%08lX [%s] EXSTYLE=0x%08lX [%s]", tag,
              static_cast<void*>(hwnd), static_cast<unsigned long>(style),
              sflags.c_str(), static_cast<unsigned long>(ex), xflags.c_str());
  Log(subsystem, buf);
}

void DumpDwmState(HWND hwnd) {
  BOOL nc_enabled = FALSE;
  HRESULT hr_nc =
      DwmGetWindowAttribute(hwnd, DWMWA_NCRENDERING_ENABLED, &nc_enabled,
                            sizeof(nc_enabled));

  DWORD cloaked = 0;
  HRESULT hr_cloak =
      DwmGetWindowAttribute(hwnd, DWMWA_CLOAKED, &cloaked, sizeof(cloaked));

  RECT frame{};
  HRESULT hr_frame = DwmGetWindowAttribute(hwnd, DWMWA_EXTENDED_FRAME_BOUNDS,
                                           &frame, sizeof(frame));
  RECT wr{};
  GetWindowRect(hwnd, &wr);

  char buf[320];
  _snprintf_s(
      buf, sizeof(buf), _TRUNCATE,
      "DWM hwnd=0x%p NCRENDERING_ENABLED=%s(hr=0x%08lX) CLOAKED=%lu(hr=0x%08lX) "
      "WindowRect=(%ld,%ld,%ld,%ld) ExtendedFrame=(%ld,%ld,%ld,%ld)(hr=0x%08lX)",
      static_cast<void*>(hwnd),
      SUCCEEDED(hr_nc) ? (nc_enabled ? "TRUE" : "FALSE") : "?",
      static_cast<unsigned long>(hr_nc), static_cast<unsigned long>(cloaked),
      static_cast<unsigned long>(hr_cloak), wr.left, wr.top, wr.right, wr.bottom,
      frame.left, frame.top, frame.right, frame.bottom,
      static_cast<unsigned long>(hr_frame));
  Log("DWM", buf);
}

namespace {

BOOL CALLBACK EnumChildProc(HWND child, LPARAM /*param*/) {
  char cls[128] = {0};
  GetClassNameA(child, cls, sizeof(cls));
  DWORD style = static_cast<DWORD>(GetWindowLongPtr(child, GWL_STYLE));
  DWORD ex = static_cast<DWORD>(GetWindowLongPtr(child, GWL_EXSTYLE));

  char buf[256];
  _snprintf_s(buf, sizeof(buf), _TRUNCATE,
              "  child hwnd=0x%p class=\"%s\" STYLE=0x%08lX EXSTYLE=0x%08lX "
              "visible=%d parent=0x%p",
              static_cast<void*>(child), cls,
              static_cast<unsigned long>(style), static_cast<unsigned long>(ex),
              IsWindowVisible(child) ? 1 : 0,
              static_cast<void*>(GetParent(child)));
  Log("GRAPH", buf);
  return TRUE;
}

}  // namespace

void DumpHwndGraph(HWND root) {
  char cls[128] = {0};
  GetClassNameA(root, cls, sizeof(cls));

  char buf[320];
  _snprintf_s(buf, sizeof(buf), _TRUNCATE,
              "ROOT hwnd=0x%p class=\"%s\" visible=%d parent=0x%p owner=0x%p "
              "GA_ROOT=0x%p",
              static_cast<void*>(root), cls, IsWindowVisible(root) ? 1 : 0,
              static_cast<void*>(GetParent(root)),
              static_cast<void*>(GetWindow(root, GW_OWNER)),
              static_cast<void*>(GetAncestor(root, GA_ROOT)));
  Log("GRAPH", buf);

  EnumChildWindows(root, EnumChildProc, 0);
}

}  // namespace forensic
