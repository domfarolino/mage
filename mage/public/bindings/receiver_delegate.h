#ifndef MAGE_PUBLIC_RECEIVER_DELEGATE_H_
#define MAGE_PUBLIC_RECEIVER_DELEGATE_H_

#include <memory>

#include "mage/public/message.h"

namespace mage {

class ReceiverDelegate {
 public:
  virtual ~ReceiverDelegate() = default;
  // This is the only public method in this class, because it is the
  // gatekeeper for dispatching a message to a delegate. This method receives
  // `message` bound for `weak_delegate`, and runs on the task loop that
  // `weak_delegate` was bound on in `Endpoint`. First check if
  // `weak_delegate` is still alive; if so, we can dispatch the message.
  // Otherwise, the message must be dropped because the delegate is dead.
  static void DispatchMessageIfStillAlive(
      std::weak_ptr<ReceiverDelegate> weak_delegate,
      mage::Message message) {
    std::shared_ptr<ReceiverDelegate> receiver = weak_delegate.lock();
    if (!receiver) {
      LOG("\033[31;1m[static] "
          "ReceiverDelegate::DispatchMessageIfStillAlive() received a "
          "message for a destroyed delegate. Dropping the message.\033[0m");
      return;
    }

    receiver->OnReceivedMessage(std::move(message));
  }

 private:
  virtual void OnReceivedMessage(Message) = 0;
};

};  // namespace mage

#endif  // MAGE_PUBLIC_RECEIVER_DELEGATE_H_
