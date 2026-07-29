# 可复现的客户机软件配置

这里保存的是已经在本模拟器上成功启动的完整配置快照，不包含编译输出。

| 配置 | 架构 |
| --- | --- |
| `linux/linux-v6.12.96-rv32ima.config` | RV32IMA、ILP32、Sv32 |
| `linux/linux-v6.12.96-rv64imac.config` | RV64IMAC、LP64、Sv39 |
| `linux/linux-v6.12.96-rv64gc.config` | RV64GC、LP64D 用户态支持、Sv39 |
| `buildroot/buildroot-v2025.02.16-rv32ima.config` | RV32IMA、musl、BusyBox、64 MiB ext4 |
| `buildroot/buildroot-v2025.02.16-rv64imac.config` | RV64IMAC、musl、BusyBox、128 MiB ext4 |

三份 Linux 配置均为单 Hart；RV32 不启用 C，两个 RV64 配置启用 C。
RV64IMAC 配置不启用内核 FPU 支持，RV64GC 配置启用
`CONFIG_FPU`，供 Debian LP64D 用户态使用。
OpenSBI、Linux 和 Buildroot 源码仍在 WSL 中独立维护，不复制进本仓库。

`linux-v6.12.96-rv64imac.config` 保留为 Buildroot/网络快速回归基线；
`linux-v6.12.96-rv64gc.config` 是 Debian 13/LP64D 的当前配置。两者使用
独立镜像名，不互相覆盖。

## Linux

RV32：

```sh
PROJECT=/mnt/c/Users/Lenovo/Desktop/files/RISC-V_CPU_Emulator
SOURCE=/home/yzl/riscv/linux-6.12.96
OUT=/home/yzl/riscv/linux-build-rv32

mkdir -p "$OUT"
cp "$PROJECT/configs/linux/linux-v6.12.96-rv32ima.config" \
  "$OUT/.config"
make -C "$SOURCE" O="$OUT" \
  ARCH=riscv CROSS_COMPILE=riscv64-linux-gnu- olddefconfig
make -C "$SOURCE" O="$OUT" \
  ARCH=riscv CROSS_COMPILE=riscv64-linux-gnu- -j"$(nproc)" Image
```

RV64GC：

```sh
PROJECT=/mnt/c/Users/Lenovo/Desktop/files/RISC-V_CPU_Emulator
SOURCE=/home/yzl/riscv/linux-6.12.96-rv64
OUT=/home/yzl/riscv/linux-build-rv64

mkdir -p "$OUT"
cp "$PROJECT/configs/linux/linux-v6.12.96-rv64gc.config" \
  "$OUT/.config"
make -C "$SOURCE" O="$OUT" \
  ARCH=riscv CROSS_COMPILE=riscv64-linux-gnu- olddefconfig
make -C "$SOURCE" O="$OUT" \
  ARCH=riscv CROSS_COMPILE=riscv64-linux-gnu- -j"$(nproc)" Image
```

生成结果都位于 `$OUT/arch/riscv/boot/Image`。

## Buildroot

RV32：

```sh
PROJECT=/mnt/c/Users/Lenovo/Desktop/files/RISC-V_CPU_Emulator
SOURCE=/home/yzl/riscv/buildroot-2025.02.16
OUT=/home/yzl/riscv/buildroot-output-rv32

mkdir -p "$OUT"
cp "$PROJECT/configs/buildroot/buildroot-v2025.02.16-rv32ima.config" \
  "$OUT/.config"
make -C "$SOURCE" O="$OUT" olddefconfig
make -C "$SOURCE" O="$OUT" -j"$(nproc)"
```

RV64：

```sh
PROJECT=/mnt/c/Users/Lenovo/Desktop/files/RISC-V_CPU_Emulator
SOURCE=/home/yzl/riscv/buildroot-2025.02.16
OUT=/home/yzl/riscv/buildroot-output-rv64

mkdir -p "$OUT"
cp "$PROJECT/configs/buildroot/buildroot-v2025.02.16-rv64imac.config" \
  "$OUT/.config"
make -C "$SOURCE" O="$OUT" olddefconfig
make -C "$SOURCE" O="$OUT" -j"$(nproc)"
```

根文件系统位于 `$OUT/images/rootfs.ext4`。它通常是指向
`rootfs.ext2` 的符号链接，但文件系统内容实际为 ext4。

## OpenSBI

从 OpenSBI v1.8.1 源码目录分别执行：

```sh
make O=build-rv32 PLATFORM=generic \
  CROSS_COMPILE=riscv64-linux-gnu- \
  PLATFORM_RISCV_XLEN=32 PLATFORM_RISCV_ABI=ilp32 \
  PLATFORM_RISCV_ISA=rv32ima_zicsr_zifencei \
  FW_JUMP_ADDR=0x80400000

make O=build-rv64 PLATFORM=generic \
  CROSS_COMPILE=riscv64-linux-gnu- \
  PLATFORM_RISCV_XLEN=64 PLATFORM_RISCV_ABI=lp64 \
  PLATFORM_RISCV_ISA=rv64imac_zicsr_zifencei \
  FW_JUMP_ADDR=0x80400000
```

使用各自的 `platform/generic/firmware/fw_jump.bin`。

## 固件与内核校验

当前验证通过的持久镜像：

```text
7b19308eca664bf8f6b7767b5ccc23eae01991f35b0f1a9e5a62ac7c7fe7310f  opensbi-v1.8.1-rv32-fw_jump.bin
011a94bb1cf60a94e3e6130b1a901f96c544b140ab3da87bd1071019b42753be  opensbi-v1.8.1-rv64-fw_jump.bin
1beac73291dd8fea053f53498af01014539227714a54ab4ccac87f02c818175b  linux-v6.12.96-rv32ima-Image
bdae982e0c5717d7ada11e5c54c4629510fc6556603715ea3f39bdb0ca911bea  linux-v6.12.96-rv64imac-Image
a69abe8d0afa4d1cdc85840ca331d5c61ebfe6bdb59135bf0a5fd3eede120a60  linux-v6.12.96-rv64gc-Image
```

ext4 镜像在客户机关机时会回写，正常使用后 SHA-256 会发生变化，因此不把
运行后的磁盘哈希作为固定验收值。
