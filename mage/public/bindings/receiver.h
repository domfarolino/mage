#ifndef MAGE_BINDINGS_RECEIVER_H_
#define MAGE_BINDINGS_RECEIVER_H_

#include <memory>

#include "base/scheduling/scheduling_handles.h"
#include "base/threading/thread_checker.h"
#include "mage/public/bindings/message_pipe.h"
#include "mage/public/message.h"

namespace mage {

template <typename Interface>
class Receiver {
 public:
  using InterfaceStub = typename Interface::ReceiverStub;

  Receiver() = default;

  void Bind(MessagePipe local_pipe, Interface* impl) {
    CHECK(thread_checker_.CalledOnConstructedThread());
    stub_ = std::make_shared<InterfaceStub>();
    // We pass in the current thread's `base::TaskRunner` so that mage knows
    // which task runner to dispatch messages to `impl_` on.
    stub_->BindToPipe(local_pipe, impl, base::GetCurrentThreadTaskRunner());
  }

  // In the future, we'll want to support unbinding receivers. This is difficult
  // to achieve however; see the design notes.

 private:
  // This is the receiver stub that once bound to a |MessagePipe|, associates
  // itself with the underlying |mage::Endpoint| that receives messages bound
  // for |impl_|. It is a generated object that automatically deserializes the
  // message data and dispatches the correct method on |impl_|.
  //
  // The lifetime of `stub_` is directly tied to `this`, which could be
  // destroyed by the time mage core tries to dispatch a message to `stub_` for
  // deserialization. To account for this, `stub_` passes a weak pointer of
  // itself to mage core for async message dispatching. Mage core can check if
  // `stub_` (and implicitly `this`) is still alive, and if so, dispatch the
  // message. If not, the message must be dropped.
  std::shared_ptr<InterfaceStub> stub_;

  // If we ever have the use-case of creating a receiver on one thread and
  // binding it on another, then we can remove this. Until then however, this is
  // the safest way to maintain this object.
  base::ThreadChecker thread_checker_;
};

};  // namespace mage

#endif  // MAGE_BINDINGS_RECEIVER_H_
