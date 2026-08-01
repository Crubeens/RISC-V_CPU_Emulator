#!/bin/sh
set -eu

state_directory=/var/lib/rv64-m11
phase_one_marker="${state_directory}/phase1-complete"
phase_two_marker="${state_directory}/phase2-complete"
phase_three_marker="${state_directory}/phase3-complete"
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
        # This acceptance-only PID 1 has no systemd shutdown transaction.
        # Sync the journal, then invoke the kernel poweroff path directly.
        systemctl poweroff --force --force || reboot -f
    else
        systemctl poweroff --no-block
    fi
}

require_public_dns()
{
    dns_attempt=0
    while [ "${dns_attempt}" -lt 12 ]; do
        if getent ahostsv4 deb.debian.org >/dev/null 2>&1; then
            return 0
        fi
        dns_attempt=$((dns_attempt + 1))
        sleep 5
    done
    echo "RV64-M11: public DNS precondition did not recover" >&2
    return 1
}

if [ -e "${phase_three_marker}" ]; then
    echo "RV64-M11: acceptance already complete"
    finish_and_power_off
    exit 0
fi

if [ -e "${phase_two_marker}" ]; then
    echo "RV64-M11 PHASE3 START"
    fault_root=/run/rv64-m11-faults
    source_file="${fault_root}/source.list"
    lists_directory="${fault_root}/lists"
    cache_directory="${fault_root}/cache"
    mkdir -p \
        "${lists_directory}/partial" \
        "${cache_directory}/archives/partial"
    printf '%s\n' \
        'deb [signed-by=/usr/share/keyrings/debian-archive-keyring.gpg] https://deb.debian.org/debian trixie main' \
        >"${source_file}"

    record_checkpoint disconnected-source-start
    if apt_get \
        -o Dir::Etc::sourcelist="${source_file}" \
        -o Dir::Etc::sourceparts=- \
        -o Dir::State::lists="${lists_directory}" \
        -o Dir::Cache="${cache_directory}" \
        -o Acquire::Retries=0 \
        -o Acquire::https::Timeout=5 \
        -o Acquire::https::Proxy=http://127.0.0.1:9 \
        update -o APT::Update::Error-Mode=any; then
        echo "RV64-M11: disconnected APT update unexpectedly succeeded" >&2
        finish_and_power_off
        exit 1
    fi
    echo "RV64-M11 DISCONNECTED SOURCE REJECTED"

    rm -rf "${lists_directory}" "${cache_directory}"
    mkdir -p \
        "${lists_directory}/partial" \
        "${cache_directory}/archives/partial"
    printf '%s\n' \
        'deb [signed-by=/usr/share/keyrings/debian-archive-keyring.gpg] https://deb.debian.org/debian rv64-m11-no-such-suite main' \
        >"${source_file}"
    record_checkpoint invalid-source-start
    if ! require_public_dns; then
        finish_and_power_off
        exit 1
    fi
    invalid_source_log="${fault_root}/invalid-source.log"
    if apt_get \
        -o Dir::Etc::sourcelist="${source_file}" \
        -o Dir::Etc::sourceparts=- \
        -o Dir::State::lists="${lists_directory}" \
        -o Dir::Cache="${cache_directory}" \
        -o Acquire::Retries=0 \
        update -o APT::Update::Error-Mode=any \
        >"${invalid_source_log}" 2>&1; then
        echo "RV64-M11: invalid Debian suite unexpectedly succeeded" >&2
        finish_and_power_off
        exit 1
    fi
    if ! grep -Eq '404|does not have a Release file' \
        "${invalid_source_log}"; then
        cat "${invalid_source_log}" >&2
        echo "RV64-M11: invalid source failed for an unrelated reason" >&2
        finish_and_power_off
        exit 1
    fi
    echo "RV64-M11 INVALID SOURCE REJECTED"

    record_checkpoint certificate-error-start
    if ! require_public_dns; then
        finish_and_power_off
        exit 1
    fi
    certificate_status=0
    curl --fail --silent --show-error --location \
        --connect-timeout 30 --max-time 180 \
        --pinnedpubkey 'sha256//AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA=' \
        https://deb.debian.org/debian/ >/dev/null 2>&1 ||
        certificate_status=$?
    if [ "${certificate_status}" -ne 90 ]; then
        echo "RV64-M11: expected curl certificate pin error 90, got ${certificate_status}" >&2
        finish_and_power_off
        exit 1
    fi
    echo "RV64-M11 CERTIFICATE ERROR REJECTED"

    record_checkpoint disk-full-start
    full_directory="${fault_root}/full"
    mkdir -p "${full_directory}"
    mount -t tmpfs -o size=16k tmpfs "${full_directory}"
    disk_status=0
    disk_full_log="${fault_root}/disk-full.log"
    (cd "${full_directory}" && apt-get download hello) \
        >"${disk_full_log}" 2>&1 ||
        disk_status=$?
    umount "${full_directory}"
    if [ "${disk_status}" -eq 0 ]; then
        echo "RV64-M11: package download unexpectedly fit in 16 KiB" >&2
        finish_and_power_off
        exit 1
    fi
    if ! grep -Eq 'No space left on device|\(28:' \
        "${disk_full_log}"; then
        cat "${disk_full_log}" >&2
        echo "RV64-M11: package download failed for an unrelated reason" >&2
        finish_and_power_off
        exit 1
    fi
    echo "RV64-M11 DISK FULL REJECTED"

    test -z "$(dpkg --audit)"
    dpkg-query -W -f='${db:Status-Abbrev} ${Architecture} ${Version}\n' base-files
    printf '%s\n' complete >"${phase_three_marker}"
    echo "RV64-M11 PHASE3 PASS: deterministic failure paths preserved the system"
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
