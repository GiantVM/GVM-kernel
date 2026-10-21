# GVM-Kernel

GiantVM is a many-to-one virtualization framework built atop the QEMU/KVM hypervisor that consolidates multiple physical servers into a unified virtual machine.
Our core technical approach provides a Single System Image without requiring any modifications to the Guest OS or userspace applications.

## Introduction

The `upstream/7.3` branch hosts the DSM (Distributed Shared Memory) version of GiantVM, which can be evaluated over TCP/IP.

The `shared-memory/v7.3` branch hosts our code that utilizes shared memory as the underlying communication and memory access mechanism for GiantVM.

We are actively expanding GiantVM across multiple interconnects and hardware platforms. `RDMA` support and `CXL` support have been completed and will be released soon. `Huawei UnifiedBus` support has also been completed and is currently under testing. `ARM` architecture support is under active development and close to completion, while `RISC-V` support is planned for future work.

## Installation

This guide describes how to download the required Linux kernel source, apply the GiantVM kernel patches, prepare the provided static QEMU package, replace the host kernel, and start a two-node GiantVM instance.

### Step 1. Obtain the Kernel Source

```bash
git clone --branch upstream/7.3 https://github.com/GiantVM/GVM-kernel.git
cd GVM-kernel
```

The frozen baseline has exactly the official Linux `v7.3` source tree:

- Official repository: `git@github.com:torvalds/linux.git`
- Official commit: `c03c1a14507dfaf8b856c2608f9ac216021fc222`
- Tree: `30fd2ef82912c26cf3a58ad0ba6afc375fa851ac`

The baseline is followed by seven functional commits and `Create README.md`.
To export only the seven functional patches from this branch tip:

```bash
git format-patch -o ../GVM-patches HEAD~8..HEAD~1
```

These patches apply to the unmodified official `v7.3` source. Earlier
7.1-rc3 patch packages target a different baseline.

### Step 2. Build the Matching QEMU Runtime

Linux v7.3 already assigns capability IDs 249, 250 and 251 to official
interfaces. This kernel uses `KVM_CAP_X86_DSM=252`. Pair it with QEMU
`upstream/v10.1.2-linux-v7.3`, whose headers and capability checks match.
The previous Google Drive DSM runtime uses capability 249 and must be rebuilt.

Build the static runtime and package its matching BIOS files using the
[QEMU README](https://github.com/GiantVM/GVM-qemu/blob/upstream/v10.1.2-linux-v7.3/README.md):

```bash
git clone --branch upstream/v10.1.2-linux-v7.3 https://github.com/GiantVM/GVM-qemu.git
```

The resulting package is `gvm-qemu-dsm-v10.1.2-linux-v7.3.tar.gz`.
Extract it and check the binary and BIOS directory:

```bash
tar -xzf /path/to/gvm-qemu-dsm-v10.1.2-linux-v7.3.tar.gz
cd gvm-qemu-dsm-v10.1.2-linux-v7.3
./qemu-system-x86_64 --version
ls pc-bios
```

The expected QEMU version is `10.1.2`. The version string alone does not
identify the GiantVM ABI; use the specified branch for both QEMU nodes.
The poweroff fix from commit `2d2411ba23` is included in that branch.

### Step 3. Build the Kernel and Replace the System Kernel

Return to the cloned `GVM-kernel` source directory.

The following packages are required for compilation. Install them as needed:

```bash
sudo apt install -y libncurses-dev
sudo apt install -y libdw-dev libssl-dev libelf-dev
sudo apt install gawk
```

Enter the patched Linux kernel source directory:

```bash
cd /path/to/GVM-kernel
```

Import the current system kernel configuration:

```bash
cp /boot/config-$(uname -r) .config
scripts/config --set-str LOCALVERSION "-gvm-dsm-v7.3"
scripts/config --disable LOCALVERSION_AUTO
```

Open the kernel configuration menu:

```bash
make menuconfig
```

In `menuconfig`, enter the `Virtualization` submenu and enable the following options:

->Virtualization

 ->[*]KVM DSM interrupt forwarding support(Press 'Y' to enable)

  ->[*]KVM distributed software memory support(Press 'Y' to enable)

`KVM distributed software memory support` depends on `KVM DSM interrupt forwarding support`. Enable `KVM DSM interrupt forwarding support` first, then enable `KVM distributed software memory support`.

After enabling the option, save the kernel configuration and exit `menuconfig`.

Disable kernel build-time authentication settings before compiling the kernel:

```bash
scripts/config --disable SYSTEM_TRUSTED_KEYS
scripts/config --disable SYSTEM_REVOCATION_KEYS
```

Before compiling, configure the host to disable APIC virtualization (APICv) for GiantVM. `enable_apicv` is a `kvm_intel` module parameter, not a `menuconfig` option:

```bash
echo 'options kvm_intel enable_apicv=0' | sudo tee /etc/modprobe.d/gvm-kvm-intel.conf
```

Alternatively, append `kvm_intel.enable_apicv=0` to `GRUB_CMDLINE_LINUX` in `/etc/default/grub`, preserving any existing parameters, then run `sudo update-grub`. For example:

```bash
GRUB_CMDLINE_LINUX="kvm_intel.enable_apicv=0"
```

Choose either method; it takes effect after the host reboot below. Verify that APICv is disabled with `cat /sys/module/kvm_intel/parameters/enable_apicv` (expected output: `N`).

Build the kernel with parallel jobs. Replace `N` with the number of available CPU cores:

```bash
make -jN
```

Install the kernel modules and kernel image:

```bash
sudo make modules_install -jN
sudo make install
sudo update-initramfs -u -k "$(make -s kernelrelease)"
```

Reboot the host:

```bash
sudo reboot
```

During reboot, select the newly installed GiantVM kernel from the GRUB menu.

After the system comes back up, verify that the active kernel is available:

```bash
uname -r
```

The expected kernel version is: `7.3.0`

### Step 4. Start GiantVM Nodes

Download the guest kernel image and system image before starting the nodes.

Download address for bzImage:

```text
https://drive.google.com/file/d/1ZOkVbzW284IKsSD8yItNrlUh8x2J4rMG/view?usp=sharing
```

Download address for the system image:

```text
https://drive.google.com/file/d/12Mi35QuTl2cFg9WtFr-_4NFUG0Qf36F2/view?usp=sharing
```

Create `start-common.sh` for settings shared by all GiantVM nodes:

```bash
vim start-common.sh
```

Use the following template:

```bash
#!/bin/bash

QEMU_PATH="/path/to/gvm-qemu-dsm-v10.1.2-linux-v7.3/qemu-system-x86_64"
BIOS_PATH="/path/to/gvm-qemu-dsm-v10.1.2-linux-v7.3/pc-bios"
KERNEL_PATH="/path/to/bzImage"
ROOTFS_PATH="/path/to/debian-12.raw"

qemu_args=(
    -nographic
    -machine kernel-irqchip=split
    -smp 4 -m xG
    -enable-kvm
    -drive file="$ROOTFS_PATH",format=raw,id=rootfs,if=none,file.locking=off
    -device ide-hd,drive=rootfs,id=rootfs
    -kernel "$KERNEL_PATH"
    -append "nokaslr console=ttyS0 root=/dev/sda1"
    -L "$BIOS_PATH"
)
```

Update `QEMU_PATH` to point to the provided static `qemu-system-x86_64` binary. Update `BIOS_PATH` to point to the `pc-bios` directory from the same QEMU package. Update `KERNEL_PATH` to point to the downloaded guest kernel image. Update `ROOTFS_PATH` to point to the downloaded system image.

Create `start-node0.sh` in the same directory as `start-common.sh`:

```bash
#!/bin/bash

source start-common.sh

qemu_args+=(
    -local-cpu cpus=2,start=0,iplist="0.0.0.0 0.0.0.0"
    -vnc :0
)
"$QEMU_PATH" "${qemu_args[@]}"
```

Create `start-node1.sh` in the same directory:

```bash
#!/bin/bash

source start-common.sh

qemu_args+=(
    -local-cpu cpus=2,start=2,iplist="0.0.0.0 0.0.0.0"
)
"$QEMU_PATH" "${qemu_args[@]}"
```

Parameter notes:

- `-local-cpu cpus=2,start=0,...` assigns two CPUs to node 0, starting from CPU index `0`.
- `-local-cpu cpus=2,start=2,...` assigns two CPUs to node 1, starting from CPU index `2`.
- `cpus` is the number of CPUs assigned to the node.
- `start` is the starting CPU index assigned to the node.

Start the nodes from two separate terminals in the same directory.

In the first terminal, run:

```bash
sudo ./start-node0.sh
```

In the second terminal, run:

```bash
sudo ./start-node1.sh
```

After both node processes are started, GiantVM presents one aggregated VM to the user. Log in only once from the visible console. Use `lab` as the username and `p` as the password when prompted.

## Acknowledgement

This work represents a multi-year research and engineering effort. We would like
to deeply thank all contributors who made this possible across different phases
of the project:

[Initial Project Development - Linux 4.19 era]

  Ding Zhuocheng <tcbbd@sjtu.edu.cn>

  Chen Yubin <binsschen@sjtu.edu.cn>

  Zhang Jin <jzhang3002@sjtu.edu.cn>

  Wang Yun <yunwang94@sjtu.edu.cn>

  Ma Jiacheng <jiacheng.ma@amd.com>

  Yu Boshi <201608ybs@sjtu.edu.cn>

  Jia Xingguo <jiaxg1998@sjtu.edu.cn>

  Chen Weiye <vorringer@sjtu.edu.cn>

  Wu Chenggang <wuchenggang@sjtu.edu.cn>

  Xiang Yuxin <xiangyuxin@sjtu.edu.cn>

[Modernization, Upstream Development, and Active Maintenance]

  Xiong Tianlei <qmyyxtl@sjtu.edu.cn>

  Xu Kailiang <xukl2019@sjtu.edu.cn>

  Xue Songtao <xxxlhhxz@sjtu.edu.cn>

  Shou Muliang <muliang.shou@sjtu.edu.cn>

  Han Fengze <adoniswhite926@gmail.com>

  Ren Luobin <renluobin0257@gmail.com>

## Contact

For questions or technical discussions related to GiantVM, please reach out to the project team at tcloud.sjtu@gmail.com

## Linux 7.3 ABI

The memory capability is 252. Interrupt exits remain 45 (IPI), 46
(x2APIC ICR), and 47 (APIC base). Ioctl values and payload offsets are
unchanged, including `dest_id` at offset 12 in the 16-byte IPI payload.
The kernel-internal DSM request uses bit 35 because official Linux uses
bit 33 for VMSA page reload.

The shared-memory pair is kernel `shared-memory/v7.3` and QEMU
`shared-memory/v10.1.2-linux-v7.3`; it uses capability 253 and exit 44.

Use the distinct `-gvm-dsm-v7.3` kernel release suffix so installing
the DSM and shared-memory builds does not overwrite the other build.
