#ifndef MAGE_CORE_UTIL_H_
#define MAGE_CORE_UTIL_H_

#include <string>

namespace mage {
namespace util {

static const char alphanum[] =
    "0123456789"
    "!@#$%^&*"
    "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
    "abcdefghijklmnopqrstuvwxyz";

std::string RandomIdentifier();

static bool VerboseLogging = false;

// Some sort of hacky ways to prevent tons of diagnostic logging. If verbose
// logging is enabled, the macros below will log everything. We should probably
// conditionally define the log macros based on the existence of a debug
// preprocessor directive, so that:
//   1.) In "production" code, we're not constantly checking the
//       `VerboseLogging` variable
//   2.) In "debug" mode, we have the ability to turn on/off verbose logging as
//       usual
// Log a message on a single line with no newline appended.
#define LOG_SL(str, ...)              \
  if (::mage::util::VerboseLogging) { \
    printf(str, ##__VA_ARGS__);       \
  }
// Log a message with a new line appended to the end.
#define LOG(str, ...)                 \
  if (::mage::util::VerboseLogging) { \
    printf(str, ##__VA_ARGS__);       \
    printf("\n");                     \
  }

};  // namespace util
};  // namespace mage

#endif  // MAGE_CORE_UTIL_H_
