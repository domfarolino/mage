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

----

### Table of contents

- [Supported platforms](#supported-platforms)
- [Overview](#overview)
  - [`mage::Remote<T>`](#mageremotet)
  - [`mage::Receiver<T>`](#magereceivert)
- [Magen Interface Definition Language (IDL)](#magen-interface-definition-language-idl)
- [Using Mage in your application](#using-mage-in-your-application)
  - [1. Write your interface in a `.magen` file](#1-write-your-interface-in-a-magen-file)
  - [2. Build your `.magen` file](#2-build-your-magen-file)
  - [3. Implement your interface in C++](#3-implement-your-interface-in-c)
  - [4. Use a `Remote` to send cross-process IPCs](#4-use-a-remote-to-send-cross-process-ipcs)
  - [Sending `MessagePipes` cross-process](#sending-messagepipes-cross-process)
- [Mage invitations](#mage-invitations)
- [Threading & task scheduling](#threading--task-scheduling)
- [Platform support](#platform-support)
- [Building and running the tests](#building-and-running-the-tests)
  - [Debugging](#debugging)
- [Design limitations](#design-limitations)
- [Security considerations](#security-considerations)

----


## Supported platforms

![Linux](./assets/linux.svg)
![macOS](./assets/apple.svg)

In progress:

![Windows](./assets/windows.svg)


## Overview

Mage IPC allows you to seamlessly send asynchronous messages to an object that
lives in another process, thread, or even the same thread, without the sender
having to know anything about where the target object actually is. Messages are
described by user-provided interface files.

To get started, you need to be familiar with three concepts from the [public
API]:
 - `mage::MessagePipe`
 - `mage::Remote<T>`
 - `mage::Receiver<T>`

Messages are sent over bidirectional message pipes, each end of which is
represented by a `mage::MessagePipe`, which can be passed over interfaces, even
to other processes. Given a pair of entangled `MessagePipe`s, you'll ultimately
bind one end to a `Remote` and the other to a `Receiver`. It is through these
objects that arbitrary user messages get passed as IPCs.

### `mage::Remote<T>`

Once bound, a `Remote<magen::Foo>` represents a local "proxy" for a `magen::Foo`
interface, whose concrete implementation may live in another process. You can
synchronously invoke any of the `magen::Foo` interface methods on a
`Remote<magen::Foo>`, and the proxy will forward the message to the right place,
wherever the corresponding `Receiver<magen::Foo>` lives, even if it's moving
around.

```cpp
mage::MessagePipe remote_pipe = /* get pipe from somewhere */;
mage::Remote<magen::Foo> remote(remote_pipe);

// Start sending IPCs!
remote->ArbitraryMessage("some payload");
```

### `mage::Receiver<T>`

Messages sent over a bound `Remote<magen::Foo>` get queued on the other end's
`MessagePipe` until _it_ is bound to a corresponding `Receiver<magen::Foo>`,
which represents the concrete implementation of the Mage interface `magen::Foo`.
The receiver itself does not handle messages sent by the remote, but rather it
has a reference to a user-provided C++ object that implements the interface, and
it forwards messages to it. Receivers are typically owned by the concrete
implementation of the relevant interface.

Here's an example:

```cpp
// Instances of this class can receive asynchronous IPCs from other processes.
class FooImpl final : public magen::Foo {
 public:
  Bind(mage::MessagePipe foo_receiver) {
    // Tell `receiver_` that `this` is the concrete implementation of
    // `magen::Foo` and its interface methods.
    receiver_.Bind(foo_receiver, this);
  }

  // Implementation of `magen::Foo`. These methods get invoked by `receiver_`
  // when IPCs come in from the remote.
  void ArbitraryMessage(string) { /* ... */ }
  void AnotherIPC(MessagePipe) { /* ... */ }

 private:
  // The corresponding remote may live in another process.
  mage::Receiver<magen::Foo> receiver_;
};
```


## Magen Interface Definition Language (IDL)

Magen is the [IDL] that describes Mage interfaces. Interfaces are written in
`.magen` files by consumers of Mage, and are understood by the `magen_idl(...)`
Bazel rule which generates C++ bindings for the interfaces.

The Magen IDL is quite simple (and much less feature-rich than Mojo's IDL). Each
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


## Using Mage in your application

Using Mage to provide IPC support in an application is pretty simple; there are
only a few steps:

 1. Write your interface in a `.magen` file
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

### 1. Write your interface in a `.magen` file

The first thing you need to do is write the Magen interface that `main.cc` will
use to send messages to the network process. This includes a `FetchURL` IPC that
contains a URL. Magen interfaces are typically defined in a `magen/` directory:

```cpp
// network_process/magen/network_process.magen

interface NetworkProcess {
  FetchURL(string url);
}
```

### 2. Build your `.magen` file

Next, you need to tell your `BUILD` file about the interface in your `.magen`
file, so it can "build" it (generate the requisite C++ code). `magen/`
directories get their own `BUILD` files that invoke the Mage build process.

```starlark
# network_process/magen/BUILD

load("@mage//mage/public/parser:magen_idl.bzl", "magen_idl")

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
+    "@mage//mage/public",
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
```

```cpp
// network/network_process.cc

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
  mage::MessagePipe network_pipe = /* obtained from creating the network process */;
  mage::Remote<magen::NetworkProcess> remote(network_pipe);

  while (true) {
    std::string url /* get user input */;
    remote->FetchURL(url);
  }
  return 0;
}
```

### Sending `MessagePipes` cross-process

The previous sections illustrate sending a message over a bound message pipe to
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
mage::Remote<magen::BoostrapInterface> bootstrap = /* ... */;

// Create two entangled message pipes: one for us, one for the remote process.
std::vector<mage::MessagePipe> new_pipes = mage::CreateMessagePipes();
bootstrap->SendCompositorReceiver(new_pipes[1]);

mage::Remote<magen::Compositor> compositor_remote(new_pipes[0]);
// Now we can start invoking methods on the remote compositor, and they'll
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

## Threading & task scheduling

Since Mage is multithreaded and inherently asynchronous, it has some threading
and scheduling requirements. While Mage is in ["MVP" mode], it has a hard
dependency on the external [`//base`] library that was originally developed
alongside Mage. That means _right now_, in order to use Mage you have to use
[`//base`] as your application's primary threading and scheduling library.

**This is only temporary**: work is being done to decouple the two with the end
goal of being able to use Mage in any application that provides a sufficient
implementation for these generic requirements. Some good implementations of
these requirements could be:
 - [`//base`]
 - [concurrencpp](https://github.com/David-Haim/concurrencpp/)
 - Facebook's [libunifex](https://github.com/facebookexperimental/libunifex)
 - Perhaps other libraries from: https://github.com/topics/asyncio?l=c%2B%2B

A task scheduling library must also support the ability to asynchronously listen
to I/O from native platform sockets, such as Unix file descriptors or Windows
HANDLEs. All of this is provided by default in [`//base`], which was developed
with Mage in mind.

Here's a complete list of threading/scheduling requirements/APIs an external
application would have to satisfy to use Mage successfully:

 - API to retrieve a thread-local reference to the current thread's task-posting
   sink (that can be stored)
    - Currently provided by `base::GetCurrentThreadTaskRunner()`
    - Must support cross-thread task posting
 - API to retrieve a reference to the task-posting sinks for other threads, from
      any thread
    - Currently provided by other handles in
      `//base/scheduling/scheduling_handles`
 - API to "Watch" and "Unwatch" a native socket, and get asynchronously notified
   when data is available to read from it
    - Currently provided by `base::TaskLoopForIO`, which has `(Un)Watch()`
      methods that tell that underlying task loop which object to post a
      notification message to when data is available to read on the relevant
      socket
 - API/Macro to tell what thread you're running on (UI or IO)
    - Currently provided by `CHECK_ON_THREAD()` via
      `//base/threading/thread_checker.h`
 - API allowing an object to repeatedly determine if current execution is
   happening on the thread the object was constructed on (this is slightly
   different from the requirement immediately above)
     - Currently provided by `base::ThreadChecker`
     - Not a strict requirement — if the above is satisfied, we could re-write
       `ThreadChecker` to just "save" the name of the constructor's thread for
       later querying against the current thread

## Platform support

Since Mage is in ["MVP" mode] right now, it only supports Linux and macOS.
Windows support for Mage and the [`//base`] library are currently underway.

## Building and running the tests

With the repository downloaded, to build and run the demo, run:

```sh
$ bazel build mage/demo/parent mage/demo/child
$ ./bazel-bin/mage/demo/parent
```

To run the tests, run one of the following:

```sh
$ bazel test mage/mage_tests
```

or...


```sh
$ bazel build mage/mage_tests
$ ./bazel-bin/mage/mage_tests
```

### Debugging

Mage is built with debugging symbols by default (see [`.bazelrc`](.bazelrc)). To
debug a failing test or other internals with `lldb`, run:

```sh
$ bazel build mage/mage_tests
$ lldb ./bazel-bin/mage/mage_tests
# Set breakpoints
$ br s -n Node::SendMessage
$ br s -f mage_test.cc -l <line_number>
$ run --gtest_filter="MageTest.TestFoo"
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
[public API]: mage/public
["MVP" mode]: https://en.wikipedia.org/wiki/Minimum_viable_product
[docs/security.md]: docs/security.md
[docs/design_limitations.md]: docs/design_limitations.md
