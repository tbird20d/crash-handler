This is Tim Bird's experimental crash_handler program.

It operates very similarly to Android's debuggerd, but requires
no persistently-running process.  Much of this code came, originally,
from debuggerd.  It uses /proc and ptrace calls to gather information
about the dying process.  It records the information into a crash
report, and also records summary information into a crash journal.

Note that this program is currently only designed to work on ARM processors,
running the Linux operating system.

The project home page is at:

http://elinux.org/Crash_handler

== Building ==
To build this code, type 'make'.

If you are cross-compiling, then first set the CROSS_COMPILE environment
variable, to contain a toolchain prefix.  This should include the
trailing dash that is customary for a toolchain prefix.  For example:

 $ export CROSS_COMPILE=arm-sony-linux-gnueabi-armv7a-dev-

Your toolchain prefix is likely to be much simpler than this, and may
be something like: 'arm-eabi-' or  'arm-unknown-linux-gnu-'

== Installation ==
You must be root to install the crash_handler.

To install this code, copy it to some location on the target,
and type:
 $ ./crash_handler install

By convention, I install this binary in /usr/bin, but it can be placed
anywhere.  The crash journal is created in /tmp, and crash reports are
created in /tmp/crash_reports

The 'make install' command uses an internal Sony tool called ttc to
perform this installation.  This won't work for you.  You can change the
'install' target in the Makefile to the following line:

 install: default_install
