#include "validation/forensic_logger.h"

#include <windows.h>

#include <cstdio>
#include <ctime>
#include <filesystem>
#include <string>

namespace {

std::string EscapeJson(std::string_view s) {
  std::string out;
  out.reserve(s.size() + 2);
  for (char c : s) {
    switch (c) {
      case '"':  out += "\\\""; break;
      case '\\': out += "\\\\"; break;
      case '\n': out += "\\n";  break;
      case '\r': out += "\\r";  break;
      case '\t': out += "\\t";  break;
      default:
        if (static_cast<unsigned char>(c) < 0x20) {
          char buf[8];
          _snprintf_s(buf, sizeof(buf), _TRUNCATE, "\\u%04x", c);
          out += buf;
        } else {
          out += c;
        }
    }
  }
  return out;
}

std::string ExeDir() {
  char module_path[MAX_PATH] = {0};
  DWORD len = GetModuleFileNameA(nullptr, module_path, MAX_PATH);
  std::string path(module_path, len);
  size_t slash = path.find_last_of("\\/");
  return (slash == std::string::npos) ? std::string() : path.substr(0, slash + 1);
}

long long UnixUs() {
  FILETIME ft;
  GetSystemTimeAsFileTime(&ft);
  unsigned long long t = (static_cast<unsigned long long>(ft.dwHighDateTime) << 32) |
                          ft.dwLowDateTime;
  // FILETIME is 100ns intervals since 1601; convert to us since 1970.
  constexpr unsigned long long kEpochDiff100ns = 116444736000000000ULL;
  return static_cast<long long>((t - kEpochDiff100ns) / 10ULL);
}

}  // namespace

JsonLine::JsonLine() { buf_ << "{"; }

void JsonLine::Sep() {
  if (!first_) buf_ << ",";
  first_ = false;
}

JsonLine& JsonLine::Str(std::string_view key, std::string_view value) {
  Sep();
  buf_ << "\"" << EscapeJson(key) << "\":\"" << EscapeJson(value) << "\"";
  return *this;
}

JsonLine& JsonLine::Int(std::string_view key, long long value) {
  Sep();
  buf_ << "\"" << EscapeJson(key) << "\":" << value;
  return *this;
}

JsonLine& JsonLine::Num(std::string_view key, double value) {
  Sep();
  char num[32];
  _snprintf_s(num, sizeof(num), _TRUNCATE, "%.3f", value);
  buf_ << "\"" << EscapeJson(key) << "\":" << num;
  return *this;
}

JsonLine& JsonLine::Bool(std::string_view key, bool value) {
  Sep();
  buf_ << "\"" << EscapeJson(key) << "\":" << (value ? "true" : "false");
  return *this;
}

JsonLine& JsonLine::Rect(std::string_view key, const RECT& r) {
  Sep();
  buf_ << "\"" << EscapeJson(key) << "\":[" << r.left << "," << r.top << ","
       << (r.right - r.left) << "," << (r.bottom - r.top) << "]";
  return *this;
}

std::string JsonLine::Build() {
  buf_ << "}";
  return buf_.str();
}

ForensicLogger::ForensicLogger(const std::string& session_dir,
                               const std::string& basename) {
  if (session_dir.empty()) return;
  const std::string path = session_dir + basename + ".ndjson";
  out_.open(path, std::ios::out | std::ios::trunc);
}

ForensicLogger::~ForensicLogger() {
  if (out_.is_open()) {
    out_.flush();
    out_.close();
  }
}

void ForensicLogger::Log(const std::string& json_object) {
  std::lock_guard<std::mutex> lock(mu_);
  if (!out_.is_open()) return;
  out_ << json_object << "\n";
  out_.flush();
}

void ForensicLogger::LogEvent(JsonLine& line) {
  // Inject timestamp at the head of every event automatically. The mutex on
  // Log() makes the timestamp-then-write atomic from observers' perspective.
  JsonLine stamped;
  stamped.Int("ts_us", UnixUs());
  // Append the caller's payload by stripping the leading '{' from line and the
  // trailing '}' from stamped, then joining with a comma. Simpler: just build
  // a fresh line that wraps both. Cheapest: rebuild — payloads are small.
  std::string body = line.Build();  // {"k":v,...}
  std::string head = stamped.Build();  // {"ts_us":...}
  // head ends in '}', body starts with '{'. Splice: head[:-1] + "," + body[1:]
  std::string merged = head.substr(0, head.size() - 1);
  if (body.size() > 2) {  // not just "{}"
    merged += "," + body.substr(1);
  } else {
    merged += "}";
  }
  Log(merged);
}

std::string ForensicLogger::CreateSessionDir() {
  namespace fs = std::filesystem;
  std::string base = ExeDir() + "validation_reports";
  std::error_code ec;
  fs::create_directories(base, ec);

  // Per-process subdir: YYYYMMDD-HHMMSS-PID. Time used for human ordering, PID
  // disambiguates rapid relaunches.
  std::time_t t = std::time(nullptr);
  std::tm tm{};
  localtime_s(&tm, &t);
  char stamp[32];
  std::strftime(stamp, sizeof(stamp), "%Y%m%d-%H%M%S", &tm);
  char dirname[64];
  _snprintf_s(dirname, sizeof(dirname), _TRUNCATE, "%s-%lu", stamp,
              static_cast<unsigned long>(GetCurrentProcessId()));

  std::string session = base + "\\" + dirname + "\\";
  fs::create_directories(session, ec);
  if (ec) return "";
  return session;
}
