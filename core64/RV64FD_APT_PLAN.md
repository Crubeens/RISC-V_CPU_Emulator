# RV64F/D 与 Debian APT 前置计划

## 原因

Debian 13 官方 riscv64 用户态是 ELF64、RVC、double-float ABI，声明的 ISA
包含 `rv64imafdc`。当前 RV64IMAC CPU 已能启动同一内核、VirtIO 磁盘、
VirtIO 网卡和 Goldfish RTC，但 PID 1 的动态加载器会执行 `C.FSD`，因此
不能用只实现少量浮点指令的方式运行 APT。

本计划是独立于已固定 RV64-M1 至 M8 的后续扩展，不改写原阶段验收结论。

## 解耦边界

- 仅修改 `core64/` 的 RV64 CPU 状态、译码、执行、CSR 和提交结果。
- RV32 CPU 不增加浮点状态，不包含 RV64 头文件，不改变 RV32 测试镜像。
- VirtIO Net、libslirp、RTC、PLIC 和 Machine 保持外设/平台层实现。
- 浮点寄存器使用独立 32×64 位寄存器文件；整数寄存器与浮点寄存器不混用。

## FD-M1：状态、访存、移动和压缩访存

- 增加 `f0`–`f31`、`fflags/frm/fcsr` 和 `mstatus.FS`。
- 实现 `FLW/FSW/FLD/FSD`。
- 实现 `FMV.X.W/FMV.W.X/FMV.X.D/FMV.D.X`。
- 实现 RV64C 的 `C.FLD/C.FSD/C.FLDSP/C.FSDSP`。
- 实现单精度写入 64 位浮点寄存器时的 NaN-boxing。

验收：

- 对齐、页故障、总线失败和寄存器编号均有确定结果。
- FS=Off 时浮点指令非法；写浮点状态后 FS 进入 Dirty。
- 与 Spike 的访存、移动、压缩指令提交结果一致。

## FD-M2：算术、舍入和异常标志

- 实现 F/D 加、减、乘、除、平方根和 fused multiply-add 指令族。
- 支持 RNE、RTZ、RDN、RUP、RMM 与动态舍入模式。
- 正确累计 NV、DZ、OF、UF、NX。
- 实现规范要求的 canonical NaN、无穷、正负零和 subnormal 语义。

验收：

- 使用与 RISC-V 语义一致的确定性软浮点实现，不依赖宿主当前舍入环境。
- 每种舍入模式和异常标志均有专项边界测试。
- 与 Spike/官方浮点测试进行差分。

## FD-M3：比较、分类、符号和转换

- 实现 `FSGNJ*`、`FMIN/FMAX`、`FEQ/FLT/FLE`、`FCLASS`。
- 实现 S/D 互转。
- 实现 W/WU/L/LU 与 S/D 的全部整数浮点转换。
- 覆盖越界、NaN、无穷、精确边界和符号扩展。

验收：

- 结果位模式、整数饱和值、NaN 行为和 fflags 与规范/Spike 一致。

## FD-M4：特权状态、上下文与整机验收

- 完成浮点 CSR 别名、访问权限和 `mstatus/sstatus.FS` 汇总位。
- DTS/OpenSBI ISA 宣告由 `rv64imac` 更新为实际完成的 `rv64imafdc`。
- Debug/Release、RV32 全量、Spike F/D 和裸机测试全部通过。
- Debian 13 PID 1、systemd、DHCP 和 shell 正常运行。

## APT 最终验收

- `date -u` 与宿主时间一致，`rtc0` 正常。
- `ip address`、默认路由和 DNS 正常。
- HTTPS 访问 Debian 官方仓库成功。
- `apt update` 成功。
- 安装、运行并卸载一个小型软件包成功。
- 客户机关机后 ext4 正确回写，再次启动仍可使用 APT。
