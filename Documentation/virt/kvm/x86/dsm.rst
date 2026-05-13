.. SPDX-License-Identifier: GPL-2.0

.. _dsm:

=====================================
Distributed Shared Memory (DSM) for KVM
=====================================

Overview
========

KVM DSM (Distributed Shared Memory) is a feature that allows a single
virtual machine to span multiple physical host machines.  Each host runs
a KVM instance with ``CONFIG_KVM_DSM=y``, and DSM coordinates memory
access across nodes so that all instances present a unified view of guest
physical memory to the guest.

The primary use case is running "Giant VMs" -- single operating system
images that require more resources (CPUs, memory) than any single
physical machine can provide.

Architecture
============

::

+---------------------------------------------------+
|                     Guest OS                      |
+-------------------------+-------------------------+
                          |
+-------------------------V-------------------------+
|                       Hosts                       |
|                                                   |
|       +-----------------------------------+       |
|       |     Distributed Shared Memory     |       |
|       +----+-------------------------+----+       |
|            |                         |            |
|            V                         V            |
|  +-------------------+     +-------------------+  |
|  |       QEMU        |     |       QEMU        |  |
|  |  +-------------+  |     |  +-------------+  |  |
|  |  |    vCPU     |  |     |  |    vCPU     |  |  |
|  |  +-------------+  |     |  +-------------+  |  |
|  |  |  IO device  |  |     |  |  IO device  |  |  |
|  |  +-------------+  |     |  +-------------+  |  |
|  +-------------------+     +-------------------+  |
|  |      Host OS      |     |      Host OS      |  |
|  |  +-------------+  |     |  +-------------+  |  |
|  |  |     KVM     |  |     |  |     KVM     |  |  |
|  |  +-------------+  |     |  +-------------+  |  |
|  +-------------------+     +-------------------+  |
|  |     Hardware      |     |     Hardware      |  |
|  +-------------------+     +-------------------+  |
|         Node 0                    Node 1          |
+---------------------------------------------------+

DSM implements a page-level distributed shared memory protocol.  When
a guest accesses a page that is owned by a remote node, the local KVM
instance sends a request to fetch or acquire the page, applies coherence
protocol operations, and returns the data to the guest.

The Ivy consistency protocol provides multi-writer coherence using
invalidation-based coherence with version tracking and copyset management.

Kconfig Options
===============

The DSM feature is controlled by the following Kconfig options:

- ``CONFIG_KVM_DSM_IRQ_FORWARD``: Interrupt forwarding support.  Depends on
  ``KVM`` and ``KVM_INTEL``.  Allows IPIs and APIC register writes to be
  forwarded to the host that owns the target vCPU, enabling interrupt delivery
  in a distributed setting.  This option can be enabled independently of
  ``CONFIG_KVM_DSM``.

- ``CONFIG_KVM_DSM``: Main DSM feature.  Depends on
  ``CONFIG_KVM_DSM_IRQ_FORWARD`` and ``INET``.  Provides the core distributed
  shared memory framework.

Userspace API
=============

DSM is controlled via ioctls on the VM file descriptor (``/dev/kvm/<vm_fd>``).
All DSM ioctls require the ``KVM_CAP_X86_DSM`` capability.  Ioctls related to
distributed shared memory (``KVM_DSM_ENABLE``, ``KVM_DSM_MEMCPY``,
``KVM_DSM_MEMPIN``) require ``CONFIG_KVM_DSM=y``.  Ioctls related to interrupt
forwarding (``KVM_DSM_IPI``, ``KVM_DSM_X2APIC``, ``KVM_DSM_APIC_BASE``) require
``CONFIG_KVM_DSM_IRQ_FORWARD=y``.

Capability Check
----------------

Before using DSM ioctls, userspace must verify support::

  int kvm_fd = open("/dev/kvm", O_RDWR);
  int vm_fd = ioctl(kvm_fd, KVM_CREATE_VM, 0);

  /* Check DSM capability */
  kvm_check_extension(kvm_fd, KVM_CAP_X86_DSM);

Enabling DSM
---------------

::

  struct kvm_dsm_params params = {
      .dsm_id = 0,              /* Node ID within the DSM cluster */
      .cluster_iplist_len = N,    /* Number of cluster nodes */
      .cluster_iplist = (void __user *)iplist,
  };
  ioctl(vm_fd, KVM_DSM_ENABLE, &params);

After ``KVM_DSM_ENABLE``, the VM enters DSM mode.  Memory slots
registered via ``KVM_SET_USER_MEMORY_REGION2`` are automatically
tracked by DSM.

Memory Copy with DSM
------------------------

::

  struct kvm_dsm_memcpy args = {
      .write = 1,
      .host_virt_addr = src,
      .userspace_addr = dst_gpa,
      .length = size,
  };
  ioctl(vm_fd, KVM_DSM_MEMCPY, &args);

Pinning Memory
---------------

::

  struct kvm_dsm_mempin args = {
      .write = 0,
      .unpin = 0,
      .host_virt_addr = hva,
      .length = size,
  };
  ioctl(vm_fd, KVM_DSM_MEMPIN, &args);

IPI Forwarding
---------------

::

  struct kvm_dipi_params args = {
      .vcpu_id = target_vcpu,
      .val = icr_low,
      .val2 = icr_high,
      .dest_id = dest,
  };
  ioctl(vm_fd, KVM_DSM_IPI, &args);

X2APIC Write Forwarding
--------------------------

::

  struct kvm_x2apic_params args = {
      .vcpu_id = target_vcpu,
      .data = msr_value,
  };
  ioctl(vm_fd, KVM_DSM_X2APIC, &args);

APIC Base MSR Forwarding
--------------------------

::

  struct kvm_apic_base_params args = {
      .vcpu_id = target_vcpu,
      .host = 1,
      .index = APIC_BASE_MSR,
      .data = msr_value,
  };
  ioctl(vm_fd, KVM_DSM_APIC_BASE, &args);

Exit Reasons
------------

When an interrupt needs to be forwarded from one host to another, the
vCPU run loop exits to userspace so it can relay the request.  This
requires ``CONFIG_KVM_DSM_IRQ_FORWARD=y``.

- ``KVM_EXIT_DSM_SEND_IRQ``: An IPI or APIC ICR write needs to be
  forwarded to the host that owns the target vCPU.  The ``lapic_irq``
  field in ``struct kvm_run`` contains the vCPU id, ICR value, and
  destination.
