# Comunicação de sistemas autonomos críticos

This is some C++ thing for talking between virtual machines like cars, so we will simulate cars changing information.  
We using raw sockets and some custom protocol stuff. 
The main goal is make a VM communicate to another VM with max 600 bytes and lowest latency possible. 

## Arch
Kernel → SIGIO → Engine → NIC → Protocol (Observer).

## How to build

Deps:
- g++ for x86_64
- riscv64-linux-gnu-g++ for build to riscv64 (on Debian/Ubuntu: `sudo apt install g++-riscv64-linux-gnu`)

Steps:
1. Put all `.cpp` in `src/`
2. Open terminal here
3. Run:

```bash
make x86      # build for x86
make riscv    # build for riscv64
make all      # both
```

The binaries will be in `bin/`. Names like:
```
bin/main-x86
bin/main-riscv
```

## How to run

For x86 build, just:
```bash
./bin/main-x86
```

For riscv64 build, you need qemu-user:
```bash
sudo apt install qemu-user
qemu-riscv64 ./bin/myproject-riscv
```

## Notes

- Run with sudo if you using raw sockets.
---

## Util commands
```bash
qemu-system-riscv64 -machine virt -nographic -kernel Image -initrd initramfs.cpio -append "root=/dev/ram rw" -netdev socket,id=vlan0,mcast=230.0.0.1:1234 -device virtio-net,id=eth0,netdev=vlan0,mac=52:54:00:12:34:00

qemu-system-riscv64 -machine virt -nographic -kernel Image -initrd initramfs.cpio -append "root=/dev/ram rw" -netdev socket,id=vlan0,mcast=230.0.0.1:1234 -device virtio-net,id=eth0,netdev=vlan0,mac=52:54:00:12:34:01

 qemu-system-riscv64 -machine virt -nographic -kernel Image -initrd initramfs.cpio --append "root=/dev/ram"




git clone https://github.com/mirror/busybox.git 
cd busybox/ 

make ARCH=riscv CROSS_COMPILE=riscv64-linux-gnu- -j$(nproc) defconfig 

# IMPORTANT
# Set static flag to compile busybox
sed -i 's/# CONFIG_STATIC is not set/CONFIG_STATIC=y/g' .config
#---

sed -i 's/CONFIG_TC=y/CONFIG_TC=n/g' .config 
sed -i 's/CONFIG_MD5SUM=y/CONFIG_MD5SUM=n/g' .config 
sed -i 's/CONFIG_FEATURE_MD5_SHA1_SUM_CHECK=y/CONFIG_FEATURE_MD5_SHA1_SUM_CHECK=n/g' .config 
sed -i 's/CONFIG_FEATURE_HTTPD_AUTH_MD5=y/CONFIG_FEATURE_HTTPD_AUTH_MD5=n/g' .config 
sed -i 's/CONFIG_SHA1SUM=y/CONFIG_SHA1SUM=n/g' .config 
sed -i 's/CONFIG_SHA1_HWACCEL=y/CONFIG_SHA1_HWACCEL=n/g' .config
 
make ARCH=riscv CROSS_COMPILE=riscv64-linux-gnu- -j$(nproc) install 

cd _install/ 

#Not necessary
rm linuxrc mkdir proc 
#--

chmod +x init 

echo '#!/bin/sh' > init 
echo 'mount -t proc proc /proc' >> init 
echo 'exec /bin/sh' >> init



find . | cpio -o -H newc > ../../initramfs.cpio 
cd ../../

```
