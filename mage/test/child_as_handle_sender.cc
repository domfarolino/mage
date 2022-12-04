#include <fcntl.h>
#include <sys/socket.h>
#include <unistd.h>

#include <iostream>
#include <memory>
#include <string>

#include "base/check.h"
#include "base/scheduling/scheduling_handles.h"
#include "base/scheduling/task_loop_for_io.h"
#include "base/threading/thread_checker.h"
#include "mage/public/bindings/message_pipe.h"
#include "mage/public/bindings/remote.h"
#include "mage/public/core.h"
#include "mage/test/magen/first_interface.magen.h"  // Generated.

void OnInvitationAccepted(mage::MessagePipe remote_handle) {
  CHECK_ON_THREAD(base::ThreadType::UI);
  mage::Remote<magen::FirstInterface> remote;
  remote.Bind(remote_handle);

  // Create a dummy message pipe pair and send one end to the parent as a dummy
  // `magen::SecondInterface`.
  std::vector<mage::MessagePipe> pipes = mage::Core::CreateMessagePipes();
  remote->SendSecondInterfaceReceiver(pipes[1]);

  // We could create a remote to `magen::SecondInterface` out of `pipes[0]` and
  // send messages, but that isn't necessary for this test.
}

int main(int argc, char** argv) {
  std::shared_ptr<base::TaskLoop> main_thread =
      base::TaskLoop::Create(base::ThreadType::UI);
  base::Thread io_thread(base::ThreadType::IO);
  io_thread.Start();
  io_thread.GetTaskRunner()->PostTask(main_thread->QuitClosure());
  main_thread->Run();

  mage::Core::Init();

  CHECK_EQ(argc, 2);
  int fd = std::stoi(argv[1]);
  mage::Core::AcceptInvitation(fd, &OnInvitationAccepted);

  main_thread->Run();
  return 0;
}
