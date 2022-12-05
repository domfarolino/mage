#ifndef MAGE_PUBLIC_BINDINGS_REMOTE_H_
#define MAGE_PUBLIC_BINDINGS_REMOTE_H_

#include "mage/public/bindings/message_pipe.h"

namespace mage {

template <typename Interface>
class Remote {
 public:
  using InterfaceProxy = typename Interface::Proxy;

  Remote() : proxy_(std::make_unique<InterfaceProxy>()) {}
  explicit Remote(MessagePipe local_handle) : Remote() { Bind(local_handle); }

  void Bind(MessagePipe local_pipe) { proxy_->BindToPipe(local_pipe); }

  MessagePipe Unbind() { return proxy_->Unbind(); }

  InterfaceProxy* operator->() { return proxy_.get(); }

 private:
  std::unique_ptr<InterfaceProxy> proxy_;
};

};  // namespace mage

#endif  // MAGE_PUBLIC_BINDINGS_REMOTE_H_
