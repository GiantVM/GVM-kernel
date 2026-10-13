# GiantVM Shared-Memory Kernel

This branch provides interrupt forwarding for the shared-memory version of
GiantVM. Its baseline is Linux 7.2-rc2. Pair kernel `shared-memory/v7.2-rc2` with
GVM-qemu `shared-memory/v10.1.2`; both QEMU processes run on the same host and map
the same RAM backing file.

## Build and Install the Host Kernel

```bash
git clone --branch shared-memory/v7.2-rc2 https://github.com/GiantVM/GVM-kernel.git
cd GVM-kernel
sudo apt install -y build-essential libncurses-dev libdw-dev libssl-dev \
    libelf-dev gawk flex bison
cp /boot/config-$(uname -r) .config
make menuconfig
```

In `Virtualization`, enable `KVM DSM interrupt forwarding support`
(`CONFIG_KVM_DSM_IRQ_FORWARD=y`) together with KVM and the accelerator
for your host CPU. Save the configuration and exit `menuconfig`.

Disable APIC virtualization (APICv) on Intel hosts using either method:

```bash
echo 'options kvm_intel enable_apicv=0' | sudo tee /etc/modprobe.d/gvm-kvm-intel.conf
```

Alternatively, append `kvm_intel.enable_apicv=0` to `GRUB_CMDLINE_LINUX`
in `/etc/default/grub`, preserving existing parameters, and run
`sudo update-grub`. `enable_apicv` is a module parameter, not a
`menuconfig` option.

```bash
scripts/config --disable SYSTEM_TRUSTED_KEYS
scripts/config --disable SYSTEM_REVOCATION_KEYS
make -jN
sudo make -jN modules_install
sudo make install
sudo update-initramfs -u -k "$(make -s kernelrelease)"
sudo reboot
```

Replace `N` with the number of available CPU cores. Select the new 7.2-rc2
kernel in GRUB, then verify:

```bash
uname -r
cat /sys/module/kvm_intel/parameters/enable_apicv
```

The release begins with `7.2.0-rc2`; APICv should report `N` on Intel hosts.

## Prepare the Matching QEMU

Download the prebuilt `gvm-qemu-shm-v10.1.2.tar.gz` package, containing
the static QEMU binary and matching `pc-bios` directory:

[Download from Google Drive](https://drive.google.com/file/d/1yRLeaSt6bTpN0dokyPKuxynHH-OSZa1u/view?usp=sharing)

```bash
tar -xzf /path/to/gvm-qemu-shm-v10.1.2.tar.gz
cd gvm-qemu-shm-v10.1.2
./qemu-system-x86_64 --version
ls pc-bios
```

Alternatively, build the `shared-memory/v10.1.2` branch as described in its
[README](https://github.com/GiantVM/GVM-qemu/blob/shared-memory/v10.1.2/README.md).

The interrupt ABI uses capability `KVM_CAP_X86_DSM_IRQ_FORWARD=250`,
exit `KVM_EXIT_DSM_IRQ_FORWARD=44`, and payload types
`KVM_DSM_IRQ_FORWARD_XAPIC_ICR=1` and `KVM_DSM_IRQ_FORWARD_X2APIC_ICR=2`.
QEMU enables the capability and prints:

```text
GVM: enabled unified DSM IRQ forwarding ABI (cap 250)
```

The IPI, x2APIC and APIC-base ioctls are `0x4010aef3`, `0x4010aef4`,
and `0x4018aef5`. Both sides must agree on field widths and padding.
The version string `10.1.2` alone does not identify the GiantVM ABI.

## Start Two Nodes

Use an ordinary guest kernel, such as the previously tested Linux 6.6.71,
and a matching guest disk image. In two terminals on the same host, set
the following paths. Use `NODE_START=0` in the first terminal and
`NODE_START=2` in the second:

For the static package, set `QEMU` to its `qemu-system-x86_64` binary
and `BIOS` to its `pc-bios` directory instead of the source build paths.

```bash
QEMU=/path/to/GVM-qemu/build/qemu-system-x86_64
BIOS=/path/to/GVM-qemu/pc-bios
GUEST_KERNEL=/path/to/bzImage
ROOTFS=/path/to/debian-12.raw
SHM=/dev/shm/gvm-shared-memory
NODE_START=0

sudo "$QEMU" \
    -L "$BIOS" -nographic -enable-kvm \
    -machine kernel-irqchip=split -smp 4 -m 2G \
    -local-cpu "cpus=2,start=$NODE_START,iplist=127.0.0.1 127.0.0.1" \
    -shm-path "$SHM" \
    -drive "file=$ROOTFS,format=raw,id=rootfs,if=none,file.locking=off" \
    -device ide-hd,drive=rootfs,id=rootfs \
    -kernel "$GUEST_KERNEL" \
    -append "nokaslr console=ttyS0 root=/dev/sda1"
```

Start node0 first, then node1. Both processes must use the same QEMU build,
RAM size, BIOS files and shared-memory path. The backing file is opened
as a filesystem path, not as a POSIX `shm_open` name.

After logging into the guest on node0, verify and shut down with:

```bash
cat /sys/devices/system/cpu/online  # expected: 0-3
nproc                            # expected: 4
sudo poweroff
```

Use the default CPU model for the previously tested configuration.
Additional `-cpu host` testing exposed TSC skew and an RCU stall.

The TCP DSM version uses kernel `upstream/7.1-rc3` and QEMU `main`,
with a different memory transport and interrupt ABI.
