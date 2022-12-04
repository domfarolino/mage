#ifndef MAGE_CORE_MESSAGE_H_
#define MAGE_CORE_MESSAGE_H_

#include <inttypes.h>

#include <cstring>
#include <queue>
#include <string>
#include <vector>

#include "base/check.h"
#include "mage/public/bindings/message_pipe.h"
#include "mage/public/util.h"

namespace mage {

namespace {

// The pointer helpers below were pulled from Chromium.
// Pointers are encoded in byte buffers as relative offsets. The offsets are
// relative to the address of where the offset *value* is stored, such that the
// pointer may be recovered with the expression:
//
//   ptr = reinterpret_cast<char*>(offset) + *offset
//
// The above expression takes the byte address of where the actual offset
// variable lives, and adds (*offset) bytes to this address, which is where the
// actual value that the pointer points to lives in memory.
//
// A null pointer is encoded as an offset value of 0.

// This method takes the two parameters:
//   - |ptr_storage|: The void* address of where exactly we're storing the value
//     that we'd like to point to.
//   - |out_offset|: The address of where we're actually storing the offset.
//     This is analogous to a stack-allocated pointer variable, whereas the
//     |ptr_storage| is the elsewhere-allocated address of the variable-sized
//     value we're pointing to.
inline void EncodePointer(const void* ptr_storage, uint64_t* out_offset) {
  if (!ptr_storage) {
    *out_offset = 0;
    return;
  }

  const char* ptr_storage_location = reinterpret_cast<const char*>(ptr_storage);
  const char* offset_location = reinterpret_cast<const char*>(out_offset);
  CHECK(ptr_storage_location > offset_location);

  *out_offset = static_cast<uint64_t>(ptr_storage_location - offset_location);
}

// Note: This function doesn't validate the encoded pointer value.
inline const void* DecodePointer(const uint64_t* offset) {
  if (!*offset)
    return nullptr;

  const char* address_of_offset_placeholder =
      reinterpret_cast<const char*>(offset);
  return address_of_offset_placeholder + *offset;
}

};  // namespace

static const int kInvalidFragmentStartingIndex = -1;
static const int kIdentifierSize = 15;

enum MessageType : int {
  // This sends the maiden message to a new peer node along with a bootstrap
  // endpoint identifier that the peer node will add. The peer node is then
  // expected to respond by sending the ACCEPT_INVITATION message below. The
  // inviter's boostrap endpoint is local and ephemeral, used to queue messages
  // that will eventually make it to the fresh peer node. The state of things in
  // the inviter node after sending this message looks like something like:
  // +------------------------------+
  // |           Node 1             |
  // | +--------+       +---------+ |
  // | |Local   |       |Ephemeral| |
  // | |Endpoint|       |Endpoint | |
  // | |  peer-+|------>|         | |
  // | |        |<------|-+peer   | |
  // | +--------+       +---------+ |
  // +-------------------------------+
  SEND_INVITATION,
  // A freshly-created peer node will send this message to its inviter node
  // after receiving the above SEND_INVITATION message and recovering the
  // ephemeral endpoint identifier from it. Once the fresh peer recovers the
  // endpoint identifier, it adds it as a local endpoint, whose peer is the
  // inviter node's "Local Endpoint" above. When the inviter receives this
  // ACCEPT_INVITATION message, it knows to do two things:
  //   1.) Set "Local Endpoint"'s peer to the remote endpoint in the new peer
  //       node
  //   2.) Set "Ephemeral Endpoint" in a proxying state, flushing all messages
  //       it may have queued to the remote endpoint in the peer node
  // At this point (once the inviter receives this message), chain of endpoints
  // looks like so:
  //     +-------------------------------+
  //     |                               |
  //     v                   Proxying    +
  //   Local      Ephemeral+---------> Remote
  //  Endpoint     Endpoint           Endpoint
  //     +                               ^
  //     |                               |
  //     +-------------------------------+
  //
  //     ^                 ^             ^
  //     |                 |             |
  //     +------Node 1-----+-----Node2---+
  ACCEPT_INVITATION,
  // A message created by a library user of mage. These are generic and we use
  // the message header to determine how to route them.
  USER_MESSAGE,
};

// CS101 Lesson: Traditionally, a pointer is a stack-allocated variable (and
// therefore its size is statically known) that simply holds the address of
// often (but not always) a heap-allocated variable. It can hold the *actual*
// physical address of the value it points to because that address is stable --
// it will not change on its own.
//
// When it comes to message serialization and deserialization, a given message
// will hold both static- and variable-sized values, and therefore it is
// convenient to represent the message parameters as a struct of members, each
// of which represents a message parameter either fixed-size of dynamic-size.
// This requires the members that represent dynamic-sized to basically be
// pointers (aka their size is statically-known), pointing to some arbitrary
// location in the message payload buffer where the actual variable-sized value
// of the message parameter can be found. We can't use traditional pointers that
// store the address of a variable though because the contents of the message
// buffer is copied across processes, so no memory addresses referenced by the
// message parameters will be stable.
//
// To get around this, we use a different pointer-like mechanism. Structs are
// used to represent a bag of message parameters, and members of these structs
// that themselves represent an "array-like" parameter (string, array, etc) are
// of type `Pointer`, which is just another struct that holds an `offset`
// integer instead of an actual address. The value of `offset` represents the
// number of bytes in front of the address of `offset` that the actual value of
// the parameter is stored in. For example, imagine a message with the following
// parameters:
//   - string str
//   - string str_2
//   - int b
// The struct that would be generate for this message would look like:
//
// struct MESSAGE_PARAMS {
//   mage::Pointer<ArrayHeader<char>> str;
//   mage::Pointer<ArrayHeader<char>> str_2;
//   int a;
// };
//
// In memory, the struct would look like so (assuming 4-byte integers just for
// drawing simplicity):
// +------------+
// |str_offset1 |\.
// +------------+ \.
// |str_offset2 |  |
// +------------+  | 4 bytes to store `str`'s offset
// |str_offset3 |  |
// +------------+ /
// |str_offset4 |/
// +------------+
// |str2_offset1|\.
// +------------+ \.
// |str2_offset2|  |
// +------------+  | 4 bytes to store value of `str_2`'s offset
// |str2_offset3|  |
// +------------+ /
// |str2_offset4|/
// +------------+
// |     a1     |\.
// +------------+ \.
// |     a2     |  |
// +------------+  | 4 bytes to store value of `a`
// |     a3     |  |
// +------------+ /
// |     a4     |/
// +------------+
//
// When the serialization code attempts to encode the contents of `str`, it
// will:
//   1. Expand the dynamically-sized backing payload buffer by the number of
//      characters needed to support the actual value of `str`.
//   2. Grab the address of the first available slot at the end of the payload
//      buffer
//   3. Find the *difference* (an integer) between the addresses of the
//      following:
//        a. The first available slot at the end of the buffer
//        b. Where `str`'s `offset` variable is stored (i.e., `str_offset1`)
//   4. Set the value of `str`'s `offset` variable to the integer difference
//      computed in (3).
// This way, we avoid encoding the actual addresses of anything in the pointer
// variables, but instead just encode the *relative* offset between variables,
// which will remain stable across processes as long as the message buffer is
// correctly copied.
//
// When it comes time to encode `str_2` we do the exact same thing. Note that we
// don't need to know how long `str` is or where it ends, we simply grab the
// first available slot on the end of the backing payload buffer after expanding
// it enough to fit the actual runtime contents of `str_2` and so on.
//
//
// Decoding a `Pointer` works similar to encoding, but in reverse. In order to
// go from `offset` to the address of the first byte of data that `Pointer`
// represents, we simply take the address of `offset` and add to the number (of
// bytes) written to `offset` as its value. That jumps ahead `offset` bytes from
// the address of `offset`, giving us a pointer to the first byte in front of
// `offset` where the data that `Pointer` represents is stored.
template <typename T>
struct Pointer {
  void Set(T* ptr) { EncodePointer(ptr, &offset); }

  const T* Get() const { return static_cast<const T*>(DecodePointer(&offset)); }

  T* Get() {
    return static_cast<T*>(const_cast<void*>(DecodePointer(&offset)));
  }

  uint64_t offset = 0;
};

// Arrays are stored in serialized message buffers as:
//   mage::Pointer<ArrayHeader<T>> array;
//
// When we decode a `Pointer`, this gives us the address (in the message buffer)
// of the first thing stored in the pointer, which is an `ArrayHeader`.
// `ArrayHeader` is a `Pointer`-like object, where it stores data *about* the
// array (namely the number of elements in `num_elements`). That means in order
// to go from `ArrayHeader` to the actual data in the array, we have to jump
// ahead in memory by exactly `sizeof(ArrayHeader)` bytes; see `array_storage()`
// below.
template <typename T>
struct ArrayHeader {
  T* array_storage() {
    // Take our address, and jump ahead "1" of "us", which is really jumping
    // ahead |sizeof(*this)| bytes. This will get us to the very first byte
    // after |this|, which is where the array data is actually stored.
    return reinterpret_cast<T*>(this + 1);
  }

  int num_elements;
};

// When a message sends a `MessagePipe` (representing an existing, local
// `Endpoint`) over an existing connection, the pipe may very well end up in
// another process which means we need a way to essentially send the local
// `Endpoint` over to that process. The way we achieve this is not by actually
// sending the C++ `Endpoint` object to the other process, but by sending all of
// the information about it that the receiver would need to know to create an
// endpoint that looks just like it. All of that information is captured by
// `EndpointDescriptor`, which is what we send over the wire in place of a given
// `MessagePipe` that gets sent.
struct EndpointDescriptor {
  // The endpoint that `this` describes in the node that `this` was created in.
  // It allows consumers of `this` to look up the endpoint that backs `this` in
  // said node/process. See the next member for the cross-process case.
  char endpoint_name[kIdentifierSize];
  // If `this` is passed cross-process as a directive to create an endpoint over
  // there, the remote endpoint we create cannot have the same name as
  // `endpoint_name`. Therefore, we up-front generate a new name that this
  // endpoint *would* have if it were passed to another node. That way, if
  // `this` actually does get passed to another process, the originating
  // process:
  //   1.) Looks up the endpoint backing `this`, via `endpoint_name`
  //   2.) Sets that endpoint's `proxy_target` to the address described by
  //       (destinationnode/`cross_node_endpoint_name`)
  //   3.) Sends `this` to the destination node, so that it creates an endpoint
  //       whose name is `cross_node_endpoint_name`, so that the originating
  //       process can automatically start targeting it.
  char cross_node_endpoint_name[kIdentifierSize];
  char peer_node_name[kIdentifierSize];
  char peer_endpoint_name[kIdentifierSize];

  void Print() const {
    LOG("  endpoint_name: %.*s", kIdentifierSize, endpoint_name);
    LOG("  cross_node_endpoint_name: %.*s", kIdentifierSize,
        cross_node_endpoint_name);
    LOG("  peer_node_name: %.*s", kIdentifierSize, peer_node_name);
    LOG("  peer_endpoint_name: %.*s", kIdentifierSize, peer_endpoint_name);
  }

  EndpointDescriptor() = default;

  EndpointDescriptor(const EndpointDescriptor& other) {
    memcpy(endpoint_name, other.endpoint_name, kIdentifierSize);
    memcpy(cross_node_endpoint_name, other.cross_node_endpoint_name,
           kIdentifierSize);
    memcpy(peer_node_name, other.peer_node_name, kIdentifierSize);
    memcpy(peer_endpoint_name, other.peer_endpoint_name, kIdentifierSize);
  }
};

struct MessageHeader {
  // The *overall* message size, including the MessageHeader.
  int size;
  // All message types that are not |MessageType::USER_MESSAGE| are considered
  // "control" messages, in that they don't target a particular endpoint, but
  // are destined for the node itself. |Node| uses this member to distinguish
  // control messages from user messages, so they can be handled correctly.
  MessageType type;
  // When |type == MessageType::USER_MESSAGE|, this message will correspond to a
  // mage method within an interface. This member identifies which mage method
  // in an interface this message corresponds to, so it can be deserialized and
  // dispatched correctly to the interface implementation.
  int user_message_id;

  // Messages are sent over a particular endpoint. That endpoint tells us the
  // name of the target node and the target endpoint. The name of the target
  // node is outside the scope of the message, since it is used by `Node` and
  // `Channel`. But once the message leaves an origin node and ends up in the
  // target node, it must tell the target node which particular remote endpoint
  // it is targeting, which is described by this member.
  char target_endpoint[kIdentifierSize];

  // Both user and control messages can carry `MessagePipe`s to expand the
  // number of connections between two nodes/processes. User message
  // deserialization happens on the thread that the `Receiver` is bound to
  // (typically this is the UI thread) by interface-specific generated code.
  // However, if a user message carries `MessagePipe`s with it (which serialize
  // to `EndpointDescriptor`s in the actual message buffer), we must unpack
  // these and create concrete `Endpoint`s to represent these pipes on the IO
  // thread, before we dispatch the message to the `Receiver`. These `Endpoint`s
  // must be created and registered before message dispatching because the very
  // next message that may come in on the IO thread might be bound for one of
  // the `Endpoint`s described by the previous message. Therefore,
  // `num_endpoints_in_message` tells us up-front how many `EndpointDescriptor`s
  // we have to unpack and recover before we do the message-specific
  // deserialization and dispatching on the `Receiver` thread.
  Pointer<ArrayHeader<EndpointDescriptor>> endpoints_in_message;
};

class Message final {
 public:
  Message(MessageType type);

  Message(const Message&) = delete;
  Message operator=(const Message&) = delete;
  Message(Message&& other);

  // This method will always start reading at the first byte after the
  // MessageHeader in memory. In order to read or manipulate the MessageHeader,
  // use `GetMutableMessageHeader()`.
  template <typename MessageFragment>
  MessageFragment* Get(int starting_index) {
    CHECK_GE(starting_index, 0);
    return reinterpret_cast<MessageFragment*>(payload_buffer_.data() +
                                              starting_index);
  }

  template <typename MessageFragment>
  MessageFragment* GetView() {
    return Get<MessageFragment>(/*starting_index=*/sizeof(MessageHeader));
  }

  MessageHeader& GetMutableMessageHeader() {
    return *Get<MessageHeader>(/*starting_index=*/0);
  }

  // Must be called before actually sending the message.
  void FinalizeSize() {
    GetMutableMessageHeader().size = payload_buffer_.size();
  }

  void ConsumeBuffer(std::vector<char>&& incoming_buffer) {
    payload_buffer_ = std::move(incoming_buffer);
  }

  // Returns a mutable view (over the message buffer) of all the endpoint
  // descriptors in the message buffer. Most consumers of this will not need to
  // mutate any of the descriptors, but some do. See
  // `Node::PrepareToForwardUserMessage()`.
  std::vector<EndpointDescriptor*> GetEndpointDescriptors() {
    Pointer<ArrayHeader<EndpointDescriptor>>& endpoints_pointer =
        GetMutableMessageHeader().endpoints_in_message;
    if (!endpoints_pointer.Get()) {
      return {};
    }

    std::vector<EndpointDescriptor*> endpoint_descriptors;
    for (int i = 0; i < endpoints_pointer.Get()->num_elements; ++i) {
      EndpointDescriptor* descriptor =
          (endpoints_pointer.Get()->array_storage() + i);
      endpoint_descriptors.push_back(descriptor);
    }

    return endpoint_descriptors;
  }

  void QueuePipe(MessagePipe pipe) { pipes_.push(pipe); }

  int NumberOfPipes() {
    Pointer<ArrayHeader<EndpointDescriptor>>& endpoints_pointer =
        GetMutableMessageHeader().endpoints_in_message;
    return endpoints_pointer.Get() ? endpoints_pointer.Get()->num_elements : 0;
  }

  MessagePipe TakeNextPipe() {
    CHECK(pipes_.size());
    MessagePipe return_pipe = pipes_.front();
    pipes_.pop();
    return return_pipe;
  }

  std::vector<char>& payload_buffer() { return payload_buffer_; }

  int Size() { return GetMutableMessageHeader().size; }

  MessageType Type() { return GetMutableMessageHeader().type; }

 private:
  // Messages can carry a number of message pipes on them, queryable by
  // `NumberOfPipes()`. When a message is received, the pipes are "recovered"
  // and wired up/associated with their backing `Endpoint`s, so that consumers
  // of the message can easily pluck the pipes off of the message and start
  // using them in remotes or receivers. This is where "recovered" pipes are
  // stored, in the order that they appear on the message.
  std::queue<MessagePipe> pipes_;
  std::vector<char> payload_buffer_;
};

template <typename T>
class MessageFragment {
 public:
  MessageFragment(Message& message)
      : message_(message), starting_index_(kInvalidFragmentStartingIndex) {}

  void Allocate() {
    // Cache the starting index of the block of data that we'll allocate. Note
    // that this index is invalid at this moment, but right when the buffer is
    // resized to make room for however many bytes |T| takes up, then this index
    // will point to the first byte of our freshly-allocated block.
    starting_index_ = message_.payload_buffer().size();

    // Allocate enough bytes in the underlying message buffer for T.
    int num_bytes_to_allocate = sizeof(T);
    message_.payload_buffer().resize(message_.payload_buffer().size() +
                                     num_bytes_to_allocate);
  }

  T* data() {
    // Must only be invoked after |Allocate()|.
    return message_.Get<T>(starting_index_);
  }

 private:
  Message& message_;
  int starting_index_;
};

template <typename T>
class MessageFragment<ArrayHeader<T>> {
 public:
  MessageFragment(Message& message)
      : message_(message), starting_index_(kInvalidFragmentStartingIndex) {}

  void AllocateArray(int num_elements) {
    // See comment in |MessageFragment<T>::Allocate()|.
    starting_index_ = message_.payload_buffer().size();

    // Allocate enough bytes for the ArrayHeader + the actual array data.
    int num_bytes_to_allocate =
        sizeof(ArrayHeader<T>) + (sizeof(T) * num_elements);
    message_.payload_buffer().resize(message_.payload_buffer().size() +
                                     num_bytes_to_allocate);

    // Get a pointer to the ArrayHeader<T>* that we just allocated above, and
    // set its |num_elements| member. This writes that number to the underlying
    // message buffer where the |ArrayHeader<T>| is allocated, so we can recover
    // it later.
    ArrayHeader<T>* array_header = data();
    array_header->num_elements = num_elements;
  }

  // Gets a pointer to the first byte of data that we allocated. Note that above
  // we allocated enough bytes for the array header as well as the actual array
  // data, so when we get a pointer to the first byte that we allocated, our
  // intention is to reference the ArrayHeader at that place. If you want to
  // access the actual array *storage* (which starts at the first byte after the
  // ArrayHeader), then you should use |ArrayHeader::array_storage()|.
  ArrayHeader<T>* data() {
    return message_.Get<ArrayHeader<T>>(starting_index_);
  }

 private:
  Message& message_;
  int starting_index_;
};

struct SendInvitationParams {
  char inviter_name[kIdentifierSize];
  // This is the remote node name that the inviter has generated for the target
  // recipient. The target recipient is responsible for generating its own node
  // name though. We tell the target what we've temporarily named it (via this
  // parameter) and then in the invitation acceptance message, it sends its
  // actual name back to us. In order to understand why we even tell the target
  // its temporary name at all, see the comments below in
  // `SendAcceptInvitationParams`.
  char temporary_remote_node_name[kIdentifierSize];
  // When a node receives an invitation, it generates its own name for the
  // endpoint that backs the accept-invitation pipe. But that that endpoint must
  // record its peer as the endpoint in the sender, which is what this parameter
  // captures.
  char intended_endpoint_peer_name[kIdentifierSize];
};

struct SendAcceptInvitationParams {
  // We have to tell the inviter what our *real* name is. However, the inviter
  // may have sent invitations to multiple nodes, so it might not know which one
  // is responding it we just send our *actual* name which it has never seen
  // before. Therefore we identify ourself with the temporary node name that we
  // know the inviter is aware of. It serves as an authentication token for the
  // inviter process, which will take our actual node name and update its map of
  // nodes.
  char temporary_remote_node_name[kIdentifierSize];
  char actual_node_name[kIdentifierSize];
  // The name of the endpoint that the invitation acceptor created when it
  // accepted the invitation. Its peer is
  // `SendInvitationParams::intended_endpoint_peer_name`. The inviter needs this
  // name for the send-invitation pipe's local peer's proxy target.
  char accept_invitation_endpoint_name[kIdentifierSize];
};

};  // namespace mage

#endif  // MAGE_CORE_MESSAGE_H_
