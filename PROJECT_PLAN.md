# RISC-V32 CPU 模拟器项目最终计划书

版本：1.1

冻结日期：2026-07-24

扩展日期：2026-07-28

状态：项目最终实施扩展基线

## 1. 项目目标

开发一个能够运行 RV32 Linux 的完整系统模拟器，最终具备：

1. 自研的 RV32 CPU 核心。
2. 可加载 OpenSBI、设备树和自行交叉编译的 Linux 内核。
3. 可初始化并挂载 VirtIO 虚拟磁盘。
4. UART 终端和 Framebuffer 图形界面。
5. 通过 VirtIO 网络设备访问互联网。
6. 提供可配置的大容量 RAM 和文件后端虚拟磁盘。
7. 在解释器能力范围内提供流畅的 Framebuffer 显示，并明确区分宿主显示帧率与客户机软件渲染帧率。
8. 可测试、可调试、可逐步扩展的模块化结构。

项目的核心工作限定为 CPU 及 CPU 面向虚拟硬件的稳定接口。RAM、UART、CLINT、PLIC、VirtIO、SYSCON、Framebuffer 和图形窗口采用经过验证的既有设计，经过适配后部署在平台层。

## 2. 固定技术范围

### 2.1 CPU 最小 Linux 目标

- 单 Hart。
- Little Endian。
- 顺序解释执行。
- RV32IMA。
- Zicsr。
- Zifencei。
- Zicntr。
- M/S/U 三种特权级。
- Sv32 虚拟内存。
- 精确异常和中断。
- 64 位 `cycle`、`time`、`instret` 计数器及 RV32 高低位 CSR。

### 2.2 首次 Linux 启动后增加

- C 压缩指令扩展。
- 小型 TLB。
- SDL 图形窗口和键盘输入。
- VirtIO Net 和无需管理员权限的用户态 NAT。
- 可配置 RAM、文件后端大容量虚拟磁盘和动态设备树参数。
- GDB Remote Stub。
- 性能分析和指令执行优化。

### 2.3 第一阶段明确不实现

- F、D、V、H 扩展。
- 多 Hart/SMP。
- 流水线和周期精确微架构。
- Cache 一致性。
- JIT、动态二进制翻译、3D GPU 和真实 GPU 直通；如果未来把 CPU-only 全屏 60 FPS 改成硬目标，必须重新评估并另行扩展计划。

### 2.4 联网与系统级包管理的可兑现边界

- “能联网”是最终硬目标：客户机必须获得 IP、默认路由和 DNS，能够主动发起 TCP/UDP 连接并通过 HTTPS 下载文件。
- 当前 Buildroot 根文件系统只保留为启动和网络冒烟测试环境；Buildroot 官方不生成可供目标机在线安装的二进制包仓库，因此不作为最终包管理系统。
- Debian、Ubuntu 和 Alpine 的官方公共仓库当前只提供 `riscv64`，没有与本项目 `RV32/ILP32` ABI 匹配的 `riscv32` 二进制源。不得把这些 `riscv64` 源写入 RV32 客户机。
- M11 的正式方案采用 Yocto/OpenEmbedded 的 `qemuriscv32` 目标生成 RV32 用户态、包索引和项目自维护的软件源。Yocto 对 RV32 属于次级测试支持，必须在本模拟器上完成独立验收。
- 主包管理方案固定为 `PACKAGE_CLASSES = "package_ipk"`、`IMAGE_FEATURES += "package-management"` 和 `opkg`。软件源由同一套 Yocto 配置交叉构建并通过 HTTP/HTTPS 发布，不依赖不存在的公共 RV32 发行版仓库。
- 如果必须保留字面意义上的 `apt` 命令，允许用 Yocto 的 `package_deb` 生成项目自维护的 DEB 源；只有 `apt update/install/upgrade/remove` 在本模拟器中完成验证后才能启用。它不是 Debian/Ubuntu 官方源，也不承诺拥有 Debian 全量软件目录。
- Yocto 还可生成 RPM/DNF 源，但第一版不采用该路线，以避免增加包管理器自身的资源和依赖负担。
- 如果允许把 CPU 扩展为 RV64，则可直接采用 Debian/Ubuntu `riscv64` 仓库，但这会形成新的 CPU 架构项目，不纳入本计划。

依据：

- Debian 官方端口列表仅列出 `riscv64`：
  <https://www.debian.org/ports/>
- Ubuntu 官方支持架构仅列出 `riscv64`：
  <https://documentation.ubuntu.com/project/how-ubuntu-is-made/concepts/supported-architectures/>
- Alpine 官方架构矩阵仅列出 `riscv64`：
  <https://wiki.alpinelinux.org/wiki/Include:Architecture_support_matrix>
- Debian 的 RV32 页面只提供交叉编译说明和下游实验性 rootfs，不是官方二进制仓库：
  <https://wiki.debian.org/RISC-V/32>
- Buildroot 对二进制包管理的边界：
  <https://buildroot.org/downloads/manual/manual.html#faq-no-binary-packages>
- Yocto 5.3.2 将 RISC-V 32 位列为 `qemuriscv32` 次级测试目标：
  <https://docs.yoctoproject.org/5.3.2/ref-manual/yocto-project-supported-features.html>
- Yocto 运行时包管理、IPK/DEB/RPM 格式和软件源配置：
  <https://docs.yoctoproject.org/scarthgap/dev-manual/packages.html>

## 3. 不可破坏的架构边界

CPU 核心只能依赖以下接口：

1. 物理总线读写。
2. LR/SC 和 AMO 原子操作。
3. 六条 M/S 软件、定时器、外部中断输入线。
4. `time` 时钟源。
5. Reset 配置。
6. Snapshot 和 Commit Trace 调试接口。

CPU 不允许：

- 持有任何具体外设对象。
- 包含 UART、PLIC、CLINT、VirtIO 或 GUI 头文件。
- 调用设备 `tick()`。
- 读取磁盘文件。
- 直接操作 SDL。
- 允许总线反向修改 CPU 的异常或 CSR。

总线只返回结构化错误，CPU 负责将其转换为架构异常。

## 4. 模块划分

```text
core/
  自研 CPU 状态、译码、执行、CSR、Trap、MMU

platform/
  统一总线、DMA、整机装配、设备调度、镜像加载

devices/
  RAM、CLINT、PLIC、UART、VirtIO Block、VirtIO Net、SYSCON、Framebuffer

app/
  命令行入口和未来的 GUI 入口

tests/
  单元测试、架构测试、差分测试、Linux 集成测试
```

原有 `docs/` 和 `prestudy/` 仅作为学习资料保留，不参与正式模块，也不修改其中已有文件。

## 5. 固定物理地址布局

| 设备 | 基地址 | 范围/说明 |
|---|---:|---|
| CLINT | `0x02000000` | 64 KiB |
| PLIC | `0x0C000000` | 64 MiB 窗口 |
| NS16550A UART | `0x10000000` | 256 B，IRQ 10 |
| VirtIO Block | `0x10001000` | 4 KiB，IRQ 1 |
| VirtIO Net | `0x10002000` | 4 KiB，IRQ 2；M11 增加 |
| SYSCON | `0x11100000` | 4 KiB |
| Framebuffer | `0x40000000` | 大小由分辨率决定 |
| DRAM | `0x80000000` | 当前默认 64 MiB；M10 后默认 256 MiB，验证 512 MiB |

Linux Image 默认放置在 `0x80400000`，满足 RV32 内核 4 MiB 对齐要求。OpenSBI/BIOS 从 `0x80000000` 启动，DTB 放置在 RAM 顶部的安全区域。

## 6. 实施阶段与验收条件

### M0：整机框架和外设部署

内容：

- CMake 工程。
- `rv32_core` 稳定接口和 Reset/Snapshot 状态。
- 统一物理总线。
- RAM、CLINT、PLIC、UART、VirtIO Block、SYSCON、Framebuffer。
- Machine 装配层。
- 第三方来源和许可证记录。

验收：

- Debug 和 Release 均可构建。
- 所有框架测试通过。
- 未修改任何原有文件。
- CPU 尚不执行正式指令。

### M1：完整 RV32I

内容：

- Fetch/Decode/Execute/Commit。
- 全部 RV32I 指令。
- 对齐和访问错误。
- 非法指令。
- 精确提交。

验收：

- RV32I 单元测试和架构测试通过。
- 任何失败访存均不会产生部分提交。

### M2：M、A、Zicsr、Zifencei、Zicntr

验收：

- 乘除法全部边界条件通过。
- LR/SC、全部 RV32 Word AMO 通过。
- CSR 权限和读改写语义通过。

### M3：特权级、异常和中断

内容：

- M/S/U。
- Trap 委托。
- `MRET`、`SRET`、`WFI`。
- 中断优先级和向量入口。

验收：

- Machine/Supervisor 特权测试通过。
- CLINT 和 PLIC 能通过接口驱动 CPU Trap。

### M4：Sv32

内容：

- 两级页表。
- 4 KiB 页和 4 MiB 超级页。
- `SUM`、`MXR`、`MPRV`。
- A/D 位。
- `SFENCE.VMA`。

验收：

- 地址翻译、权限、Page Fault 和 Access Fault 测试通过。

第一版 MMU 不使用 TLB，以正确性优先。

### M5：OpenSBI 和 Linux 串口启动

验收：

- 显示 OpenSBI Banner。
- Linux 输出 `Linux version`。
- UART 控制台可交互。

### M6：VirtIO 根文件系统

验收：

- 初始化虚拟磁盘。
- Linux 识别 VirtIO Block。
- 挂载根文件系统并进入 Shell。
- 磁盘写入可以持久化。

### M7：C 扩展和图形界面

验收：

- RV32IMAC 软件通过架构测试。
- SDL 窗口显示 UART 终端或 Linux Framebuffer。
- 键盘输入能够送入 UART。

### M8：提交轨迹和差分调试基线

内容：

- Commit Trace。
- Spike 逐指令差分测试。
- RV32I、M、A、C 代表用例验证。

实现边界：

- 本阶段只建立可验证的 CPU 参考基线，不修改外设协议。
- 本阶段不实现 GDB Remote Stub、TLB、JIT 或性能快路径；这些工作后移，避免在缺少参考轨迹时优化出错误。

验收：

- Debug 和 Release 全量测试通过。
- 至少各选择一个 RV32I、M、A、C 用例与固定版本 Spike 比较。
- 权限级、PC、指令和通用寄存器写回逐条一致。

### M9：CPU 与 Framebuffer 性能基线、TLB 和快速解释路径

内容：

- 增加稳定的 MIPS、访存、页表遍历、Trap 和设备访问统计。
- 建立可重复的 CPU、Linux 启动和 Framebuffer 写入基准。
- 增加带 ASID/权限校验的小型 Sv32 TLB，并正确处理 `SFENCE.VMA`。
- 增加译码缓存和 Release 快速解释路径，减少每条指令的重复译码、快照和非必要分支。
- 调整设备轮询粒度，但中断、计时器和可见设备状态不得晚于规定的检查点。
- GUI 事件循环与 CPU 执行解耦，窗口关闭、F1/F2 切换和键盘输入不被长时间客户机执行阻塞。
- Framebuffer 使用脏矩形或脏页跟踪，只上传发生变化的区域；保留约 16 ms 的宿主显示节流和 SDL 加速纹理直接上传。
- 增加宿主显示 FPS、客户机 Framebuffer 写入带宽、丢帧和脏区域比例统计。

实现边界：

- 所有优化只位于 `core/` 内部或 Machine 调度层，不允许 CPU 依赖具体设备。
- 保留逐条参考解释模式和 Commit Trace；差分测试必须能够强制使用参考模式。
- 不实现流水线、乱序、周期精确 Cache 或多 Hart。
- 本阶段不实现 JIT，也不通过跳过异常、CSR、页表权限检查来换取速度。
- “宿主 60 FPS”只表示 SDL 能按约 60 Hz 呈现已有像素，不代表 RV32 客户机能每秒软件生成 60 个完整画面。
- 当前 640×480×32bpp 一帧为 1,228,800 字节；按 5.28M steps/s 计算，即使每条指令都完成一次 32 位 Store，理论上限也只有约 17 个全屏帧，真实 Linux 软件绘制预期约 3–10 FPS。
- 硬目标是输入、终端和局部更新流畅。CPU-only 全屏 60 FPS 不作为阻塞验收条件；若未来必须达到，需要重新规划 JIT 或 2D/GPU 外设。

验收：

- 在同一宿主、同一 Release 构建和同一 2000 万步 Linux 基准下，吞吐至少达到当前基线的 2 倍。
- 当前宿主基线记录为：2000 万 machine steps 用时 3.787 秒，约 5.28M steps/s。
- 所有单元、架构、Spike 差分和 Linux 启动测试继续通过。
- TLB 命中、失效、ASID、全局页、权限变化和 `SFENCE.VMA` 有专项测试。
- 宿主合成测试在窗口可用时达到 55–60 FPS，输入和 F1/F2 切换无肉眼可见卡死。
- 小区域变化不得触发宿主逐像素全屏转换；Linux Framebuffer 控制台滚动不再因宿主逐行上传出现撕裂。
- 分别记录全屏填充、全屏复制、文本滚动和局部动画的实测结果，不用单一 FPS 掩盖瓶颈。

2026-07-28 当前验收记录：

- 同一 Release 构建、同一 Linux/OpenSBI/DTB 和 2000 万 machine steps：
  `1.723 s`、`11.607 Msteps/s`，相对 `5.28 Msteps/s` 基线为
  `2.20x`。
- Release 合成 Framebuffer 设备路径：全屏填充 `449.35 MiB/s`，
  全屏复制 `871.66 MiB/s`，文本滚动 `862.78 MiB/s`，64x64 局部动画
  `441.69 MiB/s`；局部动画脏区比例为 `1.33%`。
- Debug 和 Release 各 `89/89` 测试通过；Linux 启动和 PLIC 中断仲裁
  继续通过。
- SDL 修复了 Windows 加速渲染器隐式等待垂直同步时与 16 ms 软件节流
  叠加造成的约 30 FPS 问题；真实窗口 55–60 FPS 仍需在同一宿主上复测
  后才能关闭本项。

### M10：大内存、文件后端磁盘和动态硬件描述

内容：

- 命令行支持配置 RAM 容量、Framebuffer 参数和磁盘文件。
- 默认 RAM 提升到 256 MiB，并验证 512 MiB Linux 配置。
- DTB 的 memory、Framebuffer 和设备节点由实际 MachineConfig 生成或可靠修补，不再与 64 MiB 固定值绑定。
- VirtIO Block 增加文件后端，按扇区读取和写入，不再把整个磁盘镜像复制到 `std::vector`。
- 支持至少 4 GiB、推荐 8 GiB 的稀疏 ext4 镜像，并保留小型内存后端供单元测试使用。

实现边界：

- 本阶段不修改 CPU ISA、MMU 语义或特权架构。
- 大磁盘不得按镜像容量等量占用宿主 RAM；宿主内存占用只允许随缓存窗口有限增长。
- 默认不承诺超过 512 MiB 的 Linux 可用 RAM；RV32 Linux 的内核虚拟地址布局需要单独验证后才能继续提高。
- 不在 Git 中提交磁盘镜像、内核、OpenSBI 或构建产物。

验收：

- 256 MiB 和 512 MiB 配置都能启动到 Shell，`free` 与设备树报告一致。
- 4 GiB 和 8 GiB 文件后端能够创建、挂载、读写、关机后重新启动并保持数据。
- 加载 8 GiB 稀疏磁盘时，模拟器宿主常驻内存不会接近 8 GiB。
- 越界、短读写、只读镜像、宿主 I/O 错误和异常退出有确定行为。

### M11：VirtIO 网络、可联网用户态和系统级包管理

方案：

- 前端设备采用 Linux 原生支持的 VirtIO MMIO Net，地址 `0x10002000`、PLIC IRQ 2。
- Windows 宿主后端采用 `libslirp` 用户态 NAT；无需 TAP、网桥、管理员权限或修改宿主网络设置。
- Linux 内核启用 `CONFIG_VIRTIO_NET`；先用现有 Buildroot 环境验证 DHCP、DNS 和 HTTPS，再切换到 Yocto `qemuriscv32` 包管理镜像。
- Yocto 镜像使用 RV32/ILP32、`package_ipk`、`package-management` 和 `opkg`，包含 DHCP 客户端、`ip`、DNS、CA 证书以及 `wget` 或 `curl`。
- 同一 Yocto 构建生成 RV32 IPK 包与索引，发布为项目自维护的 HTTP/HTTPS 软件源；镜像预置与其 ABI、C 库和版本完全匹配的源配置。
- `package_deb`/APT 作为兼容验证路线保留，但不是首个阻塞目标；禁止连接 Debian、Ubuntu 或 Alpine 的 `riscv64` 公共源。
- 保留后端抽象，以后可增加 TAP 或端口转发，但不能让 CPU 依赖网络实现。

Windows UCRT64 环境使用 MSYS2 的 `libslirp` 包：
<https://packages.msys2.org/packages/mingw-w64-ucrt-x86_64-libslirp>

实现边界：

- 第一版只实现单队列、基础收发、MAC、链路状态和必要 VirtIO 特性，不实现多队列、TSO、GSO、校验和卸载或零拷贝。
- 第一版默认只允许客户机主动访问外网，不开放宿主入站端口。
- 软件源必须由可复现的 Yocto 配置生成，包架构、ABI、C 库和内核用户接口必须一致；不得混装外部不兼容二进制包。
- 第一版源只承诺项目实际构建和验收的软件集合，不承诺 Debian/Ubuntu 规模的软件目录。
- 软件源发布目录与磁盘镜像均不提交 Git；仓库配置、配方、锁定版本、哈希和重建说明必须提交。
- 网络数据到达只能通过设备队列和 PLIC 中断影响 CPU，网络线程不得直接修改 CPU 状态。

验收：

- Linux 识别 `virtio_net`，客户机通过 DHCP 获得地址、默认路由和 DNS。
- 能解析域名，并通过 HTTP/HTTPS 下载固定测试文件并校验哈希。
- Yocto RV32 镜像启动到 Shell，`opkg update` 能从项目软件源刷新索引。
- `opkg install` 能安装一个带依赖的测试应用，应用可以运行；`upgrade`、`remove` 后包数据库和文件状态一致，重启后保持。
- 断网、错误源、损坏包、哈希不匹配、磁盘空间不足和依赖无法满足均有确定错误，不破坏已安装系统。
- APT 路线只有在自维护 DEB 源上的 `apt update/install/upgrade/remove` 全部通过时才可标记为可用；不得用命令存在代替功能验收。
- 连续收发、队列耗尽、坏描述符、超长包、重置和中断确认均有测试。
- 网络启用和关闭时，OpenSBI、Linux、磁盘与现有 87 项以上回归测试不退化。

## 7. 测试策略

测试分为六层：

1. 单元测试：位操作、译码、CSR、MMU、各外设寄存器。
2. 架构测试：RISC-V Architecture Test 和兼容的 `riscv-tests`。
3. 差分测试：逐指令比较 PC、寄存器、内存写入和 Trap。
4. 集成测试：OpenSBI、Linux、VirtIO 根文件系统和 GUI。
5. 网络测试：VirtIO 队列、DHCP、DNS、TCP/UDP、HTTPS、断网、重置和错误描述符。
6. 性能回归：固定宿主、Release 构建和固定镜像下记录 steps/s、启动时间、磁盘吞吐、网络吞吐、Framebuffer 写入带宽与显示 FPS。

所有阶段必须先通过自动测试，再进入下一阶段。

## 8. 第三方代码策略

外设行为参考 `bane9/rv64gc-emu`，其 LICENSE 为 MIT。项目只吸收设备模型思想和必要实现，不引入其 CPU、CSR、MMU 或指令执行代码。

所有吸收或改写的第三方实现必须：

- 在 `THIRD_PARTY_NOTICES.md` 中记录来源。
- 保留许可证文本。
- 修复越界、未对齐访问、CPU 反向耦合和宿主退出等不适合本项目的问题。

## 9. GitHub 交付策略

每个里程碑形成独立、可构建、可测试的提交。首次 GitHub 提交对应 M0 框架版；在用户提供目标账号和仓库信息后推送，不在提交中包含构建产物、磁盘镜像、账号或密钥。
