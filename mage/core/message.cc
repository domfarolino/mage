#include "mage/public/message.h"

#include <memory>
#include <vector>

#include "mage/core/endpoint.h"

namespace mage {

//////////////////// MESSAGE ////////////////////

Message::Message(MessageType type) {
  int num_bytes_to_allocate = sizeof(MessageHeader);
  payload_buffer_.resize(num_bytes_to_allocate + payload_buffer_.size());

  // Set the MessageHeader's type to |type|. We'll finalize the header's size
  // in |FinalizeSize()|.
  GetMutableMessageHeader().type = type;
}

Message::Message(Message&& other) {
  payload_buffer_ = std::move(other.payload_buffer_);
  pipes_ = std::move(other.pipes_);
}

//////////////////// END MESSAGE ////////////////////

};  // namespace mage
