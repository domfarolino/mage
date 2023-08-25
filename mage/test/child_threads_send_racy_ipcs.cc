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
#include "mage/public/bindings/receiver.h"
#include "mage/public/bindings/remote.h"
#include "mage/test/magen/callback_interface.magen.h"  // Generated.
#include "mage/test/magen/handle_accepter.magen.h"  // Generated.

// See the test `MageTest.MultiThreadRacyMessageSendingFromRemoteProcess` that
// corresponds to this test binary.

const int kNumThreads = 10;
const int kNumMessagesEachThread = 100;

class HandleAccepter : public magen::HandleAccepter {
 public:
  HandleAccepter(mage::MessagePipe receiver_pipe) {
    receiver_.Bind(receiver_pipe, this);
  }

  void PassHandle(mage::MessagePipe remote_pipe) override {
    remote_pipes_.push_back(remote_pipe);

    if (remote_pipes_.size() == kNumThreads) {
      SpinUpThreadsAndSendMessagesToParent();
    }
  }

 private:
  void SpinUpThreadsAndSendMessagesToParent() {
    // Create all of the threads and send all of the messages back to the
    // parent.
    std::vector<std::unique_ptr<base::Thread>> worker_threads;
    for (int i = 0; i < kNumThreads; ++i) {
      mage::MessagePipe remote_pipe = remote_pipes_[i];

      worker_threads.push_back(
          std::make_unique<base::Thread>(base::ThreadType::WORKER));
      worker_threads[i]->Start();
      worker_threads[i]->GetTaskRunner()->PostTask([remote_pipe](){
        mage::Remote<magen::CallbackInterface> callback_remote;
        callback_remote.Bind(remote_pipe);

        // Invoke `NotifyDone()` on the callback (that lives on the main thread)
        // `kNumMessagesEachThread` times.
        for (int j = 0; j < kNumMessagesEachThread; ++j) {
          callback_remote->NotifyDone();
        }
      });
    }

    // Wait for each thread to finish before destroying all of them.
    for (auto& thread : worker_threads) {
      thread->StopWhenIdle();
    }
  }

  mage::Receiver<magen::HandleAccepter> receiver_;
  std::vector<mage::MessagePipe> remote_pipes_;
};

void OnInvitationAccepted(mage::MessagePipe receiver_handle) {
  CHECK_ON_THREAD(base::ThreadType::UI);
  HandleAccepter handle_accepter(receiver_handle);

  // Run indefinitely. This process will do its thing and then quietly hang
  // until the parent kills it after the test.
  base::GetCurrentThreadTaskLoop()->Run();
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
