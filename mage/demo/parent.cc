#include <fcntl.h>
#include <sys/socket.h>
#include <unistd.h>

#include <iostream>
#include <memory>
#include <string>

#include "base/scheduling/task_loop_for_io.h"
#include "mage/demo/magen/demo.magen.h"  // Generated.
#include "mage/public/bindings/message_pipe.h"
#include "mage/public/bindings/remote.h"
#include "mage/public/core.h"

int main() {
  printf("-------- Parent process --------\n");
  int fds[2];
  CHECK_EQ(socketpair(AF_UNIX, SOCK_STREAM, 0, fds), 0);
  CHECK_EQ(fcntl(fds[0], F_SETFL, O_NONBLOCK), 0);
  CHECK_EQ(fcntl(fds[1], F_SETFL, O_NONBLOCK), 0);

  // Spin up a new process, and have it access fds[1].
  pid_t rv = fork();
  if (rv == 0) {  // Child.
    rv = execl("./bazel-bin/mage/demo/child",
               "--mage-socket=", std::to_string(fds[1]).c_str());
  }

  auto task_loop = base::TaskLoop::Create(base::ThreadType::IO);
  mage::Core::Init();

  mage::MessagePipe local_message_pipe =
      mage::Core::SendInvitationAndGetMessagePipe(fds[0]);

  mage::Remote<magen::Demo> remote(local_message_pipe);
  remote->Method1(1, "dom", "farolino");
  remote->SendName("domfarolino", true);

  task_loop->Run();
  return 0;
}
