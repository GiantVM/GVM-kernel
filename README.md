# GVM-kernel

This is the `shared-memory` branch of GiantVM. It contains the shared-memory-based implementation,which uses shared memory as the underlying communication between GiantVM nodes.

## Installation

This installation guide describes how to download the required Linux kernel source, apply the GiantVM kernel patches, prepare the provided static QEMU package, replace the host kernel, and start a two-node GiantVM instance.

### Step 1. Obtain the Kernel Source

#### Option 1.Download Linux 7.1.0-rc6 and Apply GiantVM Patches

The required kernel version is `7.1.0-rc6`.

Download and extract the official Linux kernel source:

```bash
tag=v7.1-rc6
wget https://github.com/torvalds/linux/archive/refs/tags/$tag.tar.gz
tar -xf $tag.tar.gz
cd linux-7.1-rc6
```

Download address for the patch package:

```text
https://drive.google.com/file/d/11EIBGwdd6p4nFfFmZXEXYLM_C7vEHlZP/view?usp=sharing
```

Copy the patch package into the kernel source directory:

```bash
cp /path/to/GVM-shared-memory-patches.tar.xz .
```

Extract the patch package:

```bash
tar -xf GVM-shared-memory-patches.tar.xz
```

Apply the patches from the kernel source root:

```bash
for patch in GVM-shared-memory-patches/000*.patch; do
    patch -p1 < "$patch"
done
```

#### Option 2.Clone the GiantVM Kernel Repository

```bash
git clone -b shared-memory https://github.com/GiantVM/GVM-kernel.git
```

### Step 2. Prepare the Provided Static QEMU Package

The GiantVM QEMU runtime is provided as a prebuilt static package. It includes `qemu-system-x86_64` and the matching `pc-bios` directory.

Download address for the static QEMU package:

```text
https://drive.google.com/file/d/16TI9xGpiPaYMEK--Hp3Ls5yyjwC40dNz/view?usp=sharing
```

Copy the QEMU package to the host and extract it:

```bash
tar -xf /path/to/gvm-qemu-v10.1.2.tar.xz
cd gvm-qemu-v10.1.2
```

Verify that the QEMU binary and BIOS files are present:

```bash
./qemu-system-x86_64 --version
ls pc-bios
```

The expected QEMU version is `QEMU emulator version 10.1.2`.

The GVM-qemu source code is available at https://github.com/GiantVM/GVM-qemu/tree/shared-memory. Interested users are welcome to build and test it from source. We also plan to organize the GVM-related QEMU changes and submit them to the upstream QEMU community.

### Step 3. Build the Kernel and Replace the System Kernel

Enter the GiantVM kernel source directory — this should be either the patched `linux-7.1-rc5` source tree from Option 1 or the cloned `GVM-kernel` repository from Option 2. GVM-kernel is used uniformly in the following steps.

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
```

Open the kernel configuration menu:

```bash
make menuconfig
```

In `menuconfig`, enter the `Virtualization` submenu and enable the following options:

->Virtualization

 ->[*]KVM DSM interrupt forwarding support(Press 'Y' to enable)

After enabling the option, save the kernel configuration and exit `menuconfig`.

Disable kernel build-time authentication settings before compiling the kernel:

```bash
scripts/config --disable SYSTEM_TRUSTED_KEYS
scripts/config --disable SYSTEM_REVOCATION_KEYS
```

Build the kernel with parallel jobs. Replace `N` with the number of available CPU cores:

```bash
make -jN
```

Install the kernel modules and kernel image:

```bash
sudo make modules_install -jN
sudo make install
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

The expected kernel version is: `7.1.0-rc6`

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

Check the available space of the shared memory filesystem:

```bash
df -h /dev/shm
```

Create `start-common.sh` for settings shared by all GiantVM nodes:

```bash
vim start-common.sh
```

Use the following template:

```bash
#!/bin/bash

QEMU_PATH="/path/to/gvm-qemu-v10.1.2/qemu-system-x86_64"
BIOS_PATH="/path/to/gvm-qemu-v10.1.2/pc-bios"
KERNEL_PATH="/path/to/bzImage"
ROOTFS_PATH="/path/to/debian-12.raw"

qemu_args=(
    -nographic
    -machine kernel-irqchip=split
    -smp 4 -m 2G
    -enable-kvm
    -drive file="$ROOTFS_PATH",format=raw,id=rootfs,if=none,file.locking=off
    -device ide-hd,drive=rootfs,id=rootfs
    -kernel "$KERNEL_PATH"
    -append "nokaslr console=ttyS0 root=/dev/sda1"
    -L "$BIOS_PATH"
)
```

Update `QEMU_PATH` to point to the provided static `qemu-system-x86_64` binary. Update `BIOS_PATH` to point to the `pc-bios` directory from the same QEMU package. Update `KERNEL_PATH` to point to the downloaded guest kernel image. Update `ROOTFS_PATH` to point to the downloaded system image.

The memory size passed by `-m` must not exceed the available space of `/dev/shm`.

Create `start-node0.sh` in the same directory as `start-common.sh`:

```bash
#!/bin/bash

source start-common.sh

qemu_args+=(
    -local-cpu cpus=2,start=0,iplist="0.0.0.0 0.0.0.0"
    -shm-path /dev/shm/qemu_shared
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
    -shm-path /dev/shm/qemu_shared
)
"$QEMU_PATH" "${qemu_args[@]}"
```

Parameter notes:

- `-local-cpu cpus=2,start=0,...` assigns two CPUs to node 0, starting from CPU index `0`.
- `-local-cpu cpus=2,start=2,...` assigns two CPUs to node 1, starting from CPU index `2`.
- `cpus` is the number of CPUs assigned to the node.
- `start` is the starting CPU index assigned to the node.
- `/qemu_shared` is a POSIX shared memory name. On Linux, it corresponds to `/dev/shm/qemu_shared`. QEMU creates and sizes this object automatically.

Start the nodes from two separate terminals in the same directory.

In the first terminal, run:

```bash
sudo ./start-node0.sh
```

In the second terminal, run:

```bash
sudo ./start-node1.sh
```

If the previous run exited abnormally, remove the stale shared memory object before starting the nodes again:

```bash
rm -f /dev/shm/qemu_shared
```

After both node processes are started, GiantVM presents one aggregated VM to the user. Log in only once from the visible console. Use `lab` as the username and `p` as the password when prompted.

