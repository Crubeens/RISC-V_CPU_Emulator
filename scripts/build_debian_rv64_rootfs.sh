#!/usr/bin/env bash
set -euo pipefail

if [[ ${EUID} -ne 0 ]]; then
    echo "Run this script as root inside WSL." >&2
    exit 1
fi

if [[ $# -lt 2 || $# -gt 3 ]]; then
    echo "usage: $0 <work-directory> <output.ext4> [image-size]" >&2
    exit 2
fi

work_directory=$1
output_image=$2
image_size=${3:-768M}
root_directory="${work_directory}/rootfs"

for tool in curl dpkg-deb mmdebstrap mkfs.ext4 qemu-riscv64-static sha256sum; do
    if ! command -v "${tool}" >/dev/null 2>&1; then
        echo "missing required tool: ${tool}" >&2
        exit 3
    fi
done

if [[ -e ${work_directory} ]]; then
    echo "work directory already exists: ${work_directory}" >&2
    exit 4
fi
if [[ -e ${output_image} ]]; then
    echo "output image already exists: ${output_image}" >&2
    exit 5
fi

mkdir -p "${root_directory}"

keyring_package="${work_directory}/debian-archive-keyring_2025.1_all.deb"
keyring_directory="${work_directory}/debian-archive-keyring"
curl \
    --fail \
    --location \
    --output "${keyring_package}" \
    https://deb.debian.org/debian/pool/main/d/debian-archive-keyring/debian-archive-keyring_2025.1_all.deb
printf '%s  %s\n' \
    9ea7778e443144ca490668737a8ab22dd3e748bb99e805e22ec055abeb3c7fac \
    "${keyring_package}" |
    sha256sum --check
mkdir -p "${keyring_directory}"
dpkg-deb --extract "${keyring_package}" "${keyring_directory}"

mmdebstrap \
    --mode=root \
    --architectures=riscv64 \
    --variant=minbase \
    --components=main \
    --include=systemd-sysv,udev,ifupdown,isc-dhcp-client,ca-certificates,iproute2,iputils-ping,curl,apt-utils \
    --keyring="${keyring_directory}/usr/share/keyrings/debian-archive-keyring.gpg" \
    trixie \
    "${root_directory}" \
    https://deb.debian.org/debian

cat >"${root_directory}/etc/apt/sources.list.d/debian.sources" <<'EOF'
Types: deb
URIs: https://deb.debian.org/debian
Suites: trixie trixie-updates
Components: main
Signed-By: /usr/share/keyrings/debian-archive-keyring.gpg

Types: deb
URIs: https://security.debian.org/debian-security
Suites: trixie-security
Components: main
Signed-By: /usr/share/keyrings/debian-archive-keyring.gpg
EOF

cat >"${root_directory}/etc/network/interfaces" <<'EOF'
auto lo
iface lo inet loopback

allow-hotplug eth0
iface eth0 inet dhcp
EOF

cat >"${root_directory}/etc/fstab" <<'EOF'
/dev/vda / ext4 defaults 0 1
devtmpfs /dev devtmpfs mode=0755,nosuid 0 0
proc /proc proc defaults 0 0
sysfs /sys sysfs defaults 0 0
tmpfs /tmp tmpfs mode=1777,nosuid,nodev 0 0
EOF

printf '%s\n' rv64-debian >"${root_directory}/etc/hostname"
cat >"${root_directory}/etc/hosts" <<'EOF'
127.0.0.1 localhost
127.0.1.1 rv64-debian
::1 localhost ip6-localhost ip6-loopback
EOF

if [[ -L ${root_directory}/etc/resolv.conf ]]; then
    rm "${root_directory}/etc/resolv.conf"
fi
printf '%s\n' 'nameserver 10.0.2.3' >"${root_directory}/etc/resolv.conf"

sed -i 's/^root:[^:]*:/root::/' "${root_directory}/etc/shadow"
printf '%s\n' ttyS0 >>"${root_directory}/etc/securetty"
: >"${root_directory}/etc/machine-id"

mkdir -p "${root_directory}/etc/systemd/system/network-online.target.wants"
ln -sfn /lib/systemd/system/networking.service \
    "${root_directory}/etc/systemd/system/network-online.target.wants/networking.service"

truncate -s "${image_size}" "${output_image}"
mkfs.ext4 \
    -F \
    -L rootfs \
    -d "${root_directory}" \
    "${output_image}"

e2fsck -fn "${output_image}"
echo "Created Debian 13 riscv64 APT image: ${output_image}"
