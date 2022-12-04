#include <fcntl.h>
#include <sys/socket.h>
#include <unistd.h>

#include <iostream>
#include <memory>
#include <string>

#include "base/scheduling/scheduling_handles.h"
#include "base/scheduling/task_loop_for_io.h"
#include "mage/public/api.h"
#include "mage/public/bindings/message_pipe.h"
#include "mage/public/bindings/remote.h"
#include "mage/test/magen/test.magen.h"  // Generated.

int main(int argc, char** argv) {
  std::shared_ptr<base::TaskLoop> main_thread =
      base::TaskLoop::Create(base::ThreadType::UI);
  base::Thread io_thread(base::ThreadType::IO);
  io_thread.Start();
  io_thread.GetTaskRunner()->PostTask(main_thread->QuitClosure());
  main_thread->Run();
  mage::Init();

  int fd = std::stoi(argv[1]);
  mage::MessagePipe message_pipe =
      mage::SendInvitationAndGetMessagePipe(fd, [&]() {
        mage::Remote<magen::TestInterface> remote;
        remote.Bind(message_pipe);
        remote->Method1(1, .5, "message");
        remote->SendMoney(1000, "JPY");

        // Quit the loop now that our work is done.
        base::GetCurrentThreadTaskLoop()->Quit();
      });

  // This will spin the loop until the lambda above is invoked. This process
  // will exit after the above mage message is sent.
  main_thread->Run();
  return 0;
}
