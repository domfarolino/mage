#include "mage/core/endpoint.h"

#include <unistd.h>

#include "base/scheduling/task_runner.h"
#include "mage/core/core.h"
#include "mage/public/bindings/receiver_delegate.h"

namespace mage {

namespace {
// `Endpoint` occassionally wants to verify whether a `std::weak_ptr` has been
// assigned to some object without asserting anything about the object's
// lifetime. This helper from https://stackoverflow.com/a/45507610/3947332
// enables this.
template <typename T>
bool is_weak_ptr_assigned(std::weak_ptr<T> const& weak) {
  using wt = std::weak_ptr<T>;
  return weak.owner_before(wt{}) || wt{}.owner_before(weak);
}
}  // namespace

// Guarded by `lock_`.
void Endpoint::AcceptMessageOnIOThread(Message message) {
  CHECK(state != State::kUnboundAndProxying);
  LOG("Endpoint::AcceptMessageOnIOThread [this=%p] [pid=%d]", this, getpid());
  LOG("  name: %s", name.c_str());
  LOG("  state: %d", (int)state);
  LOG("  peer_address.node_name: %s", peer_address.node_name.c_str());
  LOG("  peer_address.endpoint_name: %s", peer_address.endpoint_name.c_str());

  AcceptMessage(std::move(message));
}
// Guarded by `lock_`.
void Endpoint::AcceptMessageOnDelegateThread(Message message) {
  CHECK(state != State::kUnboundAndProxying);
  LOG("Endpoint::AcceptMessageOnDelegateThread [this=%p] [pid=%d]", this,
      getpid());
  LOG("  name: %s", name.c_str());
  LOG("  state: %d", (int)state);
  LOG("  peer_address.node_name: %s", peer_address.node_name.c_str());
  LOG("  peer_address.endpoint_name: %s", peer_address.endpoint_name.c_str());

  // Process and register all of the endpoints that `message` is carrying before
  // we either queue or dispatch it.
  std::vector<EndpointDescriptor*> endpoints_in_message =
      message.GetEndpointDescriptors();
  LOG("  endpoints_in_message.size()= %lu", endpoints_in_message.size());
  for (const EndpointDescriptor* const endpoint_descriptor :
       endpoints_in_message) {
    MessagePipe local_pipe =
        mage::Core::RecoverExistingMessagePipeFromEndpointDescriptor(
            *endpoint_descriptor);
    endpoint_descriptor->Print();
    LOG("     Queueing pipe to message after recovering endpoint");
    message.QueuePipe(local_pipe);
  }

  AcceptMessage(std::move(message));
}

// Guarded by `lock_`.
void Endpoint::AcceptMessage(Message message) {
  CHECK(state != State::kUnboundAndProxying);
  LOG("Endpoint::AcceptMessage() [this=%p], [pid=%d]", this, getpid());
  LOG("  name: %s", name.c_str());
  LOG("  state: %d", (int)state);
  LOG("  peer_address.node_name: %s", peer_address.node_name.c_str());
  LOG("  peer_address.endpoint_name: %s", peer_address.endpoint_name.c_str());
  LOG("  number_of_pipes: %d", message.NumberOfPipes());

  switch (state) {
    case State::kUnboundAndQueueing:
      CHECK(!is_weak_ptr_assigned(delegate_));
      LOG("  Endpoint is queueing a message to `incoming_message_queue_`");
      incoming_message_queue_.push(std::move(message));
      break;
    case State::kBound:
      CHECK(is_weak_ptr_assigned(delegate_));
      LOG("  Endpoint has accepted a message. Now forwarding it to `delegate_` "
          "on the delegate's task runner");
      PostMessageToDelegate(std::move(message));
      break;
    case State::kUnboundAndProxying:
      // `Endpoint` is never responsible for handling messages when it is in the
      // proxying state. That should be handled at the layer above us.
      NOTREACHED();
      break;
  }
  LOG("Endpoint::AcceptMessage() DONE");
}

// Guarded by `lock_`.
void Endpoint::PostMessageToDelegate(Message message) {
  CHECK_EQ(state, State::kBound);
  CHECK(is_weak_ptr_assigned(delegate_));
  CHECK(delegate_task_runner_);
  // We should consider whether or not we need to be unconditionally posting a
  // task here. If this method is already running on the TaskLoop/thread that
  // `delegate_` is bound to, do we need to post a task at all? It depends on
  // the async semantics that we're going for. We should also test this.
  delegate_task_runner_->PostTask(
      base::BindOnce(&ReceiverDelegate::DispatchMessageIfStillAlive, delegate_,
                     std::move(message)));
}

std::queue<Message> Endpoint::TakeQueuedMessages() {
  CHECK(!is_weak_ptr_assigned(delegate_));
  std::queue<Message> messages_to_return = std::move(incoming_message_queue_);
  return messages_to_return;
}

void Endpoint::RegisterDelegate(
    std::weak_ptr<ReceiverDelegate> delegate,
    std::shared_ptr<base::TaskRunner> delegate_task_runner) {
  // Before we observe our state and incoming message queue, we need to grab a
  // lock in case another thread is modifying this state.
  Lock();

  LOG("Endpoint::RegisterDelegate() [this=%p] [getpid=%d]", this, getpid());
  LOG("state: %d", (int)state);
  CHECK_EQ(state, State::kUnboundAndQueueing);
  state = State::kBound;
  // LOG("RegisterDelegate() just set state = kBound");
  CHECK(!is_weak_ptr_assigned(delegate_));
  delegate_ = delegate;
  CHECK(!delegate_task_runner_);
  delegate_task_runner_ = delegate_task_runner;

  LOG("  Endpoint::RegisterDelegate() seeing if we have queued messages to "
      "deliver");
  // We may have messages queued up for our `delegate_` already .
  while (!incoming_message_queue_.empty()) {
    LOG("    >> Found a message; calling PostMessageToDelegate() to forward "
        "the queued message to delegate");
    PostMessageToDelegate(std::move(incoming_message_queue_.front()));
    incoming_message_queue_.pop();
  }
  LOG("  Endpoint::RegisterDelegate() done delivering messages");
  Unlock();
}

// Not implemented. See header documentation.
void Endpoint::UnregisterDelegate() {
  NOTREACHED();
  CHECK_EQ(state, State::kBound);
  state = State::kUnboundAndQueueing;
  CHECK(is_weak_ptr_assigned(delegate_));
  CHECK(delegate_task_runner_);
}

void Endpoint::SetProxying(std::string in_node_name,
                           std::string in_endpoint_name) {
  CHECK_EQ(state, State::kUnboundAndQueueing);
  state = State::kUnboundAndProxying;
  proxy_target.node_name = in_node_name;
  proxy_target.endpoint_name = in_endpoint_name;
  LOG("Endpoint::SetProxying() proxy_target: (%s, %s):",
      proxy_target.node_name.c_str(), proxy_target.endpoint_name.c_str());
}

};  // namespace mage
