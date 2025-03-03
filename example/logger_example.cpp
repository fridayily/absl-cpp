#include "absl/log/check.h"
#include "absl/log/globals.h"
#include "absl/log/initialize.h"
#include "absl/log/log.h"
#include "absl/strings/str_format.h"
#include "absl/strings/str_split.h"
#include "absl/flags/flag.h"
#include "absl/flags/parse.h"
class LinePrinterLogSink : public absl::LogSink {
 public:
  LinePrinterLogSink() : fp_(fopen("/dev/lp0", "a")) {
    PCHECK(fp_ != nullptr) << "Failed to open /dev/lp0";
  }
  ~LinePrinterLogSink() {
    fputc('\f', fp_);
    PCHECK(fclose(fp_) == 0) << "Failed to close /dev/lp0";
  }
  void Send(const absl::LogEntry& entry) override {
    for (absl::string_view line :
         absl::StrSplit(entry.text_message_with_prefix(), absl::ByChar('\n'))) {
      // Overprint severe entries for emphasis:
      for (int i = static_cast<int>(absl::LogSeverity::kInfo);
           i <= static_cast<int>(entry.log_severity()); i++) {
        absl::FPrintF(fp_, "%s\r", line);
      }
      fputc('\n', fp_);
    }
  }

 private:
  FILE* const fp_;
};

ABSL_FLAG(std::string, log_file, "", "Path to the log file");

int main(int argc, char** argv) {
//  absl::ParseCommandLine(argc, argv);
  //  absl::SetStderrThreshold(absl::LogSeverityAtLeast::kInfo);
  absl::SetMinLogLevel(absl::LogSeverityAtLeast::kInfo);
  absl::InitializeLog();

  // 设置日志级别
  absl::SetMinLogLevel(absl::LogSeverityAtLeast::kInfo);

  LOG(INFO) << "info";
  LOG(WARNING) << "WARNING";

  VLOG(1) << "VLOG 1";
  VLOG(2) << "VLOG 2";

  LOG(ERROR) << "ERROR";


  return 0;
}