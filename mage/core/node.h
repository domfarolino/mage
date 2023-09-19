#ifndef MAGE_CORE_NODE_H_
#define MAGE_CORE_NODE_H_

#include <cstdlib>
#include <map>
#include <memory>
#include <queue>
#include <set>
#include <string>

#include "gtest/gtest_prod.h"
#include "mage/core/channel.h"
#include "mage/public/bindings/message_pipe.h"
#include "mage/public/message.h"
#include "mage/public/util.h"

namespace mage {

class Endpoint;

static const std::string kInitialChannelName = "INIT";

class Node : public Channel::Delegate {
 public:
  Node() : name_(util::RandomIdentifier()) {
    LOG("\n\nNode name_: %s", name_.c_str());
  }
  ~Node() = default;

  // Thread-safe.
  std::vector<MessagePipe> CreateMessagePipes();
  MessagePipe SendInvitationAndGetMessagePipe(int fd);
  void AcceptInvitation(int fd);
  void SendMessage(std::shared_ptr<Endpoint> local_endpoint, Message message);

  // Thread-safe.
  void RegisterEndpoint(std::shared_ptr<Endpoint>);

  // Channel::Delegate implementation:
  //
  // This is immediately called from `Channel` when it can read a new message,
  // which means it is called from whatever thread the Mage embedder deems its
  // "IO" thread.
  void OnReceivedMessage(Message message) override;

 private:
  // To more easily reason about the below methods & data structures.
  using NodeName = std::string;
  using EndpointName = std::string;

  // Control message handlers.
  void OnReceivedInvitation(Message message);
  void OnReceivedAcceptInvitation(Message message);
  // "User" message handler, which handles all non-control messages. These are
  // the "custom" messages that users can write via `Mage` bindings.
  void OnReceivedUserMessage(Message message);

  friend class CoreUnitTest;
  FRIEND_TEST(CoreUnitTest, InitializeAndEntangleEndpointsUnitTest);
  friend class MageTest;

  std::vector<std::pair<MessagePipe, std::shared_ptr<Endpoint>>>
  CreateMessagePipesAndGetEndpoints();
  std::pair<std::shared_ptr<Endpoint>, std::shared_ptr<Endpoint>>
  InitializeAndEntangleEndpoints() const;
  void SendMessagesAndRecursiveDependents(std::queue<Message> messages,
                                          std::string);
  // This is a helper method that is called immediately when we receive a 
  // message that is bound for an endpoint that is in the `kUnboundAndProxying`
  // state. It takes a `Message` and a target `Node` name, and updates all of
  // the `EndpointDescriptor`s in the message for delivery in the target node,
  // and also sets each of their backing local endpoints to the proxying state,
  // so they continue to forward any messages they receive.
  void PrepareToForwardUserMessage(Message& message,
                                   NodeName proxy_target_node_name);

  std::string name_;

  // True once |this| accepts an invitation from an inviter node.
  bool has_accepted_invitation_ = false;

  // All endpoints that are local to this node, that is, whose address's
  // "node name" is our |name_|.
  std::map<EndpointName, std::shared_ptr<Endpoint>> local_endpoints_;
  // This is used to synchronized access to `local_endpoints_` above, since it
  // can be accessed from multiple threads.
  base::Mutex local_endpoints_lock_;

  // Used when we send a message from a (necessarily, local) endpoint in order
  // to find the channel associated with its peer endpoint. Without this, we
  // couldn't send remote messages. All node names in this map will never be our
  // own |name_| since this is only used for remote nodes. Messages to an
  // endpoint in the same node (that is, from an endpoint in Node A to its peer
  // endpoint also in Node A) go through a different path.
  std::map<NodeName, std::unique_ptr<Channel>> node_channel_map_;
  base::Mutex node_channel_map_lock_;

  // Maps |NodeNames| that we've sent invitations to and are awaiting
  // acceptances from, to an |Endpoint| that we've reserved for the peer node.
  // The node names in this map are the temporary one we generate for a peer
  // node before it has told us its real name. Once an invitation acceptance
  // comes back from the node that identifies itself to us by the temporary name
  // we've given it, we update instances of its temporary name with its "real"
  // one that it provides in the invitation acceptance message.
  std::map<NodeName, std::shared_ptr<Endpoint>> pending_invitations_;
  base::Mutex pending_invitations_lock_;
};

};  // namespace mage

#endif  // MAGE_CORE_NODE_H_
