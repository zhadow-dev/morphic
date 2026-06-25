#ifndef RUNNER_VALIDATION_FORENSIC_LOGGER_H_
#define RUNNER_VALIDATION_FORENSIC_LOGGER_H_

#include <windows.h>

#include <fstream>
#include <mutex>
#include <sstream>
#include <string>
#include <string_view>

// PHASE 7D — ForensicLogger
//
// Structured NDJSON line writer for the validation harness. One JSON object per
// line, written through a single mutex, flushed every write. Crash-safe.
//
// Output is written to <validation_dir>/<basename>.ndjson where validation_dir
// is created next to the exe at construction. Multiple streams (event trace,
// projection trace, tick trace) each get their own ForensicLogger instance.
//
// Build records with the JsonLine helper: chained .Add()/.Rect()/.Num()/.Int().
//   logger.Log(JsonLine().Add("kind", "tick").Num("interval_ms", 16.3).Build());
class JsonLine {
 public:
  JsonLine();
  JsonLine& Str(std::string_view key, std::string_view value);
  JsonLine& Int(std::string_view key, long long value);
  JsonLine& Num(std::string_view key, double value);
  JsonLine& Bool(std::string_view key, bool value);
  JsonLine& Rect(std::string_view key, const RECT& r);
  std::string Build();
 private:
  std::ostringstream buf_;
  bool first_ = true;
  void Sep();
};

class ForensicLogger {
 public:
  // basename like "event_trace" produces validation_reports/<ts>/event_trace.ndjson
  ForensicLogger(const std::string& session_dir, const std::string& basename);
  ~ForensicLogger();

  ForensicLogger(const ForensicLogger&) = delete;
  ForensicLogger& operator=(const ForensicLogger&) = delete;

  // Log one prebuilt JSON object as a single NDJSON line (newline appended).
  void Log(const std::string& json_object);

  // Free-form helper that wraps the JsonLine builder and tags a timestamp.
  void LogEvent(JsonLine& line);

  // Create the validation_reports/<timestamp>/ directory next to the exe.
  // Idempotent. Returns "" on failure.
  static std::string CreateSessionDir();

 private:
  std::ofstream out_;
  std::mutex mu_;
};

#endif  // RUNNER_VALIDATION_FORENSIC_LOGGER_H_
