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
#include "mage/test/magen/first_interface.magen.h"     // Generated.
#include "mage/test/magen/fourth_interface.magen.h"    // Generated.
#include "mage/test/magen/second_interface.magen.h"    // Generated.
#include "mage/test/magen/third_interface.magen.h"     // Generated.

// The correct sequence of message that this binary will receive from the parent
// process (test runner) is:
//   1.) FirstInterface::SendString()
//   2.) FirstInterface::SendHandles()
//   3.) SecondInterface::SendStringAndNotifyDoneViaCallback()
//   4.) SecondInterface::NotifyDoneViaCallback() (only if the test stops here)
//   5.) [Maybe] SecondInterface::SendReceiverForThirdInterface()
//   6.) [If above] ThirdInterface::SendReceiverForFourthInterface()
//   7.) [If above] FourthInterface::SendStringAndNotifyDoneViaCallback()
//   8.) [If above] FourthInterface::NotifyDoneViaCallback()

bool first_interface_received_send_string = false;
bool first_interface_received_send_handles = false;

mage::Remote<magen::CallbackInterface> global_callback_remote;

class FourthInterfaceImpl final : public magen::FourthInterface {
 public:
  FourthInterfaceImpl(mage::MessagePipe receiver) {
    receiver_.Bind(receiver, this);
  }

  void SendStringAndNotifyDoneViaCallback(std::string message) {
    LOG("\033[34;1mFourthInterfaceImpl received message: %s\033[0m",
        message.c_str());

    global_callback_remote->NotifyDone();
  }

  void NotifyDoneViaCallback() { global_callback_remote->NotifyDone(); }

 private:
  mage::Receiver<magen::FourthInterface> receiver_;
};
std::unique_ptr<FourthInterfaceImpl> global_fourth_interface;

class ThirdInterfaceImpl final : public magen::ThirdInterface {
 public:
  ThirdInterfaceImpl(mage::MessagePipe receiver) {
    receiver_.Bind(receiver, this);
  }

  void NotifyDoneViaCallback() override { NOTREACHED(); }
  void SendReceiverForFourthInterface(mage::MessagePipe receiver) override {
    LOG("\033[34;1mThirdInterfaceImpl got receiver to bootstrap "
        "magen::FourthInterface\033[0m");
    global_fourth_interface = std::make_unique<FourthInterfaceImpl>(receiver);
  }

 private:
  mage::Receiver<magen::ThirdInterface> receiver_;
};
std::unique_ptr<ThirdInterfaceImpl> global_third_interface;

class SecondInterfaceImpl final : public magen::SecondInterface {
 public:
  SecondInterfaceImpl(mage::MessagePipe receiver) {
    receiver_.Bind(receiver, this);
  }

  void SendStringAndNotifyDoneViaCallback(std::string message) override {
    CHECK(first_interface_received_send_string);
    CHECK(first_interface_received_send_handles);

    CHECK_EQ(message, "Message for SecondInterface");
    LOG("\033[34;1mSecondInterfaceImpl received message: %s\033[0m",
        message.c_str());
    // This will notify the parent process that we've received all of the
    // messages, so it can tear things down.
    global_callback_remote->NotifyDone();
  }

  void NotifyDoneViaCallback() override {
    global_callback_remote->NotifyDone();
  }

  void SendReceiverForThirdInterface(mage::MessagePipe receiver) override {
    global_third_interface = std::make_unique<ThirdInterfaceImpl>(receiver);
  }

 private:
  mage::Receiver<magen::SecondInterface> receiver_;
};
std::unique_ptr<SecondInterfaceImpl> global_second_interface;

class FirstInterfaceImpl final : public magen::FirstInterface {
 public:
  FirstInterfaceImpl(mage::MessagePipe handle) { receiver_.Bind(handle, this); }

  void SendString(std::string message) override {
    CHECK_EQ(message, "Message for FirstInterface");
    LOG("\033[34;1mFirstInterfaceImpl received message: %s\033[0m",
        message.c_str());
    first_interface_received_send_string = true;
  }

  // Not used for this test.
  void SendSecondInterfaceReceiver(mage::MessagePipe) override { NOTREACHED(); }

  void SendHandles(mage::MessagePipe second_interface,
                   mage::MessagePipe callback_handle) override {
    CHECK(first_interface_received_send_string);
    CHECK(!first_interface_received_send_handles);
    first_interface_received_send_handles = true;

    global_second_interface =
        std::make_unique<SecondInterfaceImpl>(second_interface);

    // One-time initialization of the callback remote that we use to tell the
    // parent that we received its messages.
    global_callback_remote.Bind(callback_handle);
  }

 private:
  mage::Receiver<magen::FirstInterface> receiver_;
};
std::unique_ptr<FirstInterfaceImpl> global_first_interface;

void OnInvitationAccepted(mage::MessagePipe receiver_handle) {
  CHECK_ON_THREAD(base::ThreadType::UI);
  global_first_interface =
      std::make_unique<FirstInterfaceImpl>(receiver_handle);
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
