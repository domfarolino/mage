#ifndef MAGE_CORE_ENDPOINT_H_
#define MAGE_CORE_ENDPOINT_H_

#include <memory>
#include <queue>
#include <string>

#include "base/synchronization/mutex.h"
#include "mage/public/message.h"

namespace base {
class TaskRunner;
};  // namespace base

namespace mage {

class ReceiverDelegate;

struct Address {
  std::string node_name;
  std::string endpoint_name;
};

class Endpoint final {
 public:
  enum class State {
    kBound,
    kUnboundAndQueueing,
    kUnboundAndProxying,
  };

  explicit Endpoint(std::string name)
      : name(name), state(State::kUnboundAndQueueing) {}
  Endpoint() = delete;
  Endpoint(const Endpoint&) = delete;
  Endpoint& operator=(const Endpoint&) = delete;

  // This method is called by `Node` on the IO thread. It is only called when a
  // message is received from a remote node/process via `Channel`. When this is
  // called, `state` is either `kBound` or `kUnboundAndQueueing`. See
  // documentation above `AcceptMessageOnDelegateThread()` for more information.
  //   - kBound: We post a task to deliever this message to `delegate_`
  //   - kUnboundAndQueueing: The message will be queued to
  //     `incoming_message_queue_` and replayed to `delegate_` once bound, if
  //      ever. Or if this endpoint is sent to a remote node, then `Node` will
  //     take all of our messages and send them to the new destination. See
  //     `TakeQueuedMessages()`.
  // This method always processes the `EndpointDescriptors` in `message` if any,
  // by registering them with the local `Node`. We expect that the endpoints in
  // this message do not exist until we create them. They might already exist if
  // the endpoints are traveling back to a process they've already been at
  // before, but we do not support that use-case currently.
  void AcceptMessageOnIOThread(Message message);  // Guarded by `lock_`.

  // This method is called by `Node` on any thread but the IO thread. It is only
  // called when a message is received from a local peer endpoint. When this is
  // called, `state` is either `kBound` or `kUnboundAndQueueing`. See
  // documentation above `AcceptMessageOnDelegateThread()` for more information.
  //
  // This method looks at the `EndpointDescriptors` in `message`, but only to
  // retrieve the existing `MessagePipe`s from the local process that represent
  // each descriptor in the message. We specifically do not try and register
  // each descriptor with the local process, since it is expected that they
  // already represent valid endpoints in the local process. That's because this
  // path is hit when `this` receives messages in the same-process case, in
  // which case the sender is local and is sending endpoints that must be
  // registered with the current process.
  void AcceptMessageOnDelegateThread(Message message);  // Guarded by `lock_`.

  // The messages in |incoming_message_queue_| are queued in this endpoint and
  // are waiting to be dispatched to |delegate_| once it is bound. However if
  // this endpoint is being represented by a remote endpoint, someone will want
  // to take these queued messages from us, deliver them to the remote endpoint,
  // and delete us. Once the messages are delivered to the remote endpoint, they
  // are either queued (and might go through this same path), or delivered to
  // its bound |delegate_|.
  std::queue<Message> TakeQueuedMessages();

  void RegisterDelegate(std::weak_ptr<ReceiverDelegate> delegate,
                        std::shared_ptr<base::TaskRunner> delegate_task_runner);

  // Not implemented yet. See design notes for why unregistering a receiver
  // delegate is difficult.
  void UnregisterDelegate();

  void SetProxying(std::string in_node_name, std::string in_endpoint_name);

  void Lock() { lock_.lock(); }
  void Unlock() { lock_.unlock(); }

  const std::string name;
  Address peer_address;

  // Must be accessed/updated atomically with `delegate_`,
  // `delegate_task_runner_`, and `incoming_message_queue_`.
  State state;

  Address proxy_target;

 private:
  // This can be called on:
  //   - The IO thread, by `AcceptMessageOnIOThread()` (see below)
  //   - The UI or a worker thread, when receiving a message from a local
  //     endpoint. This kind of message went through the `peer_is_local` path in
  //     `Node::SendMessage()`. This is also called for each message in
  //     `incoming_message_queue_` when `delegate_` is bound (i.e., going from
  //     `kUnboundAndQueueing` state to `kBound`).
  // In both of these cases, `state` is either:
  //   - kBound: The message will immediately be dispatched to the underlying
  //    `delegate_`.
  //   - kUnboundAndQueueing: The message will be queued in
  //     `incoming_message_queues_` and will be replayed to `delegate_` once
  //     bound.
  void AcceptMessage(Message message);  // Guarded by `lock_`.

  // Thread-safe: This method is called whenever `this` needs to post a message
  // to delegate, which happens from either `AcceptMessage()` or
  // `RegisterDelegate()`, both of which grab a lock.
  void PostMessageToDelegate(Message message);  // Guarded by `lock_`.

  // This is used when |delegate_| is null and
  // `state == State::kUnboundAndQueueing`. That is, when `this` is not bound to
  // a local interface receiver. We queue the messages here, and then later once
  // bound, these messages will be forwarded, in order, to |delegate_|.
  //
  // Must be accessed/updated atomically with `state`, `delegate_` and
  // `delegate_task_runner_`.
  std::queue<Message> incoming_message_queue_;

  // When `this` is bound and receives a message, we forward the message to
  // `delegate_`. But we may receive the message on a different thread than
  // `delegate_` was bound on, so when require that a delegate pass in a
  // TaskRunner to the thread it's running on, so that we can post a task to
  // that thread for the receiver to read the message.
  //
  // TaskRunners are thread-safe, so ordinarily we wouldn't need to guard access
  // to one with a lock, but in this class, `delegate_task_runner_` is expected
  // to be assigned (and eventually cleared) with `delegate_`. That means one
  // thread may try and assign this member while another thread wants to use it,
  // so access must be guarded by `lock_` so that it doesn't get out-of-sync
  // with `delegate_`.
  std::shared_ptr<base::TaskRunner> delegate_task_runner_;

  // The receiver we post messages to when `state == State::kBound`.
  //
  // Must be accessed/updated atomically with `state`, `delegate_task_runner_`,
  // and `incoming_message_queue_`.
  std::weak_ptr<ReceiverDelegate> delegate_;

  // `this` can be accessed simultaneously on two different threads, so `lock_`
  // ensures that whatever state needs to be accessed atomically is done so. For
  // example the following must be accessed/updated atomically:
  //   - `state`
  //   - `delegate_`
  //   - `delegate_task_runner_`
  //   - `incoming_message_queue_`
  //
  // To see see why, imagine the following two happen at the same time:
  //   1.) [IO Thread]: `AcceptMessage()` is called while
  //       `this.state == State::kUnboundAndQueueing`. We queue the message to
  //       `incoming_message_queue_` since we don't have a delegate to post the
  //       message to
  //   2.) [UI Thread]: `RegisterDelegate()` is called. We set
  //       `this.state = State::kBound` and check to see if we have any queued
  //       messages in `incoming_message_queue_`. If so, forward them. If not,
  //       since we just set our state to `State::kBound`, the next time we
  //       receive a
  //       message it will be forwarded to the delegate
  //
  // When both of the above happen at the same time we can hit the following
  // races:
  //   1.) `AcceptMessage()` observes `state == State::kUnboundAndQueueing`, so
  //       it decides to queue a message. After it has decided this, but before
  //       it actually queues the message, `RegisterDelegate()` gets called and
  //       sets `state = State::kBound` and assigns `delegate_`. It checks the
  //       queue for messages to post to the newly-bound `delegate_`. Queue is
  //       empty, so it returns. By the time `AcceptMessage()` queues the
  //       message to `incoming_message_queue_`, `state == State::kBound`, which
  //       means we should be posting the message to the newly-bound `delegate_`
  //       instead of queueing. The message sits there and never gets posted.
  //   2.) `RegisterDelegate()` gets called and sets (a) `state = kBound`, and
  //       (b) assigns `delegate_` and `delegate_task_runner_`. But in between
  //       (a) and (b), `AcceptMessage()` is called and observes
  //       `state == kBound`, so it attempts to use `delegate_task_runner_`
  //       which has not yet been set.
  base::Mutex lock_;
};

};  // namespace mage

#endif  // MAGE_CORE_ENDPOINT_H_
