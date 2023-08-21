#include <fcntl.h>
#include <sys/socket.h>

#include <memory>
#include <string>

#include "base/scheduling/task_loop_for_io.h"
#include "gtest/gtest.h"
#include "mage/core/core.h"
#include "mage/core/endpoint.h"
#include "mage/core/node.h"
#include "mage/public/api.h"
#include "mage/public/bindings/message_pipe.h"
#include "mage/public/bindings/receiver.h"
#include "mage/public/bindings/remote.h"
#include "mage/test/magen/first_interface.magen.h"     // Generated.
#include "mage/test/magen/second_interface.magen.h"    // Generated.
#include "mage/test/magen/test.magen.h"                // Generated.

namespace {

class DummyProcessLauncher {
 public:
  DummyProcessLauncher() {
    EXPECT_EQ(socketpair(AF_UNIX, SOCK_STREAM, 0, fds_), 0);
    EXPECT_EQ(fcntl(fds_[0], F_SETFL, O_NONBLOCK), 0);
    EXPECT_EQ(fcntl(fds_[1], F_SETFL, O_NONBLOCK), 0);
  }
  ~DummyProcessLauncher() {
    EXPECT_EQ(close(fds_[0]), 0);
    EXPECT_EQ(close(fds_[1]), 0);
  }

  void Launch(const char child_binary[]) {
    NOTREACHED();
  }

  int GetLocalFd() {
    return fds_[0];
  }

  int GetRemoteFd() {
    return fds_[1];
  }

 private:
  int fds_[2];
};

} // namespace

namespace mage {

class CoreUnitTest : public testing::Test {
 public:
  CoreUnitTest(): io_thread(base::ThreadType::IO) {}

  void SetUp() override {
    dummy_launcher = std::unique_ptr<DummyProcessLauncher>(new DummyProcessLauncher());
    main_thread = base::TaskLoop::Create(base::ThreadType::UI);
    io_thread.Start();
    // Mage relies on `base::GetIOThreadTaskLoop()` being synchronously
    // available from the UI thread upon start up, which only happens after the
    // IO thread has actually started, which we can know by only continuing once
    // we've confirmed it is running tasks.
    io_thread.GetTaskRunner()->PostTask(main_thread->QuitClosure());
    main_thread->Run();

    mage::Core::Init();
    EXPECT_TRUE(mage::Core::Get());
  }

  void TearDown() override {
    mage::Core::ShutdownCleanly();
    io_thread.StopWhenIdle(); // Blocks.
    main_thread.reset();
    dummy_launcher.reset();
  }

 protected:
  std::map<MessagePipe, std::shared_ptr<Endpoint>>& CoreHandleTable() {
    return mage::Core::Get()->handle_table_;
  }

  std::map<std::string, std::shared_ptr<Endpoint>>& NodeLocalEndpoints() {
    return mage::Core::Get()->node_->local_endpoints_;
  }

  mage::Node& Node() {
    return *mage::Core::Get()->node_.get();
  }

  std::unique_ptr<DummyProcessLauncher> dummy_launcher;
  std::shared_ptr<base::TaskLoop> main_thread;
  base::Thread io_thread;
};

TEST_F(CoreUnitTest, CoreInitStateUnitTest) {
  EXPECT_EQ(CoreHandleTable().size(), 0);
  EXPECT_EQ(NodeLocalEndpoints().size(), 0);
}

TEST_F(CoreUnitTest, UseUnboundRemoteCrashes) {
  mage::Remote<magen::TestInterface> remote;
  ASSERT_DEATH({
    remote->SendMoney(0, "");
  }, "bound_*");
}
TEST_F(CoreUnitTest, UseUnboundRemoteCrashes2) {
  std::vector<mage::MessagePipe> pipes = mage::Core::CreateMessagePipes();
  mage::Remote<magen::TestInterface> remote;
  remote.Bind(pipes[0]);
  remote->SendMoney(0, "");

  remote.Unbind();
  ASSERT_DEATH({
    remote->SendMoney(0, "");
  }, "bound_*");
}

// `Endpoint`s only track of they are bound to a receiver, not a remote, so when
// we send an endpoint over an existing pipe, the logic in `Node::SendMessage()`
// that checks to see if all sent-endpoints are bound only doesn't work for
// endpoints bound to a remote, as this test sadly asserts. This isn't great
// behavior, but it shouldn't really be possible to run into anyways once
// MessagePipes move-only.
TEST_F(CoreUnitTest, SendBoundRemoteTechnicallyAllowedUnitTest) {
  std::vector<mage::MessagePipe> first_pair = mage::Core::CreateMessagePipes();
  std::vector<mage::MessagePipe> second_pair = mage::Core::CreateMessagePipes();

  mage::Remote<magen::FirstInterface> remote;
  remote.Bind(first_pair[0]);
  mage::Remote<magen::SecondInterface> second_remote(second_pair[0]);

  // Bad, but we don't protect against it:
  remote->SendSecondInterfaceReceiver(second_pair[0]);
}

class SIDummy : public magen::SecondInterface {
 public:
  void SendStringAndNotifyDoneViaCallback(std::string msg) { NOTREACHED(); }
  void NotifyDoneViaCallback() { NOTREACHED(); }
  void SendReceiverForThirdInterface(MessagePipe receiver) { NOTREACHED(); }
};
TEST_F(CoreUnitTest, SendBoundReceiverUnitTest) {
  std::vector<mage::MessagePipe> first_pair = mage::Core::CreateMessagePipes();
  std::vector<mage::MessagePipe> second_pair = mage::Core::CreateMessagePipes();

  mage::Remote<magen::FirstInterface> remote(first_pair[0]);
  mage::Receiver<magen::SecondInterface> receiver;
  SIDummy second_interface_impl;
  receiver.Bind(second_pair[0], &second_interface_impl);

  ASSERT_DEATH({
    remote->SendSecondInterfaceReceiver(second_pair[0]);
  }, "endpoint->state != Endpoint::State::kBound*");
}

class SecondInterfaceOnlyStringAcceptor : public magen::SecondInterface {
 public:
  void SendStringAndNotifyDoneViaCallback(std::string msg) {
    base::GetCurrentThreadTaskLoop()->Quit();
  }
  void NotifyDoneViaCallback() { NOTREACHED(); }
  void SendReceiverForThirdInterface(MessagePipe receiver) { NOTREACHED(); }
};
TEST_F(CoreUnitTest, RemoteAndReceiverDifferentInterfaces) {
  std::vector<mage::MessagePipe> pipes = mage::Core::CreateMessagePipes();

  mage::Remote<magen::FirstInterface> remote(pipes[0]);
  mage::Receiver<magen::SecondInterface> receiver;
  SecondInterfaceOnlyStringAcceptor second_interface_impl;
  receiver.Bind(pipes[1], &second_interface_impl);

  remote->SendString("Dominic");
  ASSERT_DEATH({
    main_thread->Run();
  }, "false*");
}

TEST_F(CoreUnitTest, InitializeAndEntangleEndpointsUnitTest) {
  const auto& [local, remote] = Node().InitializeAndEntangleEndpoints();

  EXPECT_EQ(CoreHandleTable().size(), 0);
  EXPECT_EQ(NodeLocalEndpoints().size(), 0);

  EXPECT_EQ(local->name.size(), 15);
  EXPECT_EQ(remote->name.size(), 15);
  EXPECT_NE(local->name, remote->name);

  // Both endpoints address the same node name.
  EXPECT_EQ(local->peer_address.node_name, remote->peer_address.node_name);

  // Both endpoints address each other.
  EXPECT_EQ(local->peer_address.endpoint_name, remote->name);
  EXPECT_EQ(remote->peer_address.endpoint_name, local->name);
}

TEST_F(CoreUnitTest, SendInvitationUnitTest) {
  MessagePipe message_pipe =
    mage::Core::SendInvitationAndGetMessagePipe(
      dummy_launcher->GetLocalFd()
    );

  EXPECT_NE(message_pipe, kInvalidPipe);
  EXPECT_EQ(CoreHandleTable().size(), 2);
  EXPECT_EQ(NodeLocalEndpoints().size(), 2);

  // Test that we can queue messages.
  mage::Remote<magen::TestInterface> remote;
  remote.Bind(message_pipe);
  remote->Method1(1, .4, "test");
}

TEST_F(CoreUnitTest, AcceptInvitationUnitTest) {
  mage::Core::AcceptInvitation(dummy_launcher->GetLocalFd(),
                               [](MessagePipe) {
                                 NOTREACHED();
                               });

  // Invitation is asynchronous, so until we receive and formally accept the
  // information, there is no impact on our mage state.
  EXPECT_EQ(CoreHandleTable().size(), 0);
  EXPECT_EQ(NodeLocalEndpoints().size(), 0);
}

}; // namespace mage
