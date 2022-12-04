# Security considerations

The boundary between less-privileged processes and more-privileged processes is
crucial to audit when determining what capabilities or information is shared
across processes. Since this boundary is largely defined by the IPC
implementation being used[^1], IPC libraries tend to care a lot about security.

This is particularly important because less-privileged processes might be
compromised by some sort of application code, so the protection of
more-privileged process depends on it being safe from whatever
possibly-malicious content a less-privileged one might try and throw at it.
Some great documentation about protecting against this can be found in the
Chromium source tree about [compromised renderer processes].

A non-exhaustive list of security considerations that an IPC library might have
are:

 1. A less-privileged process should never be able to terminate a
    more-privileged one
    - Giving control of the lifetime a privileged process to a compromised one
      could result in the corruption of user data, an unusable application, or
      other harmful side effects
 1. The library should validate deserialized data
    - A compromised process may write arbitrary data to a message that breaks
      the data serialization rules in hopes of triggering undefined behavior in
      a more-privileged receiver of the message. The deserialization code in the
      target process is responsible for _safely_ validating messages it receives
      before handing them to user code. When the library catches invalid data in
      a received message, it should attempt to kill the process that sent the
      message, and refuse to pass the message on to the usual receiving code
 1. IPC libraries often have the concept of a single "master" process that is
    responsible for brokering communication between other processes that don't
    otherwise have the ability to communicate on their own. In Chromium, this is
    literally called [the "Broker" process]. This is important especially in
    sandboxed environments where less-privileged processes can't do many things
    (including spawn child process) and require the Broker to grant powerful 
    capabilities
 1. Much more...

As it stands right now, Mage is mostly written as a proof-of-concept, for use in
applications  but not audited to uphold the security guarantees (including the
ones above) that might be required by major, critical applications in
production. It would be great if Mage could get to this point, and it's
certainly within reach, but right now Mage has not been designed for these
purposes.

# Writing safe IPCs

Not all IPC security can be taken care of by an IPC library. Much care needs to
be taken in how interfaces are defined across processes, so that it is not
possible to introduce new, subtle security bugs manually. Here are a list of
good security pointers to keep in mind when writing IPCs, especially those that
communicate across privilege gradients:

 - https://chromium.googlesource.com/chromium/src/+/HEAD/docs/security/mojo.md
 - https://www.chromium.org/Home/chromium-security/education/security-tips-for-ipc/
 - https://wiki.mozilla.org/Security/Sandbox/IPCguide
 - https://mozilla.github.io/firefox-browser-architecture/text/0013-ipc-security-models-and-status.html#best-practices


[^1]: Another tool that contributes to defining the process boundary for an
application is whatever sandboxing library is being used, if any. Here are some
examples of open source ones: [openjudge/sandbox], [Chromium sandbox],
[google/sandboxed-api].

[compromised renderer processes]: https://chromium.googlesource.com/chromium/src/+/main/docs/security/compromised-renderers.md
[the "Broker" process]: https://chromium.googlesource.com/chromium/src/+/master/mojo/core/README.md#:~:text=The%20Broker%20has%20some%20special%20responsibilities
[openjudge/sandbox]: https://github.com/openjudge/sandbox
[Chromium sandbox]: https://chromium.googlesource.com/chromium/src/+/master/docs/design/sandbox.md
[google/sandboxed-api]: https://github.com/google/sandboxed-api
