#include "mage/core/node.h"

#include <memory>

#include "base/check.h"
#include "base/threading/thread_checker.h"  // for CHECK_ON_THREAD().
#include "mage/core/core.h"
#include "mage/core/endpoint.h"
#include "mage/public/message.h"

namespace mage {

std::pair<std::shared_ptr<Endpoint>, std::shared_ptr<Endpoint>>
Node::InitializeAndEntangleEndpoints() const {
  std::shared_ptr<Endpoint> ep1(
      new Endpoint(/*name=*/util::RandomIdentifier()));
  std::shared_ptr<Endpoint> ep2(
      new Endpoint(/*name=*/util::RandomIdentifier()));

  // Both endpoints are local initially.
  ep1->peer_address.node_name = name_;
  ep2->peer_address.node_name = name_;

  // Entangle endpoints.
  ep1->peer_address.endpoint_name = ep2->name;
  ep2->peer_address.endpoint_name = ep1->name;

  LOG("Initialized and entangled the following endpoints:\n"
      "  endpoint1: (%s, %s)\n"
      "  endpoint2: (%s, %s)",
      name_.c_str(), ep1->name.c_str(), name_.c_str(), ep2->name.c_str());

  return std::make_pair(ep1, ep2);
}

void Node::RegisterEndpoint(std::shared_ptr<Endpoint> new_endpoint) {
  // Make sure the endpoint isn't already registered.
  local_endpoints_lock_.lock();
  auto it = local_endpoints_.find(new_endpoint->name);
  CHECK_EQ(it, local_endpoints_.end());

  local_endpoints_.insert({new_endpoint->name, new_endpoint});
  local_endpoints_lock_.unlock();
}

std::vector<MessagePipe> Node::CreateMessagePipes() {
  std::vector<std::pair<MessagePipe, std::shared_ptr<Endpoint>>>
      pipes_and_endpoints = Node::CreateMessagePipesAndGetEndpoints();
  return {pipes_and_endpoints[0].first, pipes_and_endpoints[1].first};
}

std::vector<std::pair<MessagePipe, std::shared_ptr<Endpoint>>>
Node::CreateMessagePipesAndGetEndpoints() {
  // Thread-safely generate two fresh endpoints.
  const auto& [endpoint_1, endpoint_2] = InitializeAndEntangleEndpoints();
  // Independently, thread-safely generate them a unique `MessagePipe` handle.
  MessagePipe handle_1 = Core::Get()->GetNextMessagePipe(),
              handle_2 = Core::Get()->GetNextMessagePipe();

  // Now simply register/associate the endpoints/message pipes. This is also
  // thread-safe.
  Core::Get()->RegisterLocalHandleAndEndpoint(handle_1, endpoint_1);
  Core::Get()->RegisterLocalHandleAndEndpoint(handle_2, endpoint_2);

  // Ensure that the endpoints have been inserted.
  local_endpoints_lock_.lock();
  CHECK_NE(local_endpoints_.find(endpoint_1->name), local_endpoints_.end());
  CHECK_NE(local_endpoints_.find(endpoint_2->name), local_endpoints_.end());
  local_endpoints_lock_.unlock();
  return {std::make_pair(handle_1, endpoint_1),
          std::make_pair(handle_2, endpoint_2)};
}

MessagePipe Node::SendInvitationAndGetMessagePipe(int fd) {
  // This node wishes to invite another fresh peer node to the network of
  // processes. The sequence of events here looks like so:
  //   Endpoints:
  //     1.) Create two new entangled endpoints.
  //     2.) Insert both endpoints in the |local_endpoints_| map since both
  //         endpoints are technically local to |this| at this point. Eventually
  //         only one endpoint, |local_endpoint| below will be local to |this|
  //         while |remote_endpoint| will eventually be replaced with an
  //         endpoint with the same name as |remote_endpoint| on the target
  //         node.
  //     3.) Insert |remote_endpoint| into |reserved_endpoints| which maps
  //         temporary node names to endpoints that will eventually be replaced
  //         with a truly remote endpoint.
  //   Nodes:
  //     1.) Create a new |Channel| for the node whose name is
  //         |temporary_remote_node_name| (its name will change later when it
  //         tells us its real name).
  //     2.) Start the channel (listen for any messages from the remote node,
  //         though we don't expect any just yet).
  //     3.) Send the |MessageType::SEND_INVITATION| message to the remote node.
  //     4.) Store the channel in |node_channel_map_| so that we can reference
  //         it whenever we send a message from an endpoint that is bound for
  //         the remote node.
  //     5.) Insert the temporary remote node name in the |pending_invitations_|
  //         set. When we receive invitation acceptance later on, we want to
  //         make sure we know which node we're receiving the acceptance from.
  //         We use this set to keep track of our pending invitations that we
  //         expect acceptances for. Later we'll update all instances of
  //         |temporary_remote_node_name| to the actual remote node name that it
  //         makes us aware of as a part of invitation acceptance.
  //   MessagePipes:
  //     1.) Return the |MessagePipe| assocaited with |local_endpoint| so that
  //         this process can start immediately queueing messages on
  //         |local_endpoint| that will eventually be delivered to the remote
  //         process.

  std::vector<std::pair<mage::MessagePipe, std::shared_ptr<Endpoint>>> pipes =
      CreateMessagePipesAndGetEndpoints();
  std::shared_ptr<Endpoint> local_endpoint = pipes[0].second,
                            remote_endpoint = pipes[1].second;

  NodeName temporary_remote_node_name = util::RandomIdentifier();

  // Lock this map because it could be accessed from many threads, since the
  // embedder can choose to send invitations from any thread.
  node_channel_map_lock_.lock();
  auto it = node_channel_map_.insert(
      {temporary_remote_node_name,
       std::make_unique<Channel>(fd, /*delegate=*/this)});
  node_channel_map_lock_.unlock();
  pending_invitations_lock_.lock();
  pending_invitations_.insert({temporary_remote_node_name, remote_endpoint});
  pending_invitations_lock_.unlock();

  // Similar to `AcceptInvitation()` below, only start the channel after it and
  // `remote_endpoint` have been inserted into their maps. Right when we the
  // channel starts it can receive message on the IO thread, which might include
  // the accept invitation message, which on the IO thread checks the
  // `pending_invitations_` map, which of course better contain the pending
  // invitation that some other node apparently accepted. We could also lock the
  // map accordingly and not have to worry about ordering, but that is more
  // expensive, and we can avoid it with one-time proper ordering.
  std::unique_ptr<Channel>& channel = it.first->second;
  channel->Start();
  channel->SendInvitation(/*inviter_name=*/name_,
                          /*temporary_remote_node_name=*/temporary_remote_node_name,
                          /*intended_endpoint_peer_name=*/
                          remote_endpoint->peer_address.endpoint_name);

  MessagePipe local_endpoint_handle = pipes[0].first;
  return local_endpoint_handle;
}

void Node::AcceptInvitation(int fd) {
  CHECK(!has_accepted_invitation_);

  LOG("Node::AcceptInvitation() getpid: %d", getpid());
  std::unique_ptr<Channel> channel(new Channel(fd, this));
  // Thread-safety: We don't need to take a lock on this map here because in
  // general, nodes that accept invitations should not be sending them too.
  // Doing so is technically possible for now, but is considered "unsupported",
  // and therefore we don't go out of our way to make it safe. See the
  // corresponding documentation in `Node::OnReceivedInvitation()`, which gets
  // invoked asynchronously after we call `Channel::Start()` below.
  auto it = node_channel_map_.insert({kInitialChannelName, std::move(channel)});

  // Start the channel *after* it is inserted into the map, because right when
  // it starts, messages could start coming in and being read from any other
  // thread. This is because `Channel::Start()` defers to the embedder-supplied
  // IO mechanism for receiving messages on `fd`, and that mechanism might
  // always listen for (and read) messages on whatever thread that IO mechanism
  // is bound to, which may be different than *this* thread.
  //
  // For example, if the embedder is using the `//base` library [1], there's
  // only ever a single "IO" thread capable of socket communication at any given
  // time in a process. If this method is run on any other thread than the IO
  // thread, then right when `Channel::Start()` is invoked on *this* thread,
  // incoming messages like `OnReceivedInvitation()` could start coming in on
  // the IO thread at any time, and expect `channel` to exist in
  // `node_channel_map_` under the `kInitialChannelName` name.
  //
  // [1]: https://github.com/domfarolino/base.
  it.first->second->Start();

  has_accepted_invitation_ = true;
}

void Node::SendMessage(std::shared_ptr<Endpoint> local_endpoint,
                       Message message) {
  LOG("Node::SendMessage() [pid=%d]", getpid());
  CHECK_EQ(message.GetMutableMessageHeader().type, MessageType::USER_MESSAGE);

  // Sanity check that no endpoints we're sending are bound. Note that this
  // check only catches endpoints that are bound to receivers, since `Endpoint`s
  // don't track whether they are bound to remotes.
  //
  // This would be a good thing to gate behind a "debug-only" build check, so
  // that we don't waste the time it takes to do this in "production".
  for (const EndpointDescriptor* const descriptor :
       message.GetEndpointDescriptors()) {
    std::string endpoint_name(descriptor->endpoint_name, kIdentifierSize);
    auto it = local_endpoints_.find(endpoint_name);
    std::shared_ptr<Endpoint> endpoint = it->second;
    CHECK(endpoint);
    endpoint->Lock();
    CHECK_NE(endpoint->state, Endpoint::State::kBound);
    endpoint->Unlock();
  }

  // If we're sending a message, one of the following, but not both, must be
  // true:
  //   1.) The peer endpoint is already in the remote node, in which case we can
  //       access the remote endpoint via the Channel in |node_channel_map_|.
  //   2.) The peer endpoint is local, held in |pending_invitations_| because we
  //       have not yet received the accept invitation message yet.
  std::string peer_node_name = local_endpoint->peer_address.node_name;
  std::string peer_endpoint_name = local_endpoint->peer_address.endpoint_name;
  auto endpoint_it = local_endpoints_.find(peer_endpoint_name);
  auto channel_it = node_channel_map_.find(peer_node_name);

  // The peer endpoint can either be local or remote, but it can't be both.
  CHECK_NE((endpoint_it == local_endpoints_.end()),
           (channel_it == node_channel_map_.end()));

  // We have to write the `peer_endpoint_name` to the message, so that the
  // ultimate recipient can dispatch it correctly.
  CHECK_EQ(peer_endpoint_name.size(), kIdentifierSize);
  memcpy(message.GetMutableMessageHeader().target_endpoint,
         peer_endpoint_name.c_str(), kIdentifierSize);

  bool peer_is_local = (endpoint_it != local_endpoints_.end());
  LOG(" peer_is_local: %d, [getpid=%d]", peer_is_local, getpid());
  if (!peer_is_local) {
    std::queue<Message> messages_to_send;
    messages_to_send.push(std::move(message));
    SendMessagesAndRecursiveDependents(std::move(messages_to_send),
                                       peer_node_name);
    return;
  }

  std::shared_ptr<Endpoint> local_peer_endpoint = endpoint_it->second;
  CHECK(local_peer_endpoint);

  // Lock `local_peer_endpoint` so that all of the actions we perform based on
  // `state` are consistent with the `state` that other threads are trying to
  // read at the same time.
  local_peer_endpoint->Lock();

  switch (local_peer_endpoint->state) {
    case Endpoint::State::kUnboundAndProxying: {
      std::string actual_node_name =
          local_peer_endpoint->proxy_target.node_name;
      std::string actual_endpoint_name =
          local_peer_endpoint->proxy_target.endpoint_name;
      LOG("  local_peer is in proxying state. forwarding message to remote "
          "endpoint (%s : %s)",
          actual_node_name.c_str(), actual_endpoint_name.c_str());

      // If we know that this message is supposed to be proxied to another node,
      // we have to rewrite its target endpoint to be the proxy target. We do
      // the same for dependent endpoint descriptors in
      // `SendMessagesAndRecursiveDependents()`.
      memcpy(message.GetMutableMessageHeader().target_endpoint,
             actual_endpoint_name.c_str(), kIdentifierSize);

      std::queue<Message> messages_to_send;
      messages_to_send.push(std::move(message));
      SendMessagesAndRecursiveDependents(
          std::move(messages_to_send),
          local_peer_endpoint->proxy_target.node_name);
      break;
    }
    case Endpoint::State::kBound:
    case Endpoint::State::kUnboundAndQueueing:
      LOG("  local_peer is not proxying state, going to deliver the message "
          "right there");
      // We can just pass this single message to the peer without recursively
      // looking at dependent messages. That's because if we *did* recursively
      // look through all of the dependent messages and try and forward them,
      // we'd just be forwarding them to *their* local peers in the same node,
      // which is where those messages already are.
      local_peer_endpoint->AcceptMessageOnDelegateThread(std::move(message));
      break;
  }

  local_peer_endpoint->Unlock();
}

void Node::OnReceivedMessage(Message message) {
  switch (message.Type()) {
    case MessageType::SEND_INVITATION:
      OnReceivedInvitation(std::move(message));
      return;
    case MessageType::ACCEPT_INVITATION:
      OnReceivedAcceptInvitation(std::move(message));
      return;
    case MessageType::USER_MESSAGE:
      OnReceivedUserMessage(std::move(message));
      return;
  }

  NOTREACHED();
}

void Node::OnReceivedInvitation(Message message) {
  SendInvitationParams* params = message.GetView<SendInvitationParams>();

  // Deserialize
  std::string inviter_name(params->inviter_name,
                           params->inviter_name + kIdentifierSize);
  std::string temporary_remote_node_name(
      params->temporary_remote_node_name,
      params->temporary_remote_node_name + kIdentifierSize);
  std::string intended_endpoint_peer_name(
      params->intended_endpoint_peer_name,
      params->intended_endpoint_peer_name + kIdentifierSize);

  LOG("Node::OnReceivedInvitation getpid(): %d", getpid());
  LOG("  inviter_name:                %s", inviter_name.c_str());
  LOG("  temporary_remote_node_name:  %s", temporary_remote_node_name.c_str());
  LOG("  intended_endpoint_peer_name: %s", intended_endpoint_peer_name.c_str());

  // Now that we know our inviter's name, we can find our initial channel in our
  // map, and change the entry's key to the actual inviter's name.
  //
  // Thread-safety: We don't need to take a lock over this map. That's because a
  // node can only accept a single invitation throughout its life, and
  // invitation-accepting nodes shouldn't be able to *send* invitations (see the
  // documentation in `Node::AcceptInvitation()`). This is safe because at this
  // point, `this` can't know about any other nodes/channels yet, so this map is
  // essentially static at this point.
  auto it = node_channel_map_.find(kInitialChannelName);
  CHECK_NE(it, node_channel_map_.end());
  std::unique_ptr<Channel> init_channel = std::move(it->second);
  node_channel_map_.erase(kInitialChannelName);
  node_channel_map_.insert({inviter_name, std::move(init_channel)});

  // We can also create a new local |Endpoint|, and wire it up to point to its
  // peer that we just learned about from the inviter's message.
  // Choose a random name for this endpoint. We send this name back to the
  // inviter when we accept the invitation.
  std::shared_ptr<Endpoint> local_endpoint(
      new Endpoint(/*name=*/util::RandomIdentifier()));
  local_endpoint->peer_address.node_name = inviter_name;
  local_endpoint->peer_address.endpoint_name = intended_endpoint_peer_name;
  local_endpoints_.insert({local_endpoint->name, local_endpoint});
  LOG("  local_endpoint->name: %s", local_endpoint->name.c_str());
  LOG("  local_endpoint->peer_address.node_name: %s",
      local_endpoint->peer_address.node_name.c_str());
  LOG("  local_endpoint->peer_address.endpoint_name: %s",
      local_endpoint->peer_address.endpoint_name.c_str());

  node_channel_map_[inviter_name]->SendAcceptInvitation(
      temporary_remote_node_name, name_, local_endpoint->name);

  // This must come after we send the invitation acceptance above. This is
  // because the following call might immediately start sending messages to the
  // remote node, but it shouldn't receive any messages from us until it knows
  // we accepted its invitation.
  Core::Get()->OnReceivedInvitation(local_endpoint);
}

void Node::OnReceivedAcceptInvitation(Message message) {
  SendAcceptInvitationParams* params =
      message.GetView<SendAcceptInvitationParams>();
  std::string temporary_remote_node_name(
      params->temporary_remote_node_name,
      params->temporary_remote_node_name + kIdentifierSize);
  std::string actual_node_name(params->actual_node_name,
                               params->actual_node_name + kIdentifierSize);
  std::string accept_invitation_endpoint_name(
      params->accept_invitation_endpoint_name,
      params->accept_invitation_endpoint_name + kIdentifierSize);

  LOG("Node::OnReceivedAcceptInvitation [getpid(): %d] from: %s (actually %s)",
      getpid(), temporary_remote_node_name.c_str(), actual_node_name.c_str());

  // We should only get ACCEPT_INVITATION messages from nodes that we have a
  // pending invitation for.
  //
  // In order to acknowledge the invitation acceptance, we must do four things:
  //   1.) Remove the pending invitation from |pending_invitations_|.
  pending_invitations_lock_.lock();
  auto remote_endpoint_it =
      pending_invitations_.find(temporary_remote_node_name);
  CHECK_NE(remote_endpoint_it, pending_invitations_.end());
  std::shared_ptr<Endpoint> remote_endpoint = remote_endpoint_it->second;
  pending_invitations_.erase(temporary_remote_node_name);
  pending_invitations_lock_.unlock();

  //   2.) Put |remote_endpoint| in the `kUnboundAndProxying` state, so that
  //       when `SendMessage()` gets message bound for it, it knows to forward
  //       them to the appropriate remote node.
  CHECK_NE(local_endpoints_.find(remote_endpoint->name),
           local_endpoints_.end());

  // Lock `remote_endpoint` here. This is important because we're going to put
  // it into a proxying mode and immediately flush all of its messages to its
  // new proxy target, and we can't do this while another thread is possibly
  // trying to queue messages onto the endpoint.
  remote_endpoint->Lock();
  remote_endpoint->SetProxying(
      /*in_node_name=*/actual_node_name,
      /*in_endpoint_name=*/accept_invitation_endpoint_name);

  LOG("  Our `remote_endpoint` now recognizes its proxy target as: (%s:%s)",
      remote_endpoint->proxy_target.node_name.c_str(),
      remote_endpoint->proxy_target.endpoint_name.c_str());

  //   3.) Update |node_channel_map_| to correctly be keyed off of
  //       |actual_node_name|.
  //
  //       Lock the node channel map because in the path of acknowledging an
  //       accepted invitation, other threads could be sending other invitations
  //       and mutating the same map.
  node_channel_map_lock_.lock();
  auto node_channel_it = node_channel_map_.find(temporary_remote_node_name);
  CHECK_NE(node_channel_it, node_channel_map_.end());
  std::unique_ptr<Channel> channel = std::move(node_channel_it->second);
  node_channel_map_.erase(temporary_remote_node_name);
  node_channel_map_.insert({actual_node_name, std::move(channel)});
  node_channel_map_lock_.unlock();

  //   4.) Forward any messages that were queued in |remote_endpoint| so that
  //       the remote node's endpoint gets them. Note that the messages queued
  //       in `remote_endpoint` might be carrying `EndpointDescriptor`s, each of
  //       which we have to examine so we can:
  //         1.) Take all of the queued messages in the endpoint described by
  //             the info, and forward them to the remote node (is it guaranteed
  //             to be remote?)
  //         2.) Set the endpoint described by the info to the proxying mode (or
  //             maybe just delete it? Figure this out....)
  std::queue<Message> messages_to_forward =
      remote_endpoint->TakeQueuedMessages();
  LOG("    Node has %lu messages queued up in the remote invitation endpoint",
      messages_to_forward.size());

  // TODO(domfarolino): This is a stupid artifact of the fact that we let the
  // recipient of the invitation choose its own name. Ultimately we should not
  // do this, perhaps for security reasons, but also to simplify this code. We
  // should choose the intended endpoint name for the primordial endpoint in the
  // remote process, and just make it a random name. That way we can avoid this
  // update.
  //
  // All of these messages were written with the `target_endpoint` as
  // `remote_endpoint->name`. But since `remote_endpoint` is now proxying to a
  // different-named endpoint in the remote process (that the remote process
  // generated for itself), we must re-target the queued messages and all
  // dependent messages. In this loop, we re-target all of the top-level
  // messages; dependent messages are handled in
  // `SendMessagesAndRecursiveDependents()`.
  std::queue<Message> final_messages_to_forward;
  while (!messages_to_forward.empty()) {
    Message message_to_forward = std::move(messages_to_forward.front());
    memcpy(message_to_forward.GetMutableMessageHeader().target_endpoint,
           remote_endpoint->proxy_target.endpoint_name.c_str(),
           kIdentifierSize);
    final_messages_to_forward.push(std::move(message_to_forward));
    messages_to_forward.pop();
  }

  SendMessagesAndRecursiveDependents(std::move(final_messages_to_forward),
                                     remote_endpoint->proxy_target.node_name);
  remote_endpoint->Unlock();
  Core::Get()->OnReceivedAcceptInvitation();
}

void Node::SendMessagesAndRecursiveDependents(
    std::queue<Message> messages_to_send,
    std::string target_node_name) {
  // All messages sent from this method are bound for the same node
  // `target_node_name`, but each is going to a cross-node endpoint whose name
  // is `EndpointDescriptor::cross_node_endpoint_name`. It's this name that
  // we'll:
  //   1.) Set `MessageHeader::target_endpoint` to
  //   2.) Set the backing endpoint's proxy target's endpoint name to

  while (!messages_to_send.empty()) {
    Message message_to_send = std::move(messages_to_send.front());

    // Push possibly many more messages to `message_to_send`.
    LOG("      Forwarding a message NumberOfPipes(): %d",
        message_to_send.NumberOfPipes());
    std::vector<EndpointDescriptor*> descriptors =
        message_to_send.GetEndpointDescriptors();

    // As we process each dependent endpoint of `message_to_send`, we have to
    // lock them. The meat of what we do below is:
    //   1.) Set each dependent endpoint into the proxying state
    //   2.) After all are into the proxying state, send `message_to_send`,
    //       which is the message that each endpoint is attached to / inside of
    // We must only unlock each endpoint *after* `message_to_send` is sent. If
    // we unlock each endpoint after they go into the proxying state, but before
    // the message-they-are-attached-to is sent, then a new message could be
    // sent via the dependent endpoints (to the proxy target node) before
    // `message_to_send` gets a chance to introduce the new target node to each
    // endpoint it's transporting. In that case, the target node would blow up
    // because it received a message for an endpoint that it's not aware of yet.
    std::vector<std::shared_ptr<Endpoint>> locked_dependent_endpoints;

    for (const EndpointDescriptor* const descriptor : descriptors) {
      std::string endpoint_name(descriptor->endpoint_name, kIdentifierSize);
      std::string cross_node_endpoint_name(descriptor->cross_node_endpoint_name,
                                           kIdentifierSize);
      LOG("        An EndpointDescriptor in this message:");
      descriptor->Print();

      auto it = local_endpoints_.find(endpoint_name);
      CHECK_NE(it, local_endpoints_.end());

      std::shared_ptr<Endpoint> endpoint_from_info = it->second;
      endpoint_from_info->Lock();
      // So we can remember to unlock these after we send the message bearing
      // this endpoint.
      locked_dependent_endpoints.push_back(endpoint_from_info);

      std::queue<Message> sub_messages =
          endpoint_from_info->TakeQueuedMessages();
      while (!sub_messages.empty()) {
        Message sub_message = std::move(sub_messages.front());
        // See `Core::PopulateEndpointDescriptor()`; when an
        // `EndpointDescriptor` gets populated, it gets a new name
        // (`cross_node_endpoint_name`) that's used to create a "remote"
        // representation of the local endpoint in the event it gets sent to
        // another node/process. At this point we know `endpoint_from_info` is
        // going to another process, so we have to take all of the messages
        // queued on it and change their target endpoint names to the name that
        // `endpoint_from_info` will have in the remote node.
        memcpy(sub_message.GetMutableMessageHeader().target_endpoint,
               cross_node_endpoint_name.c_str(), kIdentifierSize);
        messages_to_send.push(std::move(sub_message));
        sub_messages.pop();
      }

      // We know that the real endpoint was sent to `target_node_name`. By now,
      // it is possible that endpoint was sent yet again to another process, and
      // so on. Its ultimate location doesn't matter to us. We know the next
      // place it went was `target_node_name` which is either the ultimate
      // destination, or the node with the next closest proxy. We'll send the
      // message to that node and let it figure out what to do from there.
      endpoint_from_info->SetProxying(
          /*in_node_name=*/target_node_name,
          /*in_endpoint_name=*/cross_node_endpoint_name);
    }

    // Forward the message and remove it from the queue.
    node_channel_map_lock_.lock();
    node_channel_map_[target_node_name]->SendMessage(
        std::move(message_to_send));
    node_channel_map_lock_.unlock();

    // See the documentation above `locked_dependent_endpoints`.
    for (const std::shared_ptr<Endpoint>& endpoint : locked_dependent_endpoints)
      endpoint->Unlock();

    messages_to_send.pop();
  }
}

void Node::OnReceivedUserMessage(Message message) {
  CHECK_ON_THREAD(base::ThreadType::IO);
  LOG("Node::OnReceivedUserMessage getpid(): %d", getpid());
  // 1. Extract the endpoint that the message is bound for.
  char* target_endpoint_buffer =
      message.GetMutableMessageHeader().target_endpoint;
  std::string local_target_endpoint_name(
      target_endpoint_buffer, target_endpoint_buffer + kIdentifierSize);
  LOG("    OnReceivedUserMessage() looking for local_target_endpoint_name: %s "
      "to dispatch message to",
      local_target_endpoint_name.c_str());
  auto endpoint_it = local_endpoints_.find(local_target_endpoint_name);
  CHECK_NE(endpoint_it, local_endpoints_.end());

  std::shared_ptr<Endpoint> endpoint = endpoint_it->second;
  CHECK(endpoint);
  CHECK_EQ(local_target_endpoint_name, endpoint->name);
  endpoint->Lock();

  // Process and register all of the endpoints that `message` is carrying before
  // we either queue or dispatch it.
  std::vector<EndpointDescriptor*> endpoints_in_message =
      message.GetEndpointDescriptors();
  LOG("  endpoints_in_message.size()= %lu", endpoints_in_message.size());
  for (const EndpointDescriptor* const endpoint_descriptor :
       endpoints_in_message) {
    MessagePipe local_pipe =
        Core::RecoverNewMessagePipeFromEndpointDescriptor(*endpoint_descriptor);
    endpoint_descriptor->Print();
    LOG("     Queueing pipe to message after recovering new endpoint");
    message.QueuePipe(local_pipe);
  }

  // 2. Tell the endpoint to handle the message.
  switch (endpoint->state) {
    case Endpoint::State::kUnboundAndProxying: {
      Address& proxy_target = endpoint->proxy_target;
      CHECK_NE(proxy_target.node_name, name_);

      LOG("  Node::OnReceivedUserMessage() received a message when in the "
          "proxying state. Forwarding message to proxy_target=(%s : %s)",
          proxy_target.node_name.c_str(), proxy_target.endpoint_name.c_str());
      memcpy(message.GetMutableMessageHeader().target_endpoint,
             proxy_target.endpoint_name.c_str(), kIdentifierSize);
      PrepareToForwardUserMessage(endpoint, message);
      // Lock the node channel map because the thread we receive a message a
      // message on (and therefore forward messages to another node in this
      // proxying case) could be different than the arbitrary threads that might
      // be sending invitations to other processes.
      node_channel_map_lock_.lock();
      node_channel_map_[endpoint->proxy_target.node_name]->SendMessage(
          std::move(message));
      node_channel_map_lock_.unlock();
      break;
    }
    case Endpoint::State::kBound:
    case Endpoint::State::kUnboundAndQueueing:
      endpoint->AcceptMessageOnIOThread(std::move(message));
      break;
  }
  endpoint->Unlock();
}

void Node::PrepareToForwardUserMessage(std::shared_ptr<Endpoint> endpoint,
                                       Message& message) {
  CHECK_ON_THREAD(base::ThreadType::IO);
  CHECK_EQ(endpoint->state, Endpoint::State::kUnboundAndProxying);

  std::vector<EndpointDescriptor*> descriptors_to_forward =
      message.GetEndpointDescriptors();
  for (EndpointDescriptor* descriptor : descriptors_to_forward) {
    std::string endpoint_name(descriptor->cross_node_endpoint_name,
                              kIdentifierSize);
    LOG("Looking for a local endpoint by the name of: %s to put into a "
        "proxying state",
        endpoint_name.c_str());
    // Endpoints being sent in this message should be "recovered" by
    // `Node::OnReceivedUserMessage()`.
    auto it = local_endpoints_.find(endpoint_name);
    CHECK_NE(it, local_endpoints_.end());
    std::shared_ptr<Endpoint> backing_endpoint = it->second;

    // At this point, per the above `CHECK_NE`, we know this message and its
    // dependent endpoints (`descriptors_to_forward`) are being
    // forwarded/proxied to another node. This means that we cannot just send
    // the descriptor *as-is* untouched, to the proxy target. If we did this,
    // the endpoint that gets recovered from `message` in the remote node will
    // have the same `endpoint_name` & `cross_node_endpoint_name` as the one
    // that got recovered inside this process does, which breaks our invariant.
    // This is a problem because if that same endpoint got proxied *back* to
    // this node, it would try and "recover" the endpoint with the same name as
    // one that already exists in this node, and we'd crash.
    //
    // So in order to properly forward dependent endpoints, we must:
    //   1.) Regenerate a new `cross_node_endpoint_name`
    std::string new_cross_node_endpoint_name = util::RandomIdentifier();
    //   2.) Change the descriptor *as it lives on the message* (not a copy) to
    //       inchworm forward in the proxying process:
    //       `endpoint_name = cross_node_endpoint_name`
    memcpy(descriptor->endpoint_name, descriptor->cross_node_endpoint_name,
           kIdentifierSize);
    //   3.) Inchworm the `cross_node_endpoint_name` forwrad too, with the new
    //       one we generated:
    //       `cross_node_endpoint_name = new_cross_node_endpoint_name`
    memcpy(descriptor->cross_node_endpoint_name,
           new_cross_node_endpoint_name.c_str(), kIdentifierSize);
    //   3.) Set `backing_endpoint` to proxy to the *new*
    //       `cross_node_endpoint_name` since that will be the name of this
    //       endpoint in the proxy target node:
    //
    // We don't need to acquire a lock here, since no other thread should know
    // about these dependent endpoints inside `message`, since they were just
    // created during this flow and not exported anywhere.
    backing_endpoint->SetProxying(
        /*in_node_name=*/endpoint->proxy_target.node_name,
        /*in_endpoint_name=*/new_cross_node_endpoint_name);
  }
}

};  // namespace mage
