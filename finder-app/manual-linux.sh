#!/bin/bash
# Script outline to install and build kernel.
# Author: Siddhant Jajoo.

set -e
set -u
set -x

OUTDIR=/tmp/aeld
KERNEL_REPO=git://git.kernel.org/pub/scm/linux/kernel/git/stable/linux-stable.git
KERNEL_VERSION=v5.15.163
BUSYBOX_VERSION=1_33_1
FINDER_APP_DIR=$(realpath $(dirname $0))
ARCH=arm64
CROSS_COMPILE=aarch64-none-linux-gnu-
NPROC=$(nproc)
MAKE_CMD="make -j${NPROC} ARCH=${ARCH} CROSS_COMPILE=${CROSS_COMPILE}"

if [ $# -lt 1 ]
then
	echo "Using default directory ${OUTDIR} for output"
else
	OUTDIR=$1
	echo "Using passed directory ${OUTDIR} for output"
fi

mkdir -p ${OUTDIR}

cd "$OUTDIR"
if [ ! -d "${OUTDIR}/linux-stable" ]; then
    #Clone only if the repository does not exist.
	echo "CLONING GIT LINUX STABLE VERSION ${KERNEL_VERSION} IN ${OUTDIR}"
	git clone ${KERNEL_REPO} --depth 1 --single-branch --branch ${KERNEL_VERSION}
fi
if [ ! -e ${OUTDIR}/linux-stable/arch/${ARCH}/boot/Image ]; then
    cd linux-stable
    echo "Checking out version ${KERNEL_VERSION}"
    git checkout ${KERNEL_VERSION}

    # TODO: Add your kernel build steps here
    # 1. deep clean
    ${MAKE_CMD} mrproper
    # 2. Build QEMU defconfig
    ${MAKE_CMD} defconfig
    # 3. Build a linux image, vmlinux. In our case it is QEMU image
    ${MAKE_CMD} all
    # 4. Build modules
    ${MAKE_CMD} modules
    # 5. Build devicetree
    ${MAKE_CMD} dtbs
fi

echo "Adding the Image in outdir"
cp -v ${OUTDIR}/linux-stable/arch/${ARCH}/boot/Image ${OUTDIR}

echo "Creating the staging directory for the root filesystem"
cd "$OUTDIR"
if [ -d "${OUTDIR}/rootfs" ]
then
	echo "Deleting rootfs directory at ${OUTDIR}/rootfs and starting over"
    rm  -rf ${OUTDIR}/rootfs
fi

# TODO: Create necessary base directories
mkdir -p ${OUTDIR}/rootfs/{bin,dev,etc,home,lib,lib64,proc,sbin,sys,tmp,usr,var}
mkdir -p ${OUTDIR}/rootfs/{usr,bin,usr,lib,usr,sbin,var/log}

cd "$OUTDIR"
if [ ! -d "${OUTDIR}/busybox" ]
then
git clone git://busybox.net/busybox.git
    cd busybox
    git checkout ${BUSYBOX_VERSION}
    # TODO:  Configure busybox
    ${MAKE_CMD} distclean
    ${MAKE_CMD} defconfig
else
    cd busybox
fi

# TODO: Make and install busybox
${MAKE_CMD}
${MAKE_CMD} CONFIG_PREFIX=${OUTDIR}/rootfs install

echo "Library dependencies"
cd ${OUTDIR}/rootfs
INTERPRETER=$(${CROSS_COMPILE}readelf -a bin/busybox | grep "program interpreter" | awk '{print $4;}' | sed -e 's/\]//g')
LIBRARIES=$(${CROSS_COMPILE}readelf -a bin/busybox | grep "Shared library" | awk '{print $5;}' | sed -e 's/\[//g' -e 's/\]//g')

# TODO: Add library dependencies to rootfs
TOOLCHAIN_SYSROOT=$(${CROSS_COMPILE}gcc -print-sysroot)
cp ${TOOLCHAIN_SYSROOT}/${INTERPRETER} ${OUTDIR}/rootfs/lib
echo "${LIBRARIES}" | while read -r library;
do
    echo ${library}
    cp ${TOOLCHAIN_SYSROOT}/lib64/${library} ${OUTDIR}/rootfs/lib64/
done

# TODO: Make device nodes
mknod -m 666 ${OUTDIR}/rootfs/dev/null c 1 3 || true
mknod -m 600 ${OUTDIR}/rootfs/dev/console c 5 1 || true

# TODO: Clean and build the writer utility
pushd ${FINDER_APP_DIR}
${MAKE_CMD} clean
${MAKE_CMD}
popd

# TODO: Copy the finder related scripts and executables to the /home directory
# on the target rootfs
mkdir -pv ${OUTDIR}/rootfs/home/conf
cp -v ${FINDER_APP_DIR}/{finder-test,finder,autorun-qemu}.sh ${OUTDIR}/rootfs/home
cp -v ${FINDER_APP_DIR}/conf/{username,assignment}.txt ${OUTDIR}/rootfs/home/conf
cp -v ${FINDER_APP_DIR}/writer ${OUTDIR}/rootfs/home

# Modify finder-test.sh to reference conf/assignment.txt instead of ../conf/assignment.txt
sed -i 's/\.\.\/conf/conf/g' ${OUTDIR}/rootfs/home/finder-test.sh

# TODO: Chown the root directory
sudo chown -R root:root ${OUTDIR}/rootfs/

# TODO: Create initramfs.cpio.gz
cd ${OUTDIR}/rootfs
find . | cpio -H newc -ov --owner root:root > ${OUTDIR}/initramfs.cpio
gzip -f ${OUTDIR}/initramfs.cpio
