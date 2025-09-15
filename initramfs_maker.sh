#!/bin/bash

rm -rf busybox

git clone https://github.com/mirror/busybox.git
cd busybox

make ARCH=riscv CROSS_COMPILE=riscv64-linux-gnu- -j$(nproc) defconfig 
sed -i 's/# CONFIG_STATIC is not set/CONFIG_STATIC=y/g' .config 
sed -i 's/CONFIG_TC=y/CONFIG_TC=n/g' .config 
sed -i 's/CONFIG_MD5SUM=y/CONFIG_MD5SUM=n/g' .config 
sed -i 's/CONFIG_FEATURE_MD5_SHA1_SUM_CHECK=y/CONFIG_FEATURE_MD5_SHA1_SUM_CHECK=n/g' .config 
sed -i 's/CONFIG_FEATURE_HTTPD_AUTH_MD5=y/CONFIG_FEATURE_HTTPD_AUTH_MD5=n/g' .config 
sed -i 's/CONFIG_SHA1SUM=y/CONFIG_SHA1SUM=n/g' .config 
sed -i 's/CONFIG_SHA1_HWACCEL=y/CONFIG_SHA1_HWACCEL=n/g' .config

make ARCH=riscv CROSS_COMPILE=riscv64-linux-gnu- -j$(nproc) install 

cd _install/ 
rm linuxrc 
mkdir proc 

echo '#!/bin/sh' > init 
echo 'mount -t proc proc /proc' >> init 
#echo 'exec /bin/sh' >> init
echo 'mkdir -p /dev/shm' >> init
echo 'mount -t tmpfs -o mode=1777,size=1G tmpfs /dev/shm' >> init
echo 'ip link set dev eth0 up' >> init
echo './main' >> init

chmod +x init
chmod +x main

cp ../../bin/main-riscv ./main
find . | cpio -o -H newc > ../../initramfs.cpio 
cd ../../

rm -rf busybox