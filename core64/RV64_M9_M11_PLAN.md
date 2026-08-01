# RV64 M9–M11 后续实施计划

状态：当前唯一的 RV64 后续实施计划。

本文件承接已经冻结并完成的
[`RV64-M1` 至 `RV64-M8`](PROJECT_PLAN.md)，不改写这些阶段的实现与验收
结论。后续按 M9、M10、M11 顺序推进；每个子阶段测试通过后提交，再进入下一
子阶段。

## 最终目标

在保持 RV32/RV64 CPU 解耦、CPU/外设解耦的前提下：

1. 为 RV64 Machine 提供可联网的 VirtIO 网卡和准确实时时钟。
2. 将 RV64 CPU 从 RV64IMAC 完整扩展到 RV64GC 所需的 RV64F、RV64D。
3. 运行 Debian 13 riscv64/LP64D 用户态，使用官方软件源完成 APT 验收。
4. 为实际安装应用提供足够且可配置的 RAM、文件后端磁盘和确定的持久化行为。

## 不可破坏的边界

- `core32/` 和 `core64/` 不共享译码、执行、CSR、Trap、MMU 或寄存器状态。
- `core32/` 不增加浮点寄存器，也不装配网络设备。
- CPU 只通过架构无关总线和中断线访问设备，不包含 VirtIO、libslirp、
  RTC、PLIC、SDL 或宿主网络头文件。
- VirtIO Net 位于 `devices/`；libslirp 位于宿主适配层；只有
  `platform64/` 决定是否装配网卡。
- RV32/RV64 的 DTS、镜像、启动命令和测试产物使用独立名称，不互相覆盖。
- 不通过跳过非法指令、伪造浮点结果或把 LP64D 用户态改成非官方软浮点 ABI
  来绕过 CPU 实现。

## RV64-M9：网络设备、用户态 NAT 与 RTC

状态：已完成设备和平台实现；Debian 公网 HTTPS 与 APT 留到 M11 做最终验收。

已实现：

- 架构无关的 legacy VirtIO-MMIO Net，包含独立 RX/TX virtqueue、DMA、
  used ring、中断确认、复位、MAC 和链路状态。
- 网络硬件模型与以太网帧后端分离，CPU 和设备层不依赖 libslirp。
- RV64 专用 MMIO 地址、PLIC IRQ、Machine 装配和 DTS 节点；RV32 未装配。
- Windows UCRT64 下的 libslirp 用户态 NAT，不要求 TAP、网桥或管理员权限。
- Goldfish RTC，供 Linux 使用宿主实时时间。

已有验收证据：

- Buildroot RV64IMAC 启动并识别 `virtio_net`。
- DHCP 地址为 `10.0.2.15`，默认网关为 `10.0.2.2`，DNS 为 `10.0.2.3`。
- Debug/Release 自动测试和 RV32 回归通过。

阶段关闭条件：

- VirtIO 队列、错误描述符、短缓冲、DMA 失败、复位和中断有确定测试。
- 网络关闭或宿主后端不可用时，模拟器仍可启动且不会阻塞 CPU。
- 公网 DNS、HTTPS 和 Debian 仓库访问在 M11 的真实系统环境统一验收。

## RV64-M10：完整 RV64F/RV64D

状态：已完成（2026-07-30）。

### M10.1：状态、CSR、访存、移动和压缩访存

状态：已完成（2026-07-29）。

- 增加独立的 32×64 位浮点寄存器文件。
- 实现 `fflags`、`frm`、`fcsr`、`mstatus.FS`、`sstatus.FS` 和 `SD` 汇总位。
- 实现 `FLW/FSW/FLD/FSD`、整数/浮点位移动指令。
- 实现 `C.FLD/C.FSD/C.FLDSP/C.FSDSP`。
- 实现 NaN-boxing；FS=Off 时浮点指令产生非法指令，写浮点状态后进入 Dirty。

验收：

- 正常、未对齐、页故障、总线失败和跨页访问均有测试。
- CSR 权限、别名、FS 状态转换和 Trap 精确性均有测试。
- Debian 动态加载器当前首个失败点 `C.FSD fs0,112(a0)` 能按规范执行。

验收记录：

- 已实现独立 32×64 位 FPR、`fflags/frm/fcsr`、FS/SD 和浮点提交信息。
- 已实现 `FLW/FSW/FLD/FSD`、`FMV.X.W/FMV.W.X/FMV.X.D/FMV.D.X`、
  `C.FLD/C.FSD/C.FLDSP/C.FSDSP`。
- `FLW`/`FMV.W.X` NaN-boxing、`FMV.X.W` RV64 符号扩展、FS=Off、
  未对齐、总线失败、Sv39 页故障和参考/快速模式一致性均有专项测试。
- Debian 首个失败指令原始编码 `0xB920` 已完成译码和执行测试。
- Debug 与 Release 均为 106/106 通过，RV32 全量回归无退化。
- M10 完成前不修改 `misa`、DTS、OpenSBI 或 Linux 的 RV64IMAC 宣告。

### M10.2：算术、融合运算与舍入

状态：已完成（2026-07-29）。

- 实现 F/D 的加、减、乘、除、平方根和 fused multiply-add 指令族。
- 支持 RNE、RTZ、RDN、RUP、RMM 和动态舍入模式。
- 正确累计 NV、DZ、OF、UF、NX。
- 明确定义 NaN、无穷、正负零和 subnormal 的位级语义。
- 使用固定版本、确定性的软浮点实现，不依赖宿主浮点环境。

验收：

- 每种舍入模式、异常标志和特殊值均有边界测试。
- 结果位模式和 `fflags` 与 Spike/官方浮点测试一致。

验收记录：

- 已实现 `FADD/FSUB/FMUL/FDIV/FSQRT` 以及
  `FMADD/FMSUB/FNMSUB/FNMADD` 的 S/D 形式。
- RNE、RTZ、RDN、RUP、RMM 与动态 `frm` 均由位级确定性后端执行；
  保留模式会精确产生非法指令。
- NV、DZ、OF、UF、NX 直接按 RISC-V 位布局累积到 `fflags`，覆盖
  signaling NaN、除零、溢出、下溢、非精确结果和无效 NaN-box。
- 使用 Berkeley SoftFloat Release 3e 的 RISC-V specialization，固定到
  `a0c6494cdc11865811dec815d5c0049fba9d82a8`，不依赖宿主 FPU 或宿主舍入环境。
- Debug 与 Release 均为 107/107 通过，包含 RV32 全量回归。
- M10 完成前继续保持 `misa`、DTS 和启动软件的 RV64IMAC 宣告。

### M10.3：比较、分类、符号与转换

状态：已完成（2026-07-29）。

- 实现 `FSGNJ*`、`FMIN/FMAX`、`FEQ/FLT/FLE`、`FCLASS`。
- 实现 S/D 互转。
- 实现 W/WU/L/LU 与 S/D 的全部整数浮点转换。

验收：

- 覆盖精确边界、溢出、NaN、无穷、符号扩展和饱和值。
- RV64F 与 RV64D 指令集合不存在仅为启动样例补齐的缺口。

验收记录：

- 已实现 S/D 的 `FSGNJ/FSGNJN/FSGNJX`、`FMIN/FMAX`、
  `FEQ/FLT/FLE` 和 `FCLASS`。
- 已实现 `FCVT.S.D/FCVT.D.S`，以及 W/WU/L/LU 与 S/D 的双向全部
  16 种整数/浮点转换形式。
- `FMIN/FMAX` 覆盖单 NaN、双 NaN、sNaN 与正负零；FEQ 使用静默比较，
  FLT/FLE 对任意 NaN 置 NV。
- RV64 的 W/WU 转换结果均按规范从 32 位符号扩展到 XLEN；NaN、无穷、
  负数转无符号数和范围溢出使用 RISC-V 饱和值。
- 分类覆盖十个 `FCLASS` 类别，无效单精度 NaN-box 按规范视为 canonical NaN。
- Debug 与 Release 均为 108/108 通过，包含 RV32 全量回归。

### M10.4：整核与软件栈验收

状态：已完成（2026-07-30）。

- 完成 F/D 裸机、架构测试和 Spike Commit Trace 差分。
- Debug、Release、RV32 全量和 RV64IMAC 原有测试全部通过。
- OpenSBI、DTS 和 Linux ISA 宣告只有在实现完成后才更新为实际支持的
  `rv64imafdc`。
- Debian 13 PID 1、动态加载器、systemd、Shell 和 DHCP 正常运行。

验收记录：

- `misa`、Core ISA 字符串和 DTS 统一声明
  `rv64imafdc_zicntr_zicsr_zifencei`；OpenSBI 实机输出为
  `rv64imafdc`，Linux 识别 F/D/C 与 Zicntr。
- 引入官方 `riscv-tests` 的 11 个 RV64UF 与 12 个 RV64UD 用例；
  Debug 与 Release 全量测试均为 131/131 通过，包含 RV32 全量回归。
- 12 个代表性 RV64UF/RV64UD 用例与固定 Spike 版本逐提交比较，
  GPR、FPR、PC 和指令均一致。
- 新增启用 `CONFIG_FPU` 的独立 RV64GC Linux 配置和镜像；保留原
  RV64IMAC Buildroot 回归配置与镜像，不覆盖 RV32 或 RV64IMAC 产物。
- Debian 13 的 LP64D 动态加载器、systemd 257 PID 1、`/bin/sh`、
  `dpkg --print-architecture` 与 `apt 3.0.3 (riscv64)` 均已运行。
- Debian 使用 VirtIO Net/libslirp 获得 `10.0.2.15/24`，默认路由
  `10.0.2.2`，DNS `10.0.2.3`。

## RV64-M11：Debian、APT 与可长期使用的资源配置

状态：进行中；M10 已完成，当前进入文件后端磁盘、资源配置与 APT 验收。

### M11.1：文件后端 VirtIO Block

状态：已完成（2026-07-30）。

- 增加架构无关的 `BlockStorage` 接口，以及内存、文件两种后端。
- RV64 `--boot-disk` 直接打开镜像文件，VirtIO 请求按 4 KiB 块读写；
  不再启动时整盘读入、退出时整盘重写。
- 正常退出会刷新文件缓冲；写请求成功后立即标记 dirty，部分请求失败时也
  不会漏掉已经发生的持久化写入。
- 保留内存后端供 RV32、单元测试和小型裸机环境使用，CPU 核心与文件 API
  无依赖。

验收记录：

- 覆盖内存/文件读写、越界、缺失文件、非整扇区文件、刷新和重新打开后的
  数据一致性。
- 768 MiB Debian 磁盘启动时宿主工作集实测约 288 MiB，不再随磁盘容量
  增加到约 1.9 GiB；Linux 能挂载 ext4，退出时文件后端成功同步。
- Debug 与 Release 全量测试均为 132/132，通过 RV32 与 RV64 启动回归。

### M11.2：可配置 RAM 与 DTB 一致性

状态：已完成（2026-08-01）。

- RV64 命令行新增 `--ram-mib <MiB>`，支持 64–4096 MiB，默认保持 256 MiB；
  选项只进入 RV64 平台，不改变 RV32 的命令、内存映射或测试。
- 启动前由 `platform64` 中的受限 FDT 解析器定位顶层 `memory` 节点，并将
  64 位 `<base, size>` 元组更新为实际分配值；CPU 核心不解析 DTB。
- FDT 魔数、头部范围、结构 token、字符串偏移、内存节点、`reg` 长度与 DRAM
  基址均进行检查；不兼容输入确定失败，不通过字节搜索误改其他属性。
- RAM 仍采用连续宿主内存，命令行上限不是性能承诺；大配置需要宿主机具备相应
  可用内存，Debian 的推荐起点为 512 MiB。

验收记录：

- 单元测试覆盖有效 64 位内存范围修改、错误 DRAM 基址和损坏 FDT 魔数。
- 新增 512 MiB 命令行/DTB 启动装载测试；Debug 与 Release 全量均为
  133/133 通过，RV32 回归无变化。
- 使用 RV64 Linux 实际启动 512 MiB 配置，Linux 报告
  `Memory: 474028K/524288K available`，物理范围为
  `0x80000000-0x9fffffff`，与命令行及修正后的 DTB 一致。

软件基线：

- Debian 13 `riscv64`、ELF64、LP64D、RV64GC。
- 根文件系统由 `scripts/build_debian_rv64_rootfs.sh` 生成，默认输出
  `boot-images/debian-13-riscv64-apt.ext4`。
- 使用 Debian 官方 riscv64 软件源，不维护私有 RV32 包仓库。

实现内容：

- 完成公网 DNS、TCP/UDP、HTTPS 和 CA 证书链验收。
- 可配置 RV64 RAM 与运行时 DTB 同步已在 M11.2 完成。
- 文件后端 VirtIO Block 已在 M11.1 完成。
- 支持可扩展的稀疏 ext4 磁盘，并保证正常关机后的数据持久化。
- 保留 RV64IMAC Buildroot 作为无 F/D、无 APT 的快速回归基线。

最终验收：

- `date -u`、`rtc0`、IP 地址、默认路由和 DNS 正常。
- 通过 HTTPS 访问 Debian 官方仓库，`apt update` 成功。
- 安装、运行、升级并卸载至少一个小型软件包。
- 关机后磁盘正确回写，再次启动后包数据库和已安装文件保持一致。
- 断网、错误源、证书错误、磁盘空间不足和损坏包均给出确定错误，不破坏
  已安装系统。
- 大磁盘不按镜像容量等量占用宿主 RAM；RAM 和磁盘配置与 Linux 报告一致。
- Debug/Release、RV32 全量、RV64 Buildroot、OpenSBI/Linux 和网络设备测试
  全部通过。

## 提交规则

- M10.1、M10.2、M10.3、M10.4 和 M11 各形成至少一个可构建、可测试的提交。
- 每次提交记录新增指令/设备行为、测试范围、镜像要求和已知限制。
- OpenSBI、Linux、根文件系统和其他大型构建产物保留在 `boot-images/`，
  不提交 Git；可复现配置、脚本和哈希说明提交 Git。
