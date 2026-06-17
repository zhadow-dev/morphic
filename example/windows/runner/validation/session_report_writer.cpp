#include "validation/session_report_writer.h"

#include <cstdio>
#include <fstream>

#include "forensic_trace.h"
#include "validation/forensic_logger.h"
#include "validation/runtime_telemetry.h"

SessionReportWriter::SessionReportWriter() {
  session_dir_ = ForensicLogger::CreateSessionDir();
  if (!session_dir_.empty()) {
    forensic::Log("REPORT", "session dir = " + session_dir_);
  } else {
    forensic::Log("REPORT", "session dir creation FAILED (no artifacts will be written)");
  }
}

SessionReportWriter::~SessionReportWriter() {
  if (!wrote_summary_) {
    forensic::Log("REPORT", "shutdown without WriteSummary — summary skipped");
  }
}

void SessionReportWriter::WriteSummary(const RuntimeTelemetry& t,
                                       const std::string& mode_name,
                                       const std::string& reason) {
  if (session_dir_.empty()) return;
  const std::string path = session_dir_ + "session_summary.json";
  std::ofstream f(path, std::ios::out | std::ios::trunc);
  if (!f) {
    forensic::Log("REPORT", "session_summary.json open FAILED path=" + path);
    return;
  }

  auto kv_int = [&](const char* k, long long v, bool last = false) {
    f << "    \"" << k << "\": " << v << (last ? "\n" : ",\n");
  };
  auto kv_num = [&](const char* k, double v, bool last = false) {
    char buf[32];
    _snprintf_s(buf, sizeof(buf), _TRUNCATE, "%.3f", v);
    f << "    \"" << k << "\": " << buf << (last ? "\n" : ",\n");
  };
  auto kv_str = [&](const char* k, const std::string& v, bool last = false) {
    f << "    \"" << k << "\": \"" << v << "\"" << (last ? "\n" : ",\n");
  };

  const int ticks = t.tick_count.load();
  const double sum_ms = t.sum_tick_interval_ms.load();
  const double avg = (ticks > 1) ? (sum_ms / static_cast<double>(ticks - 1)) : 0.0;

  f << "{\n";
  f << "  \"meta\": {\n";
  kv_str("mode", mode_name);
  kv_str("reason", reason, true);
  f << "  },\n";
  f << "  \"clock\": {\n";
  kv_int("tick_count", ticks);
  kv_num("last_tick_interval_ms", t.last_tick_interval_ms.load());
  kv_num("peak_tick_interval_ms", t.peak_tick_interval_ms.load());
  kv_num("avg_tick_interval_ms", avg);
  kv_int("tick_starvation_count", t.tick_starvation_count.load());
  kv_int("subscriber_peak", t.subscriber_peak.load());
  // PHASE 8A.2 — interval histogram. Healthy distribution after timer fix
  // should have the bulk in tick_bucket_12_20ms with a thin >32ms tail.
  kv_int("tick_bucket_under_12ms", t.tick_bucket_under_12ms.load());
  kv_int("tick_bucket_12_20ms",   t.tick_bucket_12_20ms.load());
  kv_int("tick_bucket_20_32ms",   t.tick_bucket_20_32ms.load());
  kv_int("tick_bucket_over_32ms", t.tick_bucket_over_32ms.load(), true);
  f << "  },\n";
  f << "  \"interactions\": {\n";
  kv_int("started", t.interactions_started.load());
  kv_int("ended", t.interactions_ended.load());
  kv_int("cancelled", t.interactions_cancelled.load());
  kv_int("capture_acquisitions", t.capture_acquisitions.load());
  kv_int("capture_releases", t.capture_releases.load(), true);
  f << "  },\n";
  f << "  \"projection\": {\n";
  kv_int("projections", t.projections.load());
  kv_int("projections_dropped", t.projections_dropped.load());
  kv_int("projection_storms", t.projection_storms.load());
  kv_int("reentrant_drops", t.reentrant_drops.load(), true);
  f << "  },\n";
  f << "  \"audit\": {\n";
  kv_int("audit_ticks", t.audit_ticks.load());
  kv_int("audit_warnings", t.audit_warnings.load());
  kv_int("peak_target_presented_lag_px", t.peak_target_presented_lag_px.load());
  kv_int("peak_presented_native_lag_px", t.peak_presented_native_lag_px.load(), true);
  f << "  },\n";
  f << "  \"integrity\": {\n";
  kv_int("integrity_checks", t.integrity_checks.load());
  kv_int("integrity_failures", t.integrity_failures.load(), true);
  f << "  },\n";
  f << "  \"surfaces\": {\n";
  kv_int("created", t.surfaces_created.load());
  kv_int("destroyed", t.surfaces_destroyed.load());
  kv_int("live", t.surfaces_live.load(), true);
  f << "  },\n";
  f << "  \"transactions\": {\n";
  kv_int("begun", t.txn_begun.load());
  kv_int("committed", t.txn_committed.load(), true);
  f << "  },\n";
  f << "  \"input\": {\n";
  kv_int("pointer_events", t.pointer_events.load());
  kv_num("peak_velocity_px_per_s", t.peak_velocity_px_per_s.load(), true);
  f << "  }\n";
  f << "}\n";
  f.close();
  wrote_summary_ = true;
  forensic::Log("REPORT", "session_summary.json written");
}
