#include "crash_handler.h"

#include <windows.h>
#include <dbghelp.h>  // MiniDumpWriteDump (dbghelp.lib)

#include <cstdio>
#include <cstdlib>
#include <exception>

#include "forensic_trace.h"

namespace crash {
namespace {

// Set the instant we begin handling a crash so a fault INSIDE the handler
// (e.g. dbghelp tripping over a corrupted heap) can't recurse forever.
volatile LONG g_handling = 0;

// Fill `out` (size >= MAX_PATH) with "<exe dir>\<leaf>" and return it.
const char* InExeDir(char* out, size_t out_size, const char* leaf) {
  char module_path[MAX_PATH] = {0};
  DWORD len = GetModuleFileNameA(nullptr, module_path, MAX_PATH);
  // Trim back to the trailing separator (keep it).
  while (len > 0 && module_path[len - 1] != '\\' && module_path[len - 1] != '/')
    --len;
  module_path[len] = '\0';
  _snprintf_s(out, out_size, _TRUNCATE, "%s%s", module_path, leaf);
  return out;
}

// Crash-time minidump. Uses only stack buffers + direct Win32 — no heap, no
// std::string, no locks — because the process state is already untrustworthy.
void WriteDump(EXCEPTION_POINTERS* ep) {
  SYSTEMTIME st{};
  GetLocalTime(&st);
  char leaf[64] = {0};
  _snprintf_s(leaf, sizeof(leaf), _TRUNCATE,
              "morphic_crash_%04d%02d%02d_%02d%02d%02d.dmp", st.wYear,
              st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
  char path[MAX_PATH] = {0};
  InExeDir(path, sizeof(path), leaf);

  HANDLE file = CreateFileA(path, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                            FILE_ATTRIBUTE_NORMAL, nullptr);
  if (file == INVALID_HANDLE_VALUE) return;

  MINIDUMP_EXCEPTION_INFORMATION info{};
  info.ThreadId = GetCurrentThreadId();
  info.ExceptionPointers = ep;
  info.ClientPointers = FALSE;

  // Normal dump + per-thread context = all thread call stacks and the module
  // list, which is what a post-mortem triage actually needs. Compact on disk.
  const MINIDUMP_TYPE type = static_cast<MINIDUMP_TYPE>(
      MiniDumpNormal | MiniDumpWithThreadInfo | MiniDumpWithUnloadedModules);

  MiniDumpWriteDump(GetCurrentProcess(), GetCurrentProcessId(), file, type,
                    ep ? &info : nullptr, nullptr, nullptr);
  CloseHandle(file);

  // Leave a breadcrumb in the forensic log naming the dump that was written.
  char note[128] = {0};
  _snprintf_s(note, sizeof(note), _TRUNCATE, "minidump written: %s", leaf);
  forensic::LogCrash("CRASH", note);
}

LONG WINAPI UnhandledFilter(EXCEPTION_POINTERS* ep) {
  // First crasher wins; a recursive fault falls straight through to terminate.
  if (InterlockedExchange(&g_handling, 1) != 0) return EXCEPTION_EXECUTE_HANDLER;

  char line[320] = {0};
  const void* addr =
      ep && ep->ExceptionRecord ? ep->ExceptionRecord->ExceptionAddress : nullptr;
  const unsigned long code =
      ep && ep->ExceptionRecord ? ep->ExceptionRecord->ExceptionCode : 0;

  // Resolve the faulting MODULE + offset (no symbolizer needed): tells us which
  // DLL crashed — flutter_windows.dll vs ours vs a system DLL — and the stable
  // offset to compare across runs.
  char modinfo[160] = "module=?";
  HMODULE mod = nullptr;
  if (addr != nullptr &&
      GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                             GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                         reinterpret_cast<LPCSTR>(addr), &mod)) {
    char path[MAX_PATH] = {0};
    GetModuleFileNameA(mod, path, MAX_PATH);
    const char* leaf = path;
    for (const char* p = path; *p; ++p)
      if (*p == '\\' || *p == '/') leaf = p + 1;
    const uintptr_t off = reinterpret_cast<uintptr_t>(addr) -
                          reinterpret_cast<uintptr_t>(mod);
    _snprintf_s(modinfo, sizeof(modinfo), _TRUNCATE, "module=%s+0x%llX", leaf,
                static_cast<unsigned long long>(off));
  }

  _snprintf_s(line, sizeof(line), _TRUNCATE,
              "UNHANDLED EXCEPTION code=0x%08lX addr=0x%p tid=%lu %s", code,
              addr, static_cast<unsigned long>(GetCurrentThreadId()), modinfo);
  forensic::LogCrash("CRASH", line);

  WriteDump(ep);

  // We captured what we can; let the process die. (Returning EXECUTE_HANDLER
  // unwinds to process exit since wWinMain has no enclosing __try.)
  return EXCEPTION_EXECUTE_HANDLER;
}

void OnTerminate() {
  if (InterlockedExchange(&g_handling, 1) == 0) {
    forensic::LogCrash("CRASH", "std::terminate (unhandled C++ exception)");
    WriteDump(nullptr);  // no EXCEPTION_POINTERS — still dumps thread stacks
  }
  // Default terminate behavior (abort) follows.
  std::abort();
}

}  // namespace

void Install() {
  SetUnhandledExceptionFilter(UnhandledFilter);
  std::set_terminate(OnTerminate);
  forensic::Log("BOOT", "crash handler installed (minidump + crash log)");
}

}  // namespace crash
