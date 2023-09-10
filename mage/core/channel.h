#ifndef MAGE_CORE_CHANNEL_H_
#define MAGE_CORE_CHANNEL_H_

#include <string>

#include "base/scheduling/task_loop_for_io.h"

namespace mage {

class Message;

// The underlying implementation of cross-node (typically cross-process)
// communication via socket primitives. This object gets created synchronously
// on whatever thread the Mage embedder calls operations that trigger its
// creation (i.e., sending or accepting an invitation). This class is
// responsible for IO, and as such, implements the `SocketReader` interface
// whose concrete implementation is provided by the Mage embedder. This leads to
// two interesting interactions that `this` has with the embedder-provided IO
// mechanism:
//   1. WRITE: Messages sent over `this` are written (to the underlying IO
//      mechanism managed by `SocketReader`) ON THE THREAD that triggered
//      `SendMessage()`. That is, we don't necessarily have a dedicated thread
//     for sending messages; this can happen on any thread.
//   2. READ: By `this` implementing the `SocketReader` interface, the Mage
//      embedder can tell us (on the thread that *it* does IO on), when a
//      message can be read from the underlying socket mechamism managed by
//      `SocketReader`. This will likely be on a dedicated IO thread (for
//      example, when the //base library [1] is being used by the Mage
//      embedder), but that is not technically required or enforced.
//
// One major corollary that follows from the above is that reading and writing
// messages can happen on different threads. Furthermore, since many threads can
// use `this` to write/send multiple messages to the same process at the same
// time, it is expected that the underlying socket mechanism (typically Unix
// domain sockets on Linux or Mach Ports on macOS) is thread-safe, as `this`
// does not provide a thread-safe abstraction on top of it.
//
// [1]: https://github.com/domfarolino/base.
class Channel : public base::TaskLoopForIO::SocketReader {
 public:
  class Delegate {
   public:
    virtual ~Delegate() = default;
    virtual void OnReceivedMessage(Message message) = 0;
  };

  Channel(int fd, Delegate* delegate);
  virtual ~Channel();

  // `Start()` can be called from any thread — it's typically called
  // whenever accepting or receiving process invitations. It registers `this`
  // with the Mage embedder-supplied IO mechanism as an IO listener. The IO
  // listener can listen for messages on the socket that the `SocketReader` base
  // class is initialized with. "Listening" happens on whatever thread the Mage
  // embedder deems is its "IO thread", which means `this` can *immediately*
  // start receiving messages via `OnCanReadFromSocket()` on a different thread
  // than `Start()` was called on.
  //
  // [1]: https://github.com/domfarolino/base
  void Start();
  void SetRemoteNodeName(const std::string& name);
  void SendInvitation(std::string inviter_name,
                      std::string temporary_remote_node_name,
                      std::string intended_endpoint_peer_name);
  void SendAcceptInvitation(std::string temporary_remote_node_name,
                            std::string actual_node_name,
                            std::string accept_invitation_endpoint_name);
  void SendMessage(Message message);

  // base::TaskLoopForIO::SocketReader implementation:
  void OnCanReadFromSocket() override;

 private:
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
