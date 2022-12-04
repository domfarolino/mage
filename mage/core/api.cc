#include "mage/public/api.h"

#include "mage/core/core.h"

////////////////////////////////////////////////////////////////////////////////
// These are the concrete implementation of the public API entrypoints defined
// in `//mage/public/api.h`. They just defer to the `Core` class which is
// responsible for implementing things in an encapsulated way.
////////////////////////////////////////////////////////////////////////////////

namespace mage {

void Init(bool verbose) {
  Core::Init(verbose);
}

void Shutdown() {
  Core::ShutdownCleanly();
}

std::vector<MessagePipe> CreateMessagePipes() {
  return Core::CreateMessagePipes();
}

MessagePipe SendInvitationAndGetMessagePipe(int fd,
                                            base::OnceClosure callback) {
  return Core::SendInvitationAndGetMessagePipe(fd, std::move(callback));
}

void AcceptInvitation(
    int fd,
    std::function<void(MessagePipe)> finished_accepting_invitation_callback) {
  Core::AcceptInvitation(fd, std::move(finished_accepting_invitation_callback));
}

namespace internal {

void SendMessage(MessagePipe local_handle, Message message) {
  Core::SendMessage(local_handle, std::move(message));
}

void BindReceiverDelegateToEndpoint(
    MessagePipe local_handle,
    std::weak_ptr<ReceiverDelegate> delegate,
    std::shared_ptr<base::TaskRunner> delegate_task_runner) {
  Core::BindReceiverDelegateToEndpoint(local_handle, delegate,
                                       delegate_task_runner);
}

void PopulateEndpointDescriptor(
    MessagePipe handle_to_send,
    MessagePipe handle_of_preexisting_connection,
    EndpointDescriptor& endpoint_descriptor_to_populate) {
  return Core::PopulateEndpointDescriptor(handle_to_send,
                                          handle_of_preexisting_connection,
                                          endpoint_descriptor_to_populate);
}

MessagePipe RecoverExistingMessagePipeFromEndpointDescriptor(
    const EndpointDescriptor& endpoint_descriptor) {
  return Core::RecoverExistingMessagePipeFromEndpointDescriptor(
      endpoint_descriptor);
}

MessagePipe RecoverNewMessagePipeFromEndpointDescriptor(
    const EndpointDescriptor& endpoint_descriptor) {
  return Core::RecoverNewMessagePipeFromEndpointDescriptor(endpoint_descriptor);
}

};  // namespace internal

};  // namespace mage
