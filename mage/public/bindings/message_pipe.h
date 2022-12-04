#ifndef MAGE_CORE_HANDLES_H_
#define MAGE_CORE_HANDLES_H_

namespace mage {

// Typedefing this for explicitness. Each `MessagePipe` references an underlying
// `Endpoint`, whose peer's address may be local or remote.
typedef uint32_t MessagePipe;

static const MessagePipe kInvalidPipe = 0;

};  // namespace mage

#endif  // MAGE_CORE_HANDLES_H_
