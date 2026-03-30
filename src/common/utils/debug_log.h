#pragma once

#include <cstdarg>
#include <cstdio>
#include <cstdlib>

namespace vinput::debug {

inline constexpr char kEnv[] = "VINPUT_DEBUG";

inline bool Enabled() {
  const char *value = std::getenv(kEnv);
  if (!value) {
    return false;
  }
  return value[0] == '1' || value[0] == 't' || value[0] == 'T' ||
         value[0] == 'y' || value[0] == 'Y';
}

inline void Log(const char *format, ...) {
  if (!Enabled()) {
    return;
  }

  std::fputs("[vinput-debug] ", stderr);
  va_list args;
  va_start(args, format);
  std::vfprintf(stderr, format, args);
  va_end(args);
}

}  // namespace vinput::debug
