# 客户机软件栈

大型上游源码树不进入本仓库。它们在 WSL/Ubuntu 中独立编译，最终二进制复制
到 `boot-images/`。项目 DTS 位于 `platform32/dts/` 与 `platform64/dts/`，
CMake 构建时生成对应 DTB。完整配置快照和重建命令见
[`configs/README.md`](../configs/README.md)。

## 固定版本

- OpenSBI v1.8.1；
- Linux v6.12.96；
- Buildroot 2025.02.16；
- Spike 固定提交见 [`tests/differential/SPIKE_REVISION.md`](../tests/differential/SPIKE_REVISION.md)。

## RV32 镜像

- `boot-images/opensbi-v1.8.1-rv32-fw_jump.bin`
- `boot-images/linux-v6.12.96-rv32ima-Image`
- `boot-images/buildroot-2025.02.16-rv32ima-rootfs.ext4`
- `build/<配置>/images/rv32-virt.dtb`

目标为 RV32IMA、ILP32、单 Hart、Sv32、无 F/D/V。

## RV64 镜像

- `boot-images/opensbi-v1.8.1-rv64-fw_jump.bin`
- `boot-images/linux-v6.12.96-rv64imac-Image`
- `boot-images/linux-v6.12.96-rv64gc-Image`
- `boot-images/rootfs-buildroot-v2025.02.16-rv64imac.ext4`
- `boot-images/debian-13-riscv64-apt.ext4`
- `build/<配置>/images/rv64-virt.dtb`

Buildroot 快速基线为 RV64IMAC、LP64、Sv39、musl、BusyBox 和 128 MiB ext4。
Debian 为 RV64GC、LP64D 和 768 MiB ext4，必须配合启用 `CONFIG_FPU` 的
RV64GC 内核。

## 职责边界

- OpenSBI 提供 M-mode 固件和 SBI 服务；
- Linux 运行于 S-mode；
- Buildroot 生成根文件系统和用户态，不是引导程序；
- DTS/DTB 描述 RAM、CPU、UART、CLINT、PLIC、VirtIO、Framebuffer 和
  Syscon，地址必须与相应平台常量一致；
- `boot-images/` 是持久外部产物，`build/` 是可重建输出。

## Debian APT 基线

[`scripts/build_debian_rv64_rootfs.sh`](../scripts/build_debian_rv64_rootfs.sh)
生成 `boot-images/debian-13-riscv64-apt.ext4`，使用 Debian 13 trixie 官方
riscv64 仓库。网络由 RV64 VirtIO Net 与 libslirp DHCP/DNS/NAT 提供，
Goldfish RTC 提供宿主实时时间。

M10 已完成 RV64F/RV64D、官方架构测试、Spike 差分和 RV64GC ISA 宣告。
M11 已完成官方 HTTPS 源的 APT 主链路、持久化和确定失败路径验收。阶段记录见
[`core64/PROJECT_PLAN.md`](../core64/PROJECT_PLAN.md)。

历史参考软件仓库：<https://github.com/bane9/rv64gc-emu-software>。
