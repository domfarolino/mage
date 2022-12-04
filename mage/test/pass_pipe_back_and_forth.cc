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
#include "mage/public/bindings/receiver.h"
#include "mage/public/bindings/remote.h"
#include "mage/public/core.h"
#include "mage/test/magen/handle_accepter.magen.h"  // Generated.

class HandleAccepterImpl final : public magen::HandleAccepter {
 public:
  HandleAccepterImpl(mage::MessagePipe receiver) {
    receiver_.Bind(receiver, this);
  }

  void PassHandle(mage::MessagePipe handle) {
    if (!pass_handle_has_been_called_once_) {
      // The first time `PassHandle()` is called, we're just receiving a remote
      // to the parent's `magen::HandleAccepter` instance.
      remote_.Bind(handle);
      pass_handle_has_been_called_once_ = true;
      return;
    }

    // All of the other times, we're just receiving a pipe for a callback
    // interface that we need to blindly forward to the parent.
    remote_->PassHandle(handle);
  }

 private:
  mage::Receiver<magen::HandleAccepter> receiver_;
  mage::Remote<magen::HandleAccepter> remote_;

  bool pass_handle_has_been_called_once_ = false;
};
std::unique_ptr<HandleAccepterImpl> global_handle_accepter;

void OnInvitationAccepted(mage::MessagePipe receiver_handle) {
  CHECK_ON_THREAD(base::ThreadType::UI);
  global_handle_accepter =
      std::make_unique<HandleAccepterImpl>(receiver_handle);
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
