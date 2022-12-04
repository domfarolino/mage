# Design limitations

Mage is currently in something approximating ["MVP" mode], and as such, it has
several nontrivial design limitations for the time being. This section covers
them.

## No proxy collapsing

When you send a `mage::MessagePipe` to a remote process, under the hood, the
internal `mage::Endpoint` that represents the sent pipe gets put into the
`Endpoint::State::kUnboundAndProxying` state, and will forward messages any
messages is receives to the concrete endpoint in the remote process. Now you can
imagine this is repeated several times, and the "concrete" cross-process
endpoint that is receiving forwarded messages from a proxy is _itself_ sent to
another process, and put into the proxying state. We now have a chain like so:

```
┌──────────────────────────┐      ┌────────────┐      ┌────────────┐      ┌────────────┐
│         A                │      │     B      │      │     C      │      │     D      │
│                          │      │            │      │            │      │            │
│ remote       receiver────│─────►│────pipe────│─────►│────pipe────│─────►│──receiver  │
│   │         (proxying)   │      │ (proxying) │      │ (proxying) │      │            │
│   └──────►───────┘       │      │            │      │            │      │            │
└──────────────────────────┘      └────────────┘      └────────────┘      └────────────┘
```

This means that every message sent on `A`'s `remote` effectively gets copied
several times before it makes it to the ultimate receiver `D`. We should be able
to collapse this chain of proxies once they are all done forwarding their queued
messages to their targets, and end up with something much more direct:

```
┌──────────────┐  ┌───────┐  ┌───────┐  ┌────────────┐
│      A       │  │   B   │  │   C   │  │     D      │
│              │  │       │  │       │  │            │
│ remote       │  └───────┘  └───────┘  │  receiver  │
│   │          │                        │     ▲      │
│   │          │                        │     │      │
└───│──────────┘                        └─────│──────┘
    └─────────►──────────────────►────────────┘
```

In order to achieve this much more efficient end state, we require two things:
  1. Proxy collapsing control messages: a set of control messages that a proxy
     can send back to the endpoint sending messages to it, telling it to stop,
     and instead send messages to the proxy's target directly. This is a pretty
     complicated back and forth dance, and it gets really complicated with lots
     of proxies in a chain. I haven't thought about what messages exactly would
     need to be involved or what the exact flow would be. Chromium's Mojo
     handles this case (it has to for performance), so when/if the time comes
     for Mage to handle this, we could draw inspiration from Mojo.
  1. The ability for process A to read a native socket for D that it did not
     have when it started up. See the next section below

## No ability to send native sockets across processes

Having the ability to send native, platform-specific sockets across processes
would help unblock the proxy collapsing use case above, but it would also enable
other things that are currently not possible with the current version of Mage.
In the above example, a wields `A` remote whose ultimate receiver (past the
chain of proxies) is `D`. But what instead of `A` passing a receiver deep
through the process chain, it decided that _it_ was the receiver, and it passed
a remote through the other processes? That would give us something like this:

```
┌──────────────────────────┐      ┌────────────┐      ┌────────────┐      ┌────────────┐
│         A                │      │     B      │      │     C      │      │     D      │
│                          │      │            │      │            │      │            │
│ receiver     ex-remote───│◄─────│────pipe────│◄─────│────pipe────│◄─────│───remote   │
│   │         (proxying)   │      │ (proxying) │      │ (proxying) │      │            │
│   └──────◄───────┘       │      │            │      │            │      │            │
└──────────────────────────┘      └────────────┘      └────────────┘      └────────────┘
```

This is currently impossible to achieve in the current version of Mage. That's
because whenever we pass a message pipe to another process (the `ex-remote` in
the case above, and all of its cross-process instances that get passed down to
`D`), the underlying `mage::Endpoint` keeps its _original_ peer (which is the
address `A:receiver`). That means in practice, we actually end up with the
following:

```
┌──────────────────────────┐      ┌────────────┐      ┌────────────┐      ┌────────────┐
│         A                │      │     B      │      │     C      │      │     D      │
│                          │      │            │      │            │      │            │
│ receiver     endpoint────│─────►│────pipe────│─────►│────pipe────│─────►│───remote   │
│   │  │      (proxying)   │      │ (proxying) │      │ (proxying) │      │     │      │
│   ▲  └───►───────┘       │      │     │      │      │     │      │      │     │      │
└───│──────────────────────┘      └─────▼──────┘      └─────▼──────┘      └─────▼──────┘
    │                                   │                   │                   │
    └──────◄───(peer)───◄───────────────┘                   │                   │
    └──────◄───(peer)───◄───────────────────────────────────┘                   │
    └──────◄───(peer)───◄───────────────────────────────────────────────────────┘
```

The underlying endpoints/pipes don't know whether they are remotes or receives,
and it shouldn't matter to them. All they know is that they have two addresses:
 - Proxy address, which gets populated when the endpoint/pipe is sent to another
   process
 - Peer address, which as of now never changes.

That's why the endpoint `A:receiver` above never changes its peer address, so
that its peer can proxy things to the right place without `receiver` ever
needing to know about this. Unfortunately that means all of the endpoints in the
other processes are now directly referencing `A:receiver`, when they might not
know anything about the process `A` at all, and have no `mage::Channel` that can
speak directly to it. If `D` tried to send a message on its remote, **it would
crash**.

There are two different solutions to this problem:

 1. Whenever any process is spun up, automatically give it a socket to the
    "master" or "Broker" process in the system, which is `A` here. That means
    all of the other process, no matter who their parents were above, would have
    a direct socket connection to `A` and the scenario above would just work.
    There is a limitation to this though. Imagine if if it was `B` that created
    the remote/receiver pair, passing the remote all the way down to `D`. In
    that case, `D` would have a direct connection to `A`, but not `B` and
    **would still crash**. The solution there would be to add control messages
    that make it possible for `D` to ask the master process for an introduction
    to `B`, which it knows about but can't talk to directly. `A` would respond
    to `D` with a native socket/handle to `B`, and then `D` could flush all of
    its queued messages to `B` at last. We'd have to do the same for all proxies
    in the chain here, which means messages could arrive in `B` out of order,
    but that could be fixed by just maintaining message sequence numbers, which
    is some extra minor bookkeeping. This codifies the concept of a
    master/broker node into Mage, which is probably the right direction anyways.
    For example, if `B` created `C`, and `C` created `D`, then by the time `D`
    asks `A` (the master node) for an introduction to another node in the
    system, `A` might not know about it yet because it is not responsible for
    creating all processes in the system. But if we go with this solution here,
    then we'd want to force `A` (the master node) to be responsible for creating
    all of the processes in the system so that it always synchronously knows
    about every process, in case it has to perform an introduction between two
    other processes.
 2. Upon sending an endpoint to another process, when we set the to-be-sent
    endpoint into the "proxying" state, we could **also** change the remote
    endpoint's peer address to be the newly-proxied endpoint. That would
    essentially change the system to be:

```
┌──────────────────────────┐       ┌────────────┐       ┌────────────┐       ┌────────────┐
│         A                │       │     B      │       │     C      │       │     D      │
│                          │(proxy)│            │(proxy)│            │(proxy)│            │
│ receiver────►endpoint────│──────►│────pipe────│──────►│────pipe────│──────►│───remote   │ (proxy arrows ►)
│      ▲      (proxying)   │       │    ▼ ▲     │       │    ▼ ▲     │       │     │      │
│      │ (peer)   ▼ ▲      │(peer) │    │ │     │(peer) │    │ │     │(peer) │     │      │
│      └───◄──────┘ └──────│◄──────│────┘ └─────│◄──────│────┘ └─────│◄──────│─────┘      │ (peer arrows ◄)
└──────────────────────────┘       └────────────┘       └────────────┘       └────────────┘
```

In other words, by updating all "peer" addresses (of endpoints being sent
cross-process) to point to the endpoint _that just did the sending_, we've
created a fully circular loop among all pipes in the system. This is important
because even though we have `A:receiver` and `D:remote`, they could just as
easily be `A:remote` and `D:receiver`, or even unbind and switch throughout
their lifetime, so every single endpoint/pipe in the network must be capable of
sending messages in both directions:
 - Proxy direction (left to right)
 - Peer direction (right to left)

But notice that this creates a serious issue. If `C:pipe` has the ability to
send messages in either direction (either towards `D` directly, or towards `A`
via the the intermediary `B` that it knows about), it now needs some
directionality information associated with each message it receives. For
example, if `D` sends a message that is bound for `A` ultimately, and it makes a
pit stop in `C`, `C` must somehow know whether to forward the message through
the proxy direction (back to `D`) or through the peer direction (to `B`). This
would need to be codified in every message, which is weird.

Also this solution is only a workaround for the fact that we can't send native
sockets (so that we never end up with the scenario where e.g., `D` needs to send
a message to `A` directly but has never heard of `A`). But we have to support
sending native sockets some day anyway, so this solution is not worth
implementing. It would allow us to unlock the above scenario before we support
sending native sockets across processes, but its implementation cost might be
just as large as sending native sockets across processes in the first place, so
it would probably be a waste. In any case, it would not take Mage in the right
direction, so we're not going with it.


["MVP" mode]: https://en.wikipedia.org/wiki/Minimum_viable_product
