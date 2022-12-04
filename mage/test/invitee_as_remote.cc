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
#include "mage/public/api.h"
#include "mage/public/bindings/message_pipe.h"
#include "mage/public/bindings/remote.h"
#include "mage/test/magen/test.magen.h"  // Generated.

void OnInvitationAccepted(mage::MessagePipe handle) {
  CHECK_ON_THREAD(base::ThreadType::UI);
  mage::Remote<magen::TestInterface> remote;
  remote.Bind(handle);
  remote->Method1(1, .5, "message");
  remote->SendMoney(1000, "JPY");

  // Stop the loop and kill the process.
  base::GetCurrentThreadTaskLoop()->Quit();
}

int main(int argc, char** argv) {
  std::shared_ptr<base::TaskLoop> main_thread =
      base::TaskLoop::Create(base::ThreadType::UI);
  base::Thread io_thread(base::ThreadType::IO);
  io_thread.Start();
  io_thread.GetTaskRunner()->PostTask(main_thread->QuitClosure());
  main_thread->Run();

  mage::Init();

  CHECK_EQ(argc, 2);
  int fd = std::stoi(argv[1]);
  mage::AcceptInvitation(fd, &OnInvitationAccepted);

  main_thread->Run();
  return 0;
}
