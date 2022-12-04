#include <fcntl.h>
#include <sys/socket.h>
#include <unistd.h>

#include <iostream>
#include <memory>
#include <string>

#include "base/scheduling/scheduling_handles.h"
#include "base/scheduling/task_loop_for_io.h"
#include "mage/demo/magen/demo.magen.h"  // Generated.
#include "mage/public/api.h"
#include "mage/public/bindings/message_pipe.h"
#include "mage/public/bindings/receiver.h"

class DemoImpl : public magen::Demo {
 public:
  DemoImpl(mage::MessagePipe handle) {
    receiver_.Bind(handle, this);
    base::GetCurrentThreadTaskLoop()->Run();
  }

  // magen::Demo implementation.
  void Method1(int a, std::string b, std::string c) override {
    printf("DemoImpl::Method1()\n");
    printf("  a: %d, b: %s, c: %s\n", a, b.c_str(), c.c_str());
  }
  void SendName(std::string name, bool is_first) override {
    printf("DemoImpl::SendName()\n");
    printf("  name: %s, is_first: %d\n", name.c_str(), is_first);
  }

 private:
  mage::Receiver<magen::Demo> receiver_;
};

void OnInvitationAccepted(mage::MessagePipe message_pipe) {
  std::unique_ptr<DemoImpl> demo(new DemoImpl(message_pipe));
}

int main(int argc, char** argv) {
  printf("-------- Child process --------\n");
  auto task_loop = base::TaskLoop::Create(base::ThreadType::IO);
  mage::Init();

  int fd = std::stoi(argv[1]);

  mage::AcceptInvitation(fd, &OnInvitationAccepted);
  task_loop->Run();
  return 0;
}
