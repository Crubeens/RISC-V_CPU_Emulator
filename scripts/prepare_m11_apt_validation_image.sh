#!/usr/bin/env bash
set -euo pipefail

if [[ ${EUID} -ne 0 ]]; then
    echo "Run this script as root inside WSL." >&2
    exit 1
fi

if [[ $# -ne 2 && $# -ne 4 ]]; then
    echo "usage: $0 <base.ext4> <validation.ext4> [base.dtb validation.dtb]" >&2
    exit 2
fi

base_image=$1
validation_image=$2
base_dtb=${3:-}
validation_dtb=${4:-}
script_directory=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
guest_directory="${script_directory}/guest"

if [[ ! -f ${base_image} ]]; then
    echo "base image does not exist: ${base_image}" >&2
    exit 3
fi
if [[ -e ${validation_image} ]]; then
    echo "validation image already exists: ${validation_image}" >&2
    exit 4
fi
if [[ -n ${base_dtb} && ! -f ${base_dtb} ]]; then
    echo "base DTB does not exist: ${base_dtb}" >&2
    exit 3
fi
if [[ -n ${validation_dtb} && -e ${validation_dtb} ]]; then
    echo "validation DTB already exists: ${validation_dtb}" >&2
    exit 4
fi

for tool in cp e2fsck install mount sync umount; do
    if ! command -v "${tool}" >/dev/null 2>&1; then
        echo "missing required tool: ${tool}" >&2
        exit 5
    fi
done
if [[ -n ${base_dtb} ]] && ! command -v fdtput >/dev/null 2>&1; then
    echo "missing required tool: fdtput" >&2
    exit 5
fi

mount_directory=$(mktemp -d)
mounted=false
cleanup()
{
    if [[ ${mounted} == true ]]; then
        umount "${mount_directory}"
    fi
    rmdir "${mount_directory}"
}
trap cleanup EXIT

repair_filesystem()
{
    local status=0
    e2fsck -fy "$1" || status=$?
    if [[ ${status} -gt 1 ]]; then
        echo "cannot repair validation filesystem: $1" >&2
        return "${status}"
    fi
}

cp --sparse=always "${base_image}" "${validation_image}"
repair_filesystem "${validation_image}"
mount -o loop "${validation_image}" "${mount_directory}"
mounted=true

install -D -m 0755 \
    "${guest_directory}/rv64-m11-apt-acceptance.sh" \
    "${mount_directory}/usr/local/sbin/rv64-m11-apt-acceptance"
install -D -m 0644 \
    "${guest_directory}/rv64-m11-apt-acceptance.service" \
    "${mount_directory}/etc/systemd/system/rv64-m11-apt-acceptance.service"
# debootstrap may leave a legacy source alongside the deb822 source installed
# by build_debian_rv64_rootfs.sh.  Keep one authoritative source to avoid
# downloading and parsing every index twice.
if [[ -f "${mount_directory}/etc/apt/sources.list.d/debian.sources" ]]; then
    rm -f "${mount_directory}/etc/apt/sources.list"
fi
mkdir -p "${mount_directory}/etc/systemd/system/multi-user.target.wants"
ln -sfn ../rv64-m11-apt-acceptance.service \
    "${mount_directory}/etc/systemd/system/multi-user.target.wants/rv64-m11-apt-acceptance.service"
ln -sfn usr/local/sbin/rv64-m11-apt-acceptance \
    "${mount_directory}/m11"
rm -f \
    "${mount_directory}/var/lib/rv64-m11/phase1-complete" \
    "${mount_directory}/var/lib/rv64-m11/phase2-complete"
rmdir "${mount_directory}/var/lib/rv64-m11" 2>/dev/null || true

sync
umount "${mount_directory}"
mounted=false
e2fsck -fn "${validation_image}"
echo "Created M11 APT validation image: ${validation_image}"

if [[ -n ${base_dtb} ]]; then
    cp "${base_dtb}" "${validation_dtb}"
    fdtput -t s "${validation_dtb}" /chosen bootargs \
        'console=ttyS0,115200 earlycon=uart8250,mmio,0x10000000 root=/dev/vda rw rootwait net.ifnames=0 init=/m11'
    echo "Created M11 direct-init DTB: ${validation_dtb}"
fi
