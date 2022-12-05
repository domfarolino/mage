#ifndef MAGE_PUBLIC_API_H_
#define MAGE_PUBLIC_API_H_

#include <map>
#include <memory>

#include "base/callback.h"
#include "mage/public/bindings/message_pipe.h"
#include "mage/public/message.h"

namespace base {
class TaskRunner;
};  // namespace base

namespace mage {

struct EndpointDescriptor;
class ReceiverDelegate;

// Main public APIs:

// Relies on the IO thread's TaskLoop being synchronously accessible from the
// UI thread.
void Init(bool verbose = false);
void Shutdown();

std::vector<MessagePipe> CreateMessagePipes();
MessagePipe SendInvitationAndGetMessagePipe(
    int fd,
    base::OnceClosure callback = base::OnceClosure());
void AcceptInvitation(
    int fd,
    std::function<void(MessagePipe)> finished_accepting_invitation_callback);

// These are public APIs that should only be used by mage code, some of which is
// generated *by* mage but lives in mage embedders (i.e., magen
// interface-generated code).
namespace internal {

void SendMessage(MessagePipe local_handle, Message message);
void BindReceiverDelegateToEndpoint(
    MessagePipe local_handle,
    std::weak_ptr<ReceiverDelegate> delegate,
    std::shared_ptr<base::TaskRunner> delegate_task_runner);

// More obscure helpers.

// `handle_to_send` is about to be sent over an existing connection described
// by `handle_of_preexisting_connection`. This function fills out
// `endpoint_descriptor_to_populate`, which includes generating a new
// `EndpointDescriptor::cross_node_endpoint_name` just in case
// `handle_to_send` goes cross-node/process. If it does, `Node::SendMessage()`
// will put the backing endpoint into the proxying state re-route this message
// accordingly, and flush any queued messages from the backing endoint.
// This happens if `handle_of_preexisting_endpoint` has a remote peer, or a
// local peer that is proxying.
void PopulateEndpointDescriptor(
    MessagePipe handle_to_send,
    MessagePipe handle_of_preexisting_connection,
    EndpointDescriptor& endpoint_descriptor_to_populate);
MessagePipe RecoverExistingMessagePipeFromEndpointDescriptor(
    const EndpointDescriptor& endpoint_descriptor);
MessagePipe RecoverNewMessagePipeFromEndpointDescriptor(
    const EndpointDescriptor& endpoint_descriptor);

};  // namespace internal

};  // namespace mage

#endif  // MAGE_PUBLIC_API_H_
