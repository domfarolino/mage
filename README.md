# mage 🧙‍♂️

![ci-shield](https://github.com/domfarolino/mage/actions/workflows/workflow.yml/badge.svg)

A simple cross-platform[^1] interprocess communication (IPC) library written in
C++. Mage is heavily inspired by Chromium's [Mojo IPC library], and written by
[Dominic Farolino] (a Chromium engineer) in his free time.

Chromium's Mojo is feature-rich and battle-tested, but it has a lot of
Chromium-specific dependencies that prevent it from being portable and used by
other applications. The motivation for Mage was to create a watered-down version
of Mojo, with no dependencies on the Chromium tree, for learning purposes and
for use in arbitrary C++ applications.

Right now, Mage's only dependency is [`//base`], a simple threading & scheduling
library developed alongside Mage, although design work for separating the two
entirely is being considered, to make Mage even more standalone. Mage is also
built with [Bazel] but can be integrated with other toolchains.

<details><summary>History!</summary>

The user-facing IDL portion of Mojo was based on Darin Fisher's [ipc_idl]
prototype, which describes a very similar IDL that python generates C++ bindings
from, with the Jinja2 templating engine.

In the mid-2010s, development on the "guts" of Mojo began, with a simple
messaging library called [ports]. If you're a Google employee, you can read
[Mojo Core Ports Overview]. Ports now lives inside Mojo in the Chromium tree:
https://source.chromium.org/chromium/chromium/src/+/main:mojo/core/ports/.

The Mojo language-specific bindings (like `mojo::Remote`, etc.) are built on top
of `mojo/core` which is in turn built on top of ports.

Mojo is built in an impressively layered fashion, allowing for its internals
(much of the stuff underneath the bindings layer) to be swapped out for an
entirely different backing implementation with no (Chromium) developer
consequences. Ken Rockot built the [ipcz] library to experiment replacing much
of the Mojo internals with his IPC system that implements message passing with
shared memory pools instead of explicit message passing with sockets and file
descriptors, etc.

The ultimate goal with Mage is to make it, too, as modular as possible,
allowing it to be used with any external threading & scheduling library, not
just [`//base`]. It would be cool someday if the internals were also decoupled
from the public bindings enough such that we could experiment with other
internal messaging providers such as [ipcz], for example.
</details>

## Overview

Mage IPC allows you to seamlessly send asynchronous messages to an object that
lives in another process, thread, or even the same thread, without the sender
having to know anything about where the target object actually is.

To get started, you need to be familiar with three concepts from its public API:
 - `mage::MessagePipe`
 - `mage::Remote`
 - `mage::Receiver`

Messages are passed over bidirectional message pipes, each end of which is
represented by a `MessagePipe`, which can be passed across processes.
Ultimately, one `MessagePipe` will get bound to a `Remote` and its corresponding
pipe will get bound to a `Receiver`. It is through these objects that arbitrary
user messages get passed as IPCs.

### `mage::Remote`

Once bound, a `Remote<magen::Foo>` represents a local "proxy" for a `magen::Foo`
interface, whose concrete implementation may live in another process. You can
synchronously invoke any of `magen::Foo` interface's methods on a
`Remote<magen::Foo>`, and the remote proxy will forward the message to the right
place, wherever the target object actually lives, even if it is moving around.
See the next section for defining interfaces in Magen IDL.

```cpp
MessagePipe remote_pipe = /* get pipe from somewhere */;
mage::Remote<magen::Foo> remote(remote_pipe);

// Start sending IPCs!
remote->ArbitraryMessage("some payload");
```

### `mage::Receiver`

Messages sent over a bound `mage::Remote<magen::Foo>` get queued on the other
end's `MessagePipe`, until _it_ is bound to a corresponding
`mage::Receiver<magen::Foo>`. A `Receiver<magen::Foo>` represents the concrete
implementation of a Mage interface `magen::Foo`. The receiver itself does not
handle messages sent by the remote, but rather it has a reference to a
user-provided C++ object that implements the `magen::Foo` interface, and it
forwards messages to it. A receiver for a Mage interface is typically owned by
the concrete implementation of that interface.

Here's an example:

```cpp
// Instances of this class can receive asynchronous IPCs from other processes.
class FooImpl final : public magen::Foo {
 public:
  Bind(mage::MessagePipe foo_receiver) {
    // Tell `receiver_` that `this` is the concrete implementation of
    // `magen::Foo` that can handle IPCs.
    receiver_.Bind(foo_receiver, this);
  }

  // Implementation of magen::Foo. These methods get invoked by `receiver_` when
  // it reads messages from its corresponding remote.
  void ArbitraryMessage(string) { /* ... */ }
  void AnotherIPC(MessagePipe) { /* ... */ }

 private:
  // The corresponding remote may live in another process.
  mage::Receiver<magen::Foo> receiver_;
};
```


## Magen Interface Definition Language (IDL)

Magen is the [IDL] that describes Mage interfaces. Interfaces are written in
`.magen` files and are understood by the `magen_idl(...)` Bazel rule, which
generates C++ bindings based on developer-supplied interfaces.

The magen IDL is quite simple (and much less feature-rich than Mojo's IDL). Each
`.magen` file describes a single interface with the `interface` keyword, which
can have any number of methods described by their names and parameters.

Single line C-style comments are supported. Here are a list of supported
parameter types:
 - `bool`
 - `int`
 - `long`
 - `double`
 - `char`
 - `string`
 - `MessagePipe`

The types are self-explanatory, with the exception of `MessagePipe`. A
`MessagePipe` that is not bound to a `Remote` or `Receiver` can be passed from
one process, over an existing IPC interface, to be bound to a
`Remote`/`Receiver` in another process. This is the basic primitive with which
it's possible to expand the number of connections spanning two processes.

Here's an example of an interface:

```cpp
// This interface is implemented by the parent process. It is used by its child
// processes to communicate commands to the parent.
interface ParentProcess {
  // Child tells parent process to navigate to `url`, with an arbitrary delay of
  // `delay` seconds.
  NavigateToURL(string url, int delay);

  // ...
  OpenFile(string name, bool truncate);

  // The parent binds this to a local `mage::Remote<magen::ChildProcess>` so it
  // can send messages *back* to its child.
  BindChildProcessRemote(MessagePipe child_remote);
}
```


## Using Mage in an application

Using Mage to provide IPC support in an application is pretty simple; there are
only a few steps:

 1. Create your interface in a `.magen` file (previous section)
 1. Build your `.magen` file
 1. Implement your interface in C++
 1. Use a `Remote` to send IPCs to your cross-process interface (or any thread)

Let's assume you have a networking application (`main.cc`) that takes URLs from
user input and fetches them, but you want to do the fetching in separate process
(`network_process.cc`). Specifically, `main.cc` will spin up the network process
and tell it what URLs to fetch and when. Consider the project structure:

```
my_project/
├─ src/
│  ├─ BUILD
│  ├─ main.cc
├─ network_process/
│  ├─ BUILD
│  ├─ socket.h
│  ├─ network_process.cc
├─ WORKSPACE
```

### 1. Write the Magen interface for the network process

The first thing you need to do is write the Magen interface that `main.cc` will
use to send messages to the network process. This includes a `FetchURL` IPC that
contains a URL. Magen interfaces are typically defined in a `magen/ directory`:

```cpp
// network_process/magen/network_process.magen

interface NetworkProcess {
  FetchURL(string url);
}
```

### 2. Build your `.magen`

Next, you need to tell your `BUILD` file about the interface in your `.magen`
file, so it can "build" it (generate the requisite C++ code). `magen/`
directories get their own `BUILD` files that invoke the Mage build process.

```starlark
# network_process/magen/BUILD

load("//mage/public/parser:magen_idl.bzl", "magen_idl")

# Generates `network_process.magen.h`, which can be included by depending on the
# ":include" target below.
magen_idl(
  name = "include",
  srcs = [
    "network_process.magen",
  ],
)
```

This tells Mage to generate a C++ header called `network_process.magen.h` based
on the supplied interface. Both `main.cc` and `network_process.cc` can
`#include` this header by listing the `:include` rule as a dependency. For
example, you'd modify `src/BUILD` like so:

```diff
cc_binary(
  name = "main",
  srcs = [
    "main.cc",
  ],
+  deps = [
+    "//mage/public",
+    # Allows `main.cc` to `include` the generated interface header.
+    "//network_process/magen:include",
+  ],
  visibility = ["//visibility:public"],
)
```

You'll need to do the same for `//network_process/BUILD`, so that the
`network_process.cc` binary can include the same header.

### 3. Implement your interface in C++

The C++ object that will _back_ the `magen::NetworkProcess` interface will of
course live in the `network_process.cc` binary, since that's where we'll accept
URLs from the main process to fetch. We'll need to implement this interface now:

```cpp
// network_process/network_process.cc

#include "network_process/magen/network_process.magen.h" // Generated.

// The network process's concrete implementation of the `magen::NetworkProcess`
// interface. Other processes can talk to us via that interface.
class NetworkProcess final : public magen::NetworkProcess {
 public:
  // Bind a receiver that we get from the parent, so `this` can start receiving
  // cross-process messages.
  NetworkProcess(mage::MessagePipe receiver) {
    receiver_.Bind(receiver, this);
  }

  // magen::NetworkProcess implementation:
  void FetchURL(std::string url) override { /* ... */ }
 private:
  mage::Receiver<magen::NetworkProcess> receiver_;
};

// Network process binary.
int main() {
  // Accept the mage invitation from the process.
  mage::MessagePipe network_receiver = /* ... */;
  NetworkProcess network_process_impl(network_receiver);
  // `network_process_impl` can start receiving asynchronous IPCs from the
  // parent process, directing it to fetch URLs.

  RunApplicationLoopForever();
  return 0;
}
```

### 4. Use a `Remote` to send cross-process IPCs

The main application binary can communicate to the network process with a
`mage::Remote<magen::NetworkProcess>`, by calling the interface's methods.

```cpp
// src/main.cc

#include "network_process/magen/network_process.magen.h" // Generated.

// Main binary that the user runs.
int main() {
  MessagePipe network_pipe = /* obtained from creating the network process */
  mage::Remote<magen::NetworkProcess> remote(network_pipe);

  while (true) {
    std::string url /* get user input */;
    remote->FetchURL(url);
  }
  return 0;
}
```


## Sending `MessagePipes` cross-process

The previous section illustrates sending a message over a bound message pipe to
another process, using a single remote/receiver pair that spans the two
processes. But usually you don't want just a single interface responsible for
every single message sent between two processes. That leads to bad layering and
design. Rather, you often want tightly scoped interfaces like the following:

```
┌─────────────────────────┐             ┌──────────────────────────┐
│         Proc A          │             │          Proc B          │
│                         │             │                          │
│  ┌───────────────────┐  │             │  ┌────────────────────┐  │
│  │CompositorDirector │  │             │  │CompositorImpl      │  │
│  │                   │  │             │  │                    │  │
│  │   remote──────────┼──┼─────────────┼──┼──►receiver         │  │
│  └───────────────────┘  │             │  └────────────────────┘  │
│                         │             │                          │
│  ┌───────────────────┐  │             │  ┌────────────────────┐  │
│  │RemoteUIService    │  │             │  │UIServiceImpl       │  │
│  │                   │  │             │  │                    │  │
│  │   remote──────────┼──┼─────────────┼──┼──►receiver         │  │
│  └───────────────────┘  │             │  └────────────────────┘  │
│                         │             │                          │
│  ┌───────────────────┐  │             │  ┌────────────────────┐  │
│  │LoggerImpl         │  │             │  │RemoteLogger        │  │
│  │                   │  │             │  │                    │  │
│  │ receiver◄─────────┼──┼─────────────┼──┼──remote            │  │
│  └───────────────────┘  │             │  └────────────────────┘  │
│                         │             │                          │
└─────────────────────────┘             └──────────────────────────┘
```

As long as you have a single interface spanning two processes, you can use it to
indefinitely expand the number of interfaces between them, by passing unbound
`MessagePipe`s over an existing interface:

```cpp
mage::Remote<magen::BoostrapInterface> remote = /* ... */;

// Create two entangled message pipes, and send one (for use as a receiver) to
// the remote process.
std::vector<mage::MessagePipe> new_pipes = mage::CreateMessagePipes();

remote->SendCompositorReceiver(new_pipes[1]);
mage::Remote<magen::Compositor> compositor_remote(new_pipes[0]);

// Now you can start invoking methods on the remote compositor, and they'll
// arrive in the remote process once the receiver gets bound.
compositor_remote->StartCompositing();
```

## Mage invitations

The above sections are great primers on how to get started with Mage, but they
assume you already have an initial connection spanning two processes in your
system. From there, you can send messages, and even expand the number of
connections that span the two processes. But **how do you establish the initial
connection**? This section introduces the Mage "invitation" concept, which helps
you do this.

There are three public APIs for establishing the initial message pipe connection
across two processes:
 - **Called in every process using Mage**: `mage::Init()`. Performs
   routine setup, and must be called before any other Mage APIs are called
 - **Called from the parent process[^2]**:
   `mage::SendInvitationAndGetMessagePipe(int socket)`
 - **Called from the child process**:
   `mage::AcceptInvitation(int socket, std::function<void(MessagePipe)> callback)`

First, the parent process must create a native socket pair that the child
process will inherit upon creation. The parent passes the pipe into the
`mage::SendInvitationAndGetMessagePipe()` API in exchange for a
`mage::MessagePipe` that's wired up with the Mage internals. This pipe can
immediately be bound to a remote and the parent can start sending messages that
will eventually be received by the child. (It can just as well be used as a
receiver).

```cpp
int fd = /* ... */;
mage::MessagePipe pipe = mage::SendInvitationAndGetMessagePipe(fd);
mage::Remote<magen::BoostrapInterface> remote(pipe);
remote->MyMessageHere("payload!");
```

Child process initialization is a little more involved. The process inherits all
of the parent's sockets, but it needs to know which one to use for the
invitation. This is typically communicated via an argument passed to the child
binary when the parent launches it.

When the child recovers the native socket, from any thread it can call
`mage::AcceptInvitation(socket, callback)` to accept an invitation on the
socket. This API takes a callback that runs on the same thread that called
`AccceptInvitation()`, after the invitation gets processed on the IO thread. The
callback gives the child a `mage::MessagePipe` that's connected to the parent
process and ready for immediate use. Here's the typical flow:

```cpp
void OnInvitationAccepted(mage::MessagePipe receiver_pipe) {
  CHECK_ON_THREAD(base::ThreadType::UI);
  first_interface = std::make_unique<FirstInterfaceImpl>(receiver_pipe);
}

int main(int argc, char** argv) {
  std::shared_ptr<base::TaskLoop> main_thread = base::TaskLoop::Create(base::ThreadType::UI);
  base::Thread io_thread(base::ThreadType::IO);
  io_thread.Start();
  io_thread.GetTaskRunner()->PostTask(main_thread->QuitClosure());
  main_thread->Run(); // Wait for the IO thread to get set up.

  mage::Init();

  CHECK_EQ(argc, 2);
  int fd = std::stoi(argv[1]);
  mage::AcceptInvitation(fd, &OnInvitationAccepted);

  // This will run the event loop indefinitely, running tasks when they are
  // posted (including the `OnInvitationAccepted()` function above.
  main_thread->Run();
  return 0;
}
```


## Design limitations

See [docs/design_limitations.md].


## Security considerations

See [docs/security.md].


[^1]: Well, technically it is only Linux & macOS for now 😔. Windows support
will be coming.
[^2]: In practice, it doesn't matter who of the parent and child is the
invitation "sender" vs "acceptor", though it is perhaps more natural to think of
the parent process as the one inviting the child to the network of processes,
via a Mage invitation.

[Mojo IPC library]: https://chromium.googlesource.com/chromium/src/+/master/mojo/README.md
[Dominic Farolino]: https://github.com/domfarolino
[`//base`]: https://github.com/domfarolino/browser/tree/master/base
[Bazel]: https://bazel.build/
[ipc_idl]: https://github.com/darinf/ipc_idl
[ports]: https://github.com/darinf/ports
[IDL]: https://en.wikipedia.org/wiki/Interface_description_language
[Mojo Core Ports Overview]: https://docs.google.com/document/d/1PaQEKfHi8pWifyiHRplnYeicjfjRuFJSMe3_zlJzhs8/edit
[ipcz]: https://github.com/krockot/ipcz
[docs/security.md]: docs/security.md
[docs/design_limitations.md]: docs/design_limitations.md
