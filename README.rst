
rt-can-test
===========

Overview
++++++++

The main purpose of rt-can-test is to provide an easy way to test the
performance of the Linux PREEMPT_RT patches together with CAN.

In many automotive applications, a node on a CAN network is expected to
deliver data at fixed time intervals. Sometimes the timing of the CAN
messages is very important for the functionality of the system.

Since a standard Linux kernel is not a real time system, there is risk of
latency spikes in the CAN bus traffic. These latency spikes can potentially
make the system fail. The latency spikes might not be caused by the CAN
subsystem at all, as any subsystem that causes kernel preemption to be disabled
can block the CAN threads from execution.

rt-can-test strives to implement all the recommendations for user space
real time applications on a PREEMPT_RT system (this essentially means taking
measures in order to avoid page faults after all threads have been created).

In average, rt-can-test will have the same performance (if not even better)
when running on a standard Linux kernel, but the worst case will be better
with a real time kernel.

The program uses the Linux socket CAN API (there is a small wrapper interface
defined by *can.h*), and thus it has no dependencies to any external libraries.

Usage
+++++

To run a TX test, follow the below steps:

1. Connect the CAN bus of the device to some kind of CAN-dongle (like a Vector
   CANCase or similar) and start recording.
2. Configure the CAN interface of the device using the *ip* tool.
   See `Configure CAN interface`_ for more info.
3. Launch rt-can-test (see below).

A transmit example command can look like this:

::

	rt-can-test --if can0 --tx 123#1122334455667788 -i 100000

The format of the TX message is identical to the format used by the *cansend*
tool from *can-utils*.

``-i`` is the CAN frame interval in microseconds. Thus, the above example
will transmit CAN frames every 100 ms (10 Hz)

``--if`` is the CAN interface we will send on (*can0* in the example)

``--tx`` must be followed by the CAN frame to send. In the example, a CAN
frame with ID 0x123 and data 1122334455667788 will be transmitted.

An RX test might look like this:

::

	rt-can-test --if can0 --rx

All received messages will be printed to stdout together with a time stamp.

It is important that the signal source (typically a CAN-dongle) transmits
without any jitter.

Configure CAN interface
-----------------------

Use the tool *ip* from *iproute2* and make sure the bitrate matches the
CAN-dongle configuration:

::

	ip link set can0 type can bitrate 500000
	ip link set up can0

Build
+++++

Most likely you would want to cross compile the tool since not that many
x86 PC's have native CAN interfaces.

Clone this repo, cd into the working dir and cross compile:

::

	make CC=/path/to/my/cross-gcc

In case an explicit ``--sysroot`` is needed (the cross compiler was built
without the ``--with-sysroot`` option), it can be passed with the **EXTRA_CFLAGS**
variable:

::

	make CC=/path/to/my/cross-gcc EXTRA_CFLAGS="--sysroot /path/to/cross/sysroot"
