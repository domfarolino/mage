#ifndef MAGE_CORE_CHANNEL_H_
#define MAGE_CORE_CHANNEL_H_

#include <string>

#include "base/scheduling/task_loop_for_io.h"

namespace mage {

class Message;

// The underlying implementation of cross-node (typically cross-process)
// communication via socket primitives. Always runs on the IO thread.
class Channel : public base::TaskLoopForIO::SocketReader {
 public:
  class Delegate {
   public:
    virtual ~Delegate() = default;
    virtual void OnReceivedMessage(Message message) = 0;
  };

  Channel(int fd, Delegate* delegate);
  virtual ~Channel();

  void Start();
  void SetRemoteNodeName(const std::string& name);
  void SendInvitation(std::string inviter_name,
                      std::string intended_endpoint_peer_name);
  void SendAcceptInvitation(std::string temporary_remote_node_name,
                            std::string actual_node_name,
                            std::string accept_invitation_endpoint_name);
  void SendMessage(Message message);

  // base::TaskLoopForIO::SocketReader implementation:
  void OnCanReadFromSocket() override;

 private:
  // This is always initialized as something temporary until we hear back from
  // the remote node and it tells us its name.
  std::string remote_node_name_;

  // The |Delegate| owns |this|, so this pointer will never be null.
  Delegate* delegate_;

  // Reference because |this| will always have a shorter lifetime than the task
  // loop. This is enforced by the fact that we register ourselves as a
  // SocketReader upon construction and unregister ourselves upon destruction,
  // and if the loop destructs with lingering socket readers, it will CHECK. We
  // could make this more explicit by introducing a task loop destruction
  // observer mechanism for us to listen to and clean up in response to, but for
  // now this works.
  base::TaskLoopForIO& io_task_loop_;
};

};  // namespace mage

#endif  // MAGE_CORE_CHANNEL_H_
