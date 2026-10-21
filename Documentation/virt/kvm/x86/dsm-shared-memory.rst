.. SPDX-License-Identifier: GPL-2.0

Shared-memory interrupt forwarding
=================================

This series lets two QEMU processes on one host run disjoint vCPU sets
while mapping one shared guest RAM file. Enable
``CONFIG_KVM_DSM_IRQ_FORWARD`` and use QEMU
``shared-memory/v10.1.2-linux-v7.3``. Kernel TCP DSM memory management is
not part of this series.

Userspace interface
-------------------

Check ``KVM_CAP_X86_DSM_IRQ_FORWARD`` (253) using
``KVM_CHECK_EXTENSION`` and enable it on the VM fd using
``KVM_ENABLE_CAP`` with zero arguments. The VM then inhibits APICv and
exits to userspace before delivering intercepted ICR writes.

``KVM_EXIT_DSM_IRQ_FORWARD`` (44) returns the ``dsm_irq_forward`` member
of ``struct kvm_run``. It contains the source vCPU ID, a type, register
and value fields, destination ID, and the 64-bit x2APIC value. Types 1
and 2 identify xAPIC and x2APIC ICR writes. Userspace must inject both
local and remote delivery using the matching forwarding ioctls.

``KVM_DSM_IPI`` is ``0x4010aef3`` with a 16-byte payload containing
``vcpu_id``, ``val``, ``val2``, and ``dest_id`` at offsets 0, 4, 8, and 12.
``KVM_DSM_X2APIC`` is ``0x4010aef4`` with a 16-byte payload.
``KVM_DSM_APIC_BASE`` is ``0x4018aef5`` with a 24-byte payload.
Reserved padding must be zero. Forwarded SIPIs reset an uninitialized
remote vCPU before starting it, matching INIT/SIPI startup semantics.

Linux 7.3 migration
-------------------

Official Linux v7.3 assigns capability 250 to ``KVM_CAP_PPC_COMPAT_CAPS``.
This series uses 253 and pairs with newly built QEMU binaries. Existing
Linux 7.2-rc2 binaries probe capability 250 and cannot enable this ABI.
Exit reasons, ioctl encodings, and payload layouts have not changed.
The internal DSM request uses bit 35 because bit 33 is now the official
VMSA page reload request. The APICv inhibit definitions follow their
upstream relocation to ``arch/x86/kvm/x86.h``.

Validation
----------

Verify that guest CPUs 0-3 are online, execute tasks on CPUs 2 and 3,
check shared data between local and remote CPUs, and power off the
guest. Both QEMU processes should exit without a BQL assertion.
