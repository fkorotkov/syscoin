*After branching off for a major version release of Syscoin Core, use this
template to create the initial release notes draft.*

*The release notes draft is a temporary file that can be added to by anyone. See
[/doc/developer-notes.md#release-notes](/doc/developer-notes.md#release-notes)
for the process.*

*Create the draft, named* "*version* Release Notes Draft"
*(e.g. "4.1.3 Release Notes Draft"), as a collaborative wiki in:*

https://github.com/syscoin-core/syscoin-devwiki/wiki/

*Before the final release, move the notes back to this git repository.*

*version* Release Notes Draft
===============================

Syscoin Core version *version* is now available from:

  <https://syscoincore.org/bin/syscoin-core-*version*/>

This release includes new features, various bug fixes and performance
improvements, as well as updated translations.

Please report bugs using the issue tracker at GitHub:

  <https://github.com/syscoin/syscoin/issues>

To receive security and update notifications, please subscribe to:

  <https://syscoincore.org/en/list/announcements/join/>

How to Upgrade
==============

If you are running an older version, shut it down. Wait until it has completely
shut down (which might take a few minutes for older versions), then run the
installer (on Windows) or just copy over `/Applications/Syscoin-Qt` (on Mac)
or `syscoind`/`syscoin-qt` (on Linux).

Upgrading directly from a version of Syscoin Core that has reached its EOL is
possible, but it might take some time if the datadir needs to be migrated. Old
wallet versions of Syscoin Core are generally supported.

Compatibility
==============

Syscoin Core is supported and extensively tested on operating systems using
the Linux kernel, macOS 10.12+, and Windows 7 and newer. It is not recommended
to use Syscoin Core on unsupported systems.

Syscoin Core should also work on most other Unix-like systems but is not
as frequently tested on them.

From Syscoin Core 4.1.3 onwards, macOS versions earlier than 10.12 are no
longer supported. Additionally, Syscoin Core does not yet change appearance
when macOS "dark mode" is activated.

In addition to previously supported CPU platforms, this release's pre-compiled
distribution provides binaries for the RISC-V platform.

Notable changes
===============

Credits
=======

Thanks to everyone who directly contributed to this release:


As well as to everyone that helped with translations on
[Transifex](https://www.transifex.com/syscoin/syscoin/).
