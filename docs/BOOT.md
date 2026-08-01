# 启动与日常运行

以下命令均在项目根目录的 PowerShell 中执行。省略 `--cpu` 时默认使用 RV32。

## RV32 Linux

```powershell
.\build\release\riscv_emulator.exe --cpu rv32 --boot-disk `
  ".\boot-images\opensbi-v1.8.1-rv32-fw_jump.bin" `
  ".\boot-images\linux-v6.12.96-rv32ima-Image" `
  ".\build\release\images\rv32-virt.dtb" `
  ".\boot-images\buildroot-2025.02.16-rv32ima-rootfs.ext4"
```

## RV64 Buildroot

```powershell
.\build\release\riscv_emulator.exe --cpu rv64 --ram-mib 512 --boot-disk `
  ".\boot-images\opensbi-v1.8.1-rv64-fw_jump.bin" `
  ".\boot-images\linux-v6.12.96-rv64imac-Image" `
  ".\build\release\images\rv64-virt.dtb" `
  ".\boot-images\rootfs-buildroot-v2025.02.16-rv64imac.ext4"
```

RV64 网络默认启用，无需 TAP 网卡。Buildroot 已验证 DHCP 地址
`10.0.2.15`、网关 `10.0.2.2`、DNS `10.0.2.3`。文件后端虚拟磁盘不会在
启动时整盘复制到宿主内存，正常退出会刷新写入。

`--ram-mib` 必须放在具体命令之前，支持 64–4096 MiB，默认 256 MiB。
模拟器会把实际容量同步写入 RV64 DTB。Debian 建议从 512 MiB 开始，配置值
不应超过宿主机可用内存。

## RV64 Debian 13

```powershell
.\build\release\riscv_emulator.exe --cpu rv64 --ram-mib 512 --boot-disk `
  ".\boot-images\opensbi-v1.8.1-rv64-fw_jump.bin" `
  ".\boot-images\linux-v6.12.96-rv64gc-Image" `
  ".\build\release\images\rv64-virt.dtb" `
  ".\boot-images\debian-13-riscv64-apt.ext4"
```

M11 已验证公网 DNS/HTTPS、APT 更新、安装、运行、升级、重启持久化和卸载。
断网、错误源、TLS 公钥错误、空间不足和损坏包均会确定失败且不破坏 dpkg
数据库。解释执行下索引解压和包数据库维护明显慢于真实硬件，输出短暂停顿不
等于死机。

## SDL 图形界面

在 `--boot` 或 `--boot-disk` 前增加 `--gui`。窗口中：

- `F1`：UART 终端；
- `F2`：客户机 Framebuffer；
- 键盘输入发送到虚拟 UART；
- 关闭 SDL 窗口不强制关闭客户机。

## 步数上限与镜像位置

命令末尾可追加正整数作为 machine-step 上限，例如 `100000000`。省略时，
`--boot-disk` 会运行到客户机请求关机。

持久镜像位于项目根目录的 `boot-images/`，不属于 `build/`。清理或重新生成
构建目录不会覆盖 OpenSBI、Linux 或根文件系统镜像。
