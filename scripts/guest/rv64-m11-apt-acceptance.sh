#!/bin/sh
set -eu

state_directory=/var/lib/rv64-m11
phase_one_marker="${state_directory}/phase1-complete"
phase_two_marker="${state_directory}/phase2-complete"
checkpoint_file="${state_directory}/checkpoint"

mkdir -p "${state_directory}"

record_checkpoint()
{
    printf '%s\n' "$1" >"${checkpoint_file}"
    echo "RV64-M11 CHECKPOINT: $1"
    sync
}

apt_get()
{
    apt-get \
        -o Acquire::Languages=none \
        -o Acquire::CompressionTypes::Order::=gz \
        -o Acquire::Queue-Mode=access \
        -o Acquire::http::Pipeline-Depth=0 \
        -o Acquire::https::Pipeline-Depth=0 \
        -o Acquire::http::Timeout=30 \
        -o Acquire::https::Timeout=30 \
        -o Acquire::Retries=2 \
        -o APT::Color=0 \
        -o Dpkg::Progress-Fancy=0 \
        -o Dir::Cache::pkgcache= \
        -o Dir::Cache::srcpkgcache= \
        "$@"
}

direct_init=false
if [ "$$" -eq 1 ]; then
    direct_init=true
    mount -t proc proc /proc
    mount -t sysfs sysfs /sys
    mkdir -p /run /dev/pts
    mount -t tmpfs -o mode=0755 tmpfs /run
    mount -t devpts devpts /dev/pts
    hostname rv64-debian
    ip link set lo up
    ip link set eth0 up
    dhclient -1 -v eth0
    record_checkpoint dhcp-complete
fi

finish_and_power_off()
{
    sync
    echo "RV64-M11: requesting clean power off"
    if [ "${direct_init}" = true ]; then
        # PID 1 bypasses systemd's normal shutdown transaction.  Remounting
        # root read-only commits the ext4 journal before the forced poweroff.
        mount -o remount,ro / || true
        systemctl poweroff --force --force || reboot -f
    else
        systemctl poweroff --no-block
    fi
}

if [ -e "${phase_two_marker}" ]; then
    echo "RV64-M11: acceptance already complete"
    finish_and_power_off
    exit 0
fi

if [ -e "${phase_one_marker}" ]; then
    echo "RV64-M11 PHASE2 START"
    test -x /usr/bin/hello
    dpkg-query -W -f='${Status} ${Architecture} ${Version}\n' hello
    /usr/bin/hello

    DEBIAN_FRONTEND=noninteractive apt_get remove -y hello
    if dpkg-query -W -f='${db:Status-Abbrev}\n' hello 2>/dev/null |
        grep -q '^ii '; then
        echo "RV64-M11: hello remained installed after removal" >&2
        exit 1
    fi

    date -u '+RV64-M11 UTC %Y-%m-%dT%H:%M:%SZ'
    printf '%s\n' complete >"${phase_two_marker}"
    echo "RV64-M11 PHASE2 PASS: persistence and uninstall verified"
    finish_and_power_off
    exit 0
fi

echo "RV64-M11 PHASE1 START"
record_checkpoint phase1-start
date -u '+RV64-M11 UTC %Y-%m-%dT%H:%M:%SZ'
cat /sys/class/rtc/rtc0/date
cat /sys/class/rtc/rtc0/time

attempt=0
until ip -4 address show dev eth0 | grep -q 'inet '; do
    attempt=$((attempt + 1))
    if [ "${attempt}" -ge 60 ]; then
        echo "RV64-M11: DHCP did not configure eth0" >&2
        exit 1
    fi
    sleep 1
done

ip -4 address show dev eth0
ip route show
ip neigh show
record_checkpoint dns-start
if ! timeout 120 getent ahostsv4 deb.debian.org; then
    record_checkpoint dns-failed
    echo "RV64-M11: DNS lookup failed or timed out" >&2
    finish_and_power_off
    exit 1
fi
record_checkpoint dns-complete
if ! curl --fail --silent --show-error --location --head \
    --connect-timeout 30 --max-time 180 \
    https://deb.debian.org/debian/ >/dev/null; then
    record_checkpoint https-failed
    echo "RV64-M11: HTTPS request failed or timed out" >&2
    finish_and_power_off
    exit 1
fi
record_checkpoint https-complete
echo "RV64-M11 HTTPS PASS"

record_checkpoint apt-update-start
apt_get update -o APT::Update::Error-Mode=any
record_checkpoint apt-update-complete
DEBIAN_FRONTEND=noninteractive apt_get install -y hello
record_checkpoint apt-install-complete
/usr/bin/hello
DEBIAN_FRONTEND=noninteractive apt_get install --only-upgrade -y hello
record_checkpoint apt-upgrade-complete
dpkg-query -W -f='${Status} ${Architecture} ${Version}\n' hello

printf '%s\n' 'not a Debian package' >/tmp/rv64-m11-corrupt.deb
if dpkg-deb --info /tmp/rv64-m11-corrupt.deb >/dev/null 2>&1; then
    echo "RV64-M11: corrupt package was unexpectedly accepted" >&2
    exit 1
fi
rm -f /tmp/rv64-m11-corrupt.deb
echo "RV64-M11 CORRUPT PACKAGE REJECTED"

dpkg-query -W -f='${Architecture} ${Version}\n' hello >"${phase_one_marker}"
echo "RV64-M11 PHASE1 PASS: APT update/install/run/upgrade verified"
finish_and_power_off
