#include <string>
#include <random>

#include "mage/public/message.h"  // For `kIdentifierSize`.
#include "mage/public/util.h"

namespace mage {
namespace util {

namespace {

thread_local std::mt19937 generator(std::random_device{}());

}; // namespace.

// TODO(domfarolino): Using strings as random identifiers complicates things. In
// particular, it means the control messages have to manage character buffers
// intead of something more contained and easily copyable, like random integers.
// We should consider changing this.
std::string RandomIdentifier() {
  std::string return_id;

  std::uniform_int_distribution<int> distribution(0, sizeof(alphanum) - 1);
  for (int i = 0; i < kIdentifierSize; ++i) {
    return_id += alphanum[distribution(generator)];
  }

  return return_id;
}

};  // namespace util
};  // namespace mage
