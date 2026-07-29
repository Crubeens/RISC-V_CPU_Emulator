# RV64 网络与 APT 实施计划

## 目标与边界

最终目标是在 RV64 客户机 Linux 中由内核原生 `virtio_net` 驱动识别网卡，
通过宿主机用户态 NAT 获取地址、解析 DNS、访问 HTTPS，并成功执行系统级
`apt update` 与软件包安装。

- `core32/` 与 `core64/` 不包含网卡、协议栈或宿主网络代码。
- VirtIO 网卡位于 `devices/`，只依赖架构无关的 MMIO、DMA 和中断接口。
- 宿主网络后端与 VirtIO 硬件模型分离，可替换为 libslirp、TAP 或测试后端。
- 第一条产品路径只在 `platform64/` 装配网卡；RV32 平台保持不变。
- RV32 与 RV64 的 CPU、平台、设备树、镜像和验收命令不得互相覆盖。

## 为什么暂不接入 RV32

VirtIO 网卡本身可以被 RV32 和 RV64 共用，但 Debian 与 Ubuntu 没有可用于
本项目目标的官方 riscv32 二进制软件包仓库。单独为 RV32 接入网络只能验证
BusyBox 网络工具，不能完成系统级 APT 目标，还会增加一套内核和根文件系统
验收成本。因此本阶段保留通用设备能力，但只由 RV64 Machine 装配。

## NET-M1：架构无关的 VirtIO-MMIO 网卡

状态：已完成。

内容：

- 实现 legacy VirtIO-MMIO transport 与网络设备 ID。
- 实现独立 RX/TX virtqueue、描述符链、available/used ring 和 DMA。
- 提供固定、可配置的本地管理 MAC 地址和链路状态。
- 实现队列完成中断与 ACK。
- 定义与宿主网络实现无关的以太网帧后端接口。

验收：

- 寄存器、配置空间、非法队列和复位行为有自动测试。
- TX 能从客户机描述符链提取完整以太网帧。
- RX 能把 VirtIO net header 与以太网帧写入客户机缓冲区。
- DMA 失败、短缓冲区、错误描述符和超长帧结果确定。
- RV32/RV64 CPU 测试无回归。

## NET-M2：RV64 平台接入

状态：已完成。

内容：

- 在 RV64 地址图中加入独立 VirtIO 网卡 MMIO 区域和 PLIC IRQ。
- 在 RV64 Machine 中装配网卡并把中断电平接入 PLIC。
- 更新 RV64 DTS；RV32 DTS 和 RV32 Machine 不变。
- 使用 Linux 6.12.96 验证 `virtio_net` 探测和接口创建。

验收：

- DTB 自动测试确认地址、中断和兼容字符串。
- 平台测试确认 MMIO、DMA 和 PLIC 中断调用链。
- RV64 Linux 出现 VirtIO 网络接口，RV32 设备图不出现该网卡。

## NET-M3：宿主机用户态 NAT

状态：已完成。Buildroot 已通过 DHCP 获得 `10.0.2.15`，默认网关为
`10.0.2.2`，DNS 为 `10.0.2.3`。

内容：

- 以可选宿主适配层接入 libslirp，不把其头文件暴露给设备或 CPU。
- 支持 DHCP、DNS、ARP、ICMP、UDP、TCP 和 HTTPS 所需的数据通路。
- 默认不要求管理员权限、TAP 网卡、网桥或宿主路由配置。
- 增加网络统计、错误诊断和有界收发队列。

验收：

- 客户机自动获得地址、默认路由和 DNS。
- 能 ping 用户态网关，能完成 DNS 查询与 HTTP/HTTPS 请求。
- 空闲、断网、丢包和关闭过程无死锁、无越界、无无限队列增长。

## NET-M4：Debian riscv64 与 APT

状态：根文件系统、DHCP、RTC 与官方源已准备；等待 RV64F/D CPU 前置阶段。

内容：

- 生成独立的 Debian 13 riscv64 根文件系统，不覆盖 Buildroot 或 RV32 镜像。
- 配置官方 Debian riscv64 软件源、CA 证书和 DHCP。
- 实现 Goldfish RTC，使客户机时间满足仓库有效期和 TLS 证书校验。
- 保留现有 OpenSBI、Linux、Buildroot 启动路径作为回归基线。

前置条件：

- Debian 13 官方 riscv64 端口使用 `LP64D` 和 RV64GC 基线。
- 当前 RV64IMAC CPU 能启动 Debian 内核，但动态加载器在
  `C.FSD fs0,112(a0)` 触发非法指令。
- 必须先完整实现 RV64F、RV64D、浮点 CSR/状态和 RV64C 浮点压缩指令；
  不使用跳过非法指令或伪造软浮点 ABI 的兼容性补丁。

验收：

- RV64 Linux 从 VirtIO 磁盘启动并通过 VirtIO 网卡联网。
- 系统时间和 CA 信任可满足 HTTPS。
- `apt update` 成功。
- 安装、运行并卸载一个小型软件包成功，虚拟磁盘正确回写。
- Debug/Release 自动测试、RV32 全量测试和 RV64 原有启动测试全部通过。

RV64F/D 的独立实施与验收边界见
[`core64/RV64FD_APT_PLAN.md`](../core64/RV64FD_APT_PLAN.md)。
