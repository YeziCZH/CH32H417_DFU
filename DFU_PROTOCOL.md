# CH32H417 DFU 主机协议说明

本文是本工程主机端实现的协议契约。它描述标准 USB DFU 1.1、DfuSe 地址扩展、
CH32H417 Flash 地址和 RISC-V 跳转规则。现成调用工具见
[`scripts/dfu_download.py`](scripts/dfu_download.py)。

## USB 身份

| 字段 | 值 |
|---|---|
| VID:PID | `0483:DF11` |
| USB 版本 | USB 2.0 High-Speed |
| Interface Class | `0xFE` Application Specific |
| Interface Subclass | `0x01` Device Firmware Upgrade |
| Interface Protocol | `0x02` DFU mode |
| DFU 版本 | `0x011A` |
| `wTransferSize` | 512 字节 |
| `bmAttributes` | `0x0B`：Download、Upload、Will Detach；非 Manifestation Tolerant |
| DfuSe interface | `@Flash/0x08004000/118*8Kg` |

Windows 主机应为该接口绑定 WinUSB。当前硬件基线为 High-Speed；Full-Speed
描述符存在，但实际 Full-Speed 链路尚未验证。

## 地址空间

| 区域 | 物理编程地址 | V3F 执行 alias | 大小 | DFU 权限 |
|---|---:|---:|---:|---|
| Bootloader | `0x08000000..0x08003FFF` | `0x00000000..0x00003FFF` | 16 KiB | 禁止 |
| Application | `0x08004000..0x080EFFFF` | `0x00004000..0x000EFFFF` | 944 KiB | 读、写、擦除 |
| Flash end | `0x080F0000` | `0x000F0000` | end-exclusive | 仅作边界 |

主机所有地址命令都使用 `0x08000000` 区域的物理地址。APP 链接脚本使用零基执行
alias：

```text
execution_alias = physical_address - 0x08000000
```

例如镜像写入 `0x08020000` 时，APP 的 linker FLASH ORIGIN 和 `_start` 必须是
`0x00020000`。

## DFU Class Request

| 请求 | bRequest | 方向 | 用途 |
|---|---:|---|---|
| `DETACH` | 0 | Host to Device | 仅适用于 Runtime 转 DFU；当前 DFU-mode-only 固件不提供该流程 |
| `DNLOAD` | 1 | Host to Device | 命令、固件数据或零长度 manifestation |
| `UPLOAD` | 2 | Device to Host | Flash 读取 |
| `GETSTATUS` | 3 | Device to Host | 获取 bStatus、poll timeout 和状态，并推进状态机 |
| `CLRSTATUS` | 4 | Host to Device | 从 `dfuERROR` 恢复到 `dfuIDLE` |
| `GETSTATE` | 5 | Device to Host | 获取当前 DFU 状态 |
| `ABORT` | 6 | Host to Device | 取消会话、丢弃未刷新的 cache 并恢复默认地址 |

Class Interface 的 Host-to-Device `bmRequestType` 为 `0x21`，Device-to-Host 为
`0xA1`，`wIndex` 为接口 0。

设备只枚举 Protocol `0x02` 的 DFU-mode interface，没有 Protocol `0x01` Runtime
interface。尽管当前功能描述符的 `bmAttributes` 数值包含 Will Detach，主机不应把
DETACH 当作可用的 Runtime 切换入口。进入 DFU 由 UART4 RX 高电平、调试串口
Enter/Space、固定 SRAM magic/reset，或默认 APP 入口无效决定。

## DFU 入口和不活动超时

- 硬件入口：Bootloader 让 UART4 TX (PC6) 持续输出高电平，并把 RX (PC7) 配置为
  下拉输入。短接 PC6/PC7 或由后级主控把 PC7 拉高时进入 DFU；PC7 悬空时保持低电平。
- 调试串口入口：Bootloader 启动 500 ms 窗口内，USART1/COM4 RX 收到 Enter
  （`0x0D` 或 `0x0A`）或空格（`0x20`）时进入 DFU。APP 已经运行后需要 APP
  自行写 magic 并复位。
- 软件入口：APP 向 `0x201100FC` 写入 `0x44465521`，执行 RISC-V `fence rw, rw`
  后调用 `NVIC_SystemReset()`。Bootloader 读取后立即清除 magic。
- 兜底入口：默认 APP `0x08004000` 入口越界、未对齐，或首字为 `0xE339E339`、
  `0x00000000`、`0xFFFFFFFF` 时，无条件进入并保持 DFU。

进入 DFU 后采用协议不活动超时。未显式触发 DFU、但因默认 APP 无效而进入 DFU 时，
保持 2 秒超时；由 UART4 RX 高电平、调试串口 Enter/Space 或 SRAM magic 显式触发时，
超时窗口延长到 30 秒，方便主机枚举、绑定和人工启动下载命令。每个 DFU class
request，包括 GETSTATUS、GETSTATE、Download、Upload、CLRSTATUS 和 ABORT，都会
重新计时，因此活动传输可持续超过该窗口。超时会取消未完成的 Download/Upload 和
未刷新的 RAM cache，并仅尝试默认 APP；默认 APP 首字无效时继续保持 DFU。

若 manifestation 选择的执行入口无效，固件会锁定当前 DFU 会话，不再因不活动超时
跳回另一个默认 APP。主机必须下载有效镜像并完成新的 manifestation，或复位设备。
Flash 操作处于错误状态时同样禁止超时启动，主机通过 GETSTATUS 读取错误并用
CLRSTATUS 恢复后，不活动计时才重新生效。

主机每次 DNLOAD 或命令后必须循环 GETSTATUS，按照返回的三字节
`bwPollTimeout` 等待，直到状态成为 `dfuDNLOAD-IDLE`。不要在设备仍为
`dfuDNBUSY` 时发送下一个 block。

## 标准 DFU 寻址

标准模式不发送地址命令。Download 和 Upload 都从 block 0 开始：

```text
physical_address = 0x08004000 + block_number * 512
```

最大合法数据地址为 `0x080EFFFF`。Upload 到达 `0x080F0000` 时返回零长度包；
越界 Download 进入 `dfuERROR` 并报告 `errADDRESS`。

标准 DFU 示例：

```powershell
python scripts/dfu_download.py app.bin --standard
python scripts/dfu_download.py `
  --upload readback.bin --standard --length 0x2000
```

## DfuSe 地址扩展

DfuSe 命令使用 DNLOAD block 0，payload 必须正好是五字节：

```text
byte 0: command
byte 1..4: physical_address, little-endian uint32
```

| 命令 | 编码 | 地址要求 | 作用 |
|---|---:|---|---|
| `SET_ADDRESS_POINTER` | `0x21` | APP 区内任意地址 | 设置后续 DfuSe Download/Upload 基址 |
| `ERASE` | `0x41` | APP 区内且按 8 KiB 对齐 | 擦除一个 8 KiB 扇区 |
| `SET_EXEC_ADDRESS` | `0x51` | APP 区内且按 4 字节对齐 | 设置本次 manifestation 后的物理跳转地址 |

发送 SET_ADDRESS 后，数据从 block 2 开始：

```text
physical_address = address_pointer + (block_number - 2) * 512
```

ERASE 不修改 address pointer，SET_EXEC_ADDRESS 也不修改下载地址。下载地址与执行
地址相互独立，因此可以把镜像写到一个地址，并明确选择同一个物理地址执行。

只有 `wValue == 0`、`wLength == 5` 且首字节为上述三个编码时才解释为命令。
普通数据 block 33、65、81 不会因为 block 编号等于 `0x21/0x41/0x51` 而被误判。
需要注意，一个恰好只有五字节、且首字节等于命令编码的标准 block 0 会产生歧义；
常规完整固件的首块为 512 字节，不受影响。

## DfuSe Download 时序

1. 可选：对每个目标扇区发送 ERASE，并在每条命令后轮询 GETSTATUS。
2. 发送 SET_ADDRESS_POINTER，并轮询到 `dfuDNLOAD-IDLE`。
3. 从 block 2 开始发送最多 512 字节的 DNLOAD。
4. 每个 block 后轮询 GETSTATUS，等待 `dfuDNLOAD-IDLE`。
5. 可选：发送 SET_EXEC_ADDRESS，并轮询完成。
6. 发送零长度 DNLOAD，开始 manifestation。
7. 轮询 GETSTATUS，直到设备跳转/断开，或因入口无效而回到 `dfuIDLE`。

主机工具示例：

```powershell
python scripts/dfu_download.py app.bin `
  --addr 0x08020000 --erase --exec 0x08020000
```

`--erase` 是显式整扇区擦除，适合确定目标内容不需要保留的完整更新。不使用
`--erase` 时，固件通过 Read-Modify-Write 保留未覆盖数据，并可比较扇区后跳过
相同内容。需要获得差分收益时不要预先擦除。

## Upload 时序

标准 Upload 从 block 0 开始。DfuSe Upload 的时序为：

1. 发送 SET_ADDRESS_POINTER 并轮询完成；
2. 无需 ABORT，立即发送 UPLOAD block 2；
3. 后续 block 号递增，每次最多请求 512 字节；
4. 收到短包/零长度包时结束；如果主机在固定长度处主动停止，则发送 ABORT；
5. ABORT 或自然短包结束后，address pointer 恢复到 `0x08004000`。

示例：

```powershell
python scripts/dfu_download.py `
  --upload readback.bin --addr 0x08020000 --length 0x4000
```

Upload 只读取已经写入 Flash 的数据，不读取尚未 manifestation 的脏 cache。
进行中的 Download 应先正常结束，或用 ABORT 明确取消。

## Flash 行为

- 擦除粒度：8 KiB；
- 快速编程页：256 字节；
- DFU block：512 字节；
- 擦除后的字：`0xE339E339`，字节序列为 `39 E3 39 E3 ...`；
- 每个脏扇区写入前比较完整 8 KiB，完全相同时跳过擦除和编程；
- 变化扇区执行整扇区擦除、256 字节分页编程和逐字校验；
- 非对齐短写和跨扇区写通过 8 KiB RAM cache 执行 RMW；
- ABORT 丢弃尚未刷入 Flash 的 cache，但已经因切换扇区而刷新的数据不会回滚。

更新不是整镜像原子事务。跨扇区下载中断可能留下新旧混合镜像，生产 APP 应增加
镜像完整性、版本和启动确认元数据。

## Manifestation 和 RISC-V 跳转

Release 固件收到零长度 DNLOAD 后刷新最后一个脏扇区，等待约 50 ms，然后检查
所选物理入口的地址和首字：

- 首字为 `0xE339E339`、`0x00000000` 或 `0xFFFFFFFF`：不执行，恢复到 `dfuIDLE`；
- 其他值：USB deinit，清理 SysTick/GPIO/USART/RCC，通过 Software IRQ 执行 `jr`
  跳到零基 alias。

APP 镜像必须从其链接地址上的 RISC-V `_start` 指令开始。这里不存在 Cortex-M 的
`[MSP, Reset_Handler]` 向量表解释。Bootloader 只做上述轻量入口检查，不验证入口
是否真的是有效指令，也不验证完整镜像。

SET_EXEC_ADDRESS 只影响当前 DFU 会话，不持久化。ABORT、总线 reset、无效入口返回
DFU 或重新启动都会把地址状态恢复到默认 `0x08004000`。

## 状态和错误恢复

| bStatus | 名称 | 本工程常见原因 |
|---:|---|---|
| `0x00` | OK | 正常 |
| `0x01` | `errTARGET` | 运行时 Flash 目标检查失败 |
| `0x03` | `errWRITE` | 编程超时或写保护错误 |
| `0x04` | `errERASE` | 擦除超时或写保护错误 |
| `0x07` | `errVERIFY` | 擦除或编程后校验失败 |
| `0x08` | `errADDRESS` | 地址越界、擦除未对齐或非法 block |
| `0x09` | `errNOTDONE` | 上一个 Flash job 尚未结束 |

恢复规则：

1. 读取 GETSTATUS 保存 bStatus 和状态；
2. 若为 `dfuERROR`，发送 CLRSTATUS；
3. 再次 GETSTATUS/GETSTATE 确认 `dfuIDLE`；
4. 从 SET_ADDRESS 或标准 block 0 重新开始完整会话；
5. 若要取消正常 Download/Upload，会话处于允许状态时发送 ABORT。

USB Bus Reset 会取消当前 job、清空 DFU 错误并恢复默认地址。不要把 CLRSTATUS 当成
Flash 数据回滚机制。

## 主机实现检查清单

- 使用 512 字节或更小的 block，block 编号不超过 16 位；
- 每个 DNLOAD/命令后轮询 GETSTATUS，并遵守 `bwPollTimeout`；
- DfuSe 数据从 block 2 开始，标准数据从 block 0 开始；
- 所有物理地址限制在 APP 区，ERASE 8 KiB 对齐，执行地址 4 字节对齐；
- manifestation 前明确设置需要的执行地址；
- Upload 固定长度结束时发送 ABORT；
- 处理短包、设备断开、`dfuERROR` 和重新枚举；
- 写后通过 Upload、CRC 或 hash 验证完整镜像；
- 不把物理地址直接作为 APP linker 地址。

完整硬件结果见 [TEST_PLAN.md](TEST_PLAN.md)，Flash 和安全边界概览见
[README.md](README.md)。
