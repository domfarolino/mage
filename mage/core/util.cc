#include <string>

#include "mage/public/message.h"  // For `kIdentifierSize`.
#include "mage/public/util.h"

namespace mage {
namespace util {

// TODO(domfarolino): Using strings as random identifiers complicates things. In
// particular, it means the control messages have to manage character buffers
// intead of something more contained and easily copyable, like random integers.
// We should consider changing this.
std::string RandomIdentifier() {
  std::string return_id;
  for (int i = 0; i < kIdentifierSize; ++i) {
    return_id += alphanum[rand() % (sizeof(alphanum) - 1)];
  }

  return return_id;
}

};  // namespace util
};  // namespace mage
