#ifndef RUNNER_VALIDATION_SESSION_REPORT_WRITER_H_
#define RUNNER_VALIDATION_SESSION_REPORT_WRITER_H_

#include <string>

struct RuntimeTelemetry;

// PHASE 7D — SessionReportWriter
//
// Owns the validation_reports/<ts-pid>/ session directory and emits a single
// human-readable summary JSON at session end (in addition to the per-stream
// .ndjson files written live by other subsystems).
//
// Ownership note: the session directory string is the SHARED root that
// ForensicLogger instances consume. Construct this FIRST in the harness so
// every other module can read session_dir().
class SessionReportWriter {
 public:
  SessionReportWriter();
  ~SessionReportWriter();

  SessionReportWriter(const SessionReportWriter&) = delete;
  SessionReportWriter& operator=(const SessionReportWriter&) = delete;

  // Path with trailing separator. Empty if directory creation failed.
  const std::string& session_dir() const { return session_dir_; }

  // Write a single pretty-printed session_summary.json containing the final
  // telemetry snapshot + reason. Idempotent — repeated calls overwrite.
  void WriteSummary(const RuntimeTelemetry& telemetry,
                    const std::string& mode_name,
                    const std::string& reason);

 private:
  std::string session_dir_;
  bool wrote_summary_ = false;
};

#endif  // RUNNER_VALIDATION_SESSION_REPORT_WRITER_H_
