# CH32H417 DFU Bootloader

面向 CH32H417ME V3F（RISC-V）的 USB 2.0 High-Speed DFU Bootloader，基于 CherryUSB。
工程同时支持标准 USB DFU、DfuSe 地址扩展、指定执行地址、Flash Upload，以及
CH32H417 的物理编程地址与零基执行 alias 跳转。

## 当前状态

| 项目 | 当前状态 |
|---|---|
| 目标芯片 | CH32H417ME，960 KB 内置 Flash，DBMODE=1 |
| Bootloader 保留空间 | 16 KB |
| 项目许可证 | Apache License 2.0（第三方组件除外） |
| 当前 Release 二进制 | 15772 字节，严格限制在 16 KB 内 |
| USB | `0483:DF11`，USB High-Speed，WinUSB |
| DFU 传输块 | 512 字节 |
| Flash 擦除粒度 | 8 KB |
| Flash 编程粒度 | 256 字节快速编程页 |
| 硬件验证 | T01-T19、T21、T22、T24 已通过；T20 待测；T23 已验证 PC8 物理复位子步骤 |
| CherryUSB port | 当前 DFU/EP0 用途已验证；通用非控制端点验证待补充 |
| 512 KiB 下载性能 | 全量变化 119.0 KiB/s；完全相同差分 180.1 KiB/s |

## 文档导航

- [README.md](README.md)：工程概览、构建、烧录、调用和故障排查。
- [DFU_PROTOCOL.md](DFU_PROTOCOL.md)：标准 DFU/DfuSe 请求、地址映射、状态轮询和跳转契约。
- [TEST_PLAN.md](TEST_PLAN.md)：T01-T24 测试方法、硬件结果和性能基线。
- [CHERRYUSB_PORT_VALIDATION.md](CHERRYUSB_PORT_VALIDATION.md)：CH32H417 USBHS port 的已验证范围和待补项目。
- [LICENSE](LICENSE)、[NOTICE](NOTICE) 和 [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md)：
  项目许可证、版权声明和第三方组件授权信息。

## 最短调用流程

1. 将硬件开关切到 SWD。
2. 在 VS Code 运行默认的 `Configure + Build + Flash` task。
3. 烧录完成后将硬件开关切到 USBHS/DFU。
4. 确认 Windows 枚举 `0483:DF11`。
5. 执行下载：

```powershell
python scripts/dfu_download.py app.bin `
  --addr 0x08020000 --erase --exec 0x08020000
```

此例中的 APP 必须链接到执行 alias `0x00020000`。默认 APP 则写入
`0x08004000` 并链接到 `0x00004000`。

## 已实现功能

### 1. CherryUSB CH32H417 USBHS Device Port

- 完成 CH32H417 USBHS Device Controller Driver 适配。
- EP0 使用固定、4 字节对齐的 64 字节 DMA 缓冲区。
- 支持 SETUP、Control IN、Control OUT、状态阶段和零长度包。
- 支持多包 EP0 传输；DFU 单块 512 字节会拆分为多个 64 字节 USB 包。
- 支持设备地址延迟生效、Endpoint Stall/Clear Stall、USB Bus Reset。
- 支持 Suspend/Resume 事件转发和 USB deinit；跳转 APP 前恢复 SWJ 配置。
- 已验证 USB High-Speed 枚举、DFU 控制请求、总线复位和重新枚举。

通用 Bulk、Interrupt、Isochronous 和多端点并发尚未完成完整硬件验证，详见
[CHERRYUSB_PORT_VALIDATION.md](CHERRYUSB_PORT_VALIDATION.md)。

### 2. 标准 USB DFU 1.1 状态机

支持以下 DFU Class Request：

- `DNLOAD`
- `UPLOAD`
- `GETSTATUS`
- `GETSTATE`
- `CLRSTATUS`
- `ABORT`

实现 `dfuIDLE`、`dfuDNLOAD-SYNC`、`dfuDNBUSY`、`dfuDNLOAD-IDLE`、
`dfuMANIFEST`、`dfuMANIFEST-WAIT-RESET`、`dfuUPLOAD-IDLE` 和 `dfuERROR`
等状态转换。Flash 操作结果通过标准 DFU `bStatus` 返回。

### 3. 标准 DFU Download/Upload

标准模式不需要地址扩展命令，block 直接映射到默认 APP 区：

```text
physical_address = 0x08004000 + block_number * 512
```

- 标准 Download 从 block 0 开始。
- 标准 Upload 从 block 0 开始。
- 访问超过 `0x080EFFFF` 时返回地址错误或短包结束。
- 支持 manifestation、错误恢复和后续重新进入 DFU。

### 4. DfuSe 地址扩展

block 0、长度严格为 5 字节时，可发送以下命令：

| 命令 | 编码 | Payload | 功能 |
|---|---:|---|---|
| SET_ADDRESS_POINTER | `0x21` | `21 + address_le32` | 设置后续 DfuSe Download/Upload 基址 |
| ERASE | `0x41` | `41 + address_le32` | 擦除指定 8 KB Flash 扇区 |
| SET_EXEC_ADDRESS | `0x51` | `51 + address_le32` | 设置 manifestation 后的物理执行地址 |

DfuSe 数据 block 的映射规则为：

```text
physical_address = address_pointer + (block_number - 2) * 512
block_number >= 2
```

约束：

- SET_ADDRESS 地址必须位于 APP 区。
- ERASE 地址必须位于 APP 区并按 8 KB 对齐。
- SET_EXEC_ADDRESS 必须位于 APP 区并按 4 字节对齐。
- ERASE 不改变 address pointer，避免污染后续标准 DFU block 0。
- Download 地址和 Execute 地址相互独立。
- 只有 block 0 且长度正好为 5 字节才会识别命令；普通数据 block 33、65、81
  不会被误识别为 `0x21`、`0x41`、`0x51` 命令。

### 5. DFU Upload

- 支持标准 DFU Upload。
- 支持 SET_ADDRESS 后立即从 DfuSe block 2 Upload。
- 支持任意 APP 区地址和非整扇区长度读取。
- 到达 APP 末地址 `0x080F0000` 时返回零长度或短包，正确终止 Upload。
- Upload 完成或 ABORT 后会复位地址状态。

### 6. CH32H417ME Flash 下载引擎

Flash 引擎针对 CH32H417ME 960 KB、DBMODE 双 Flash 模式实现：

- APP 可访问范围：`0x08004000..0x080EFFFF`。
- Bootloader 范围：`0x08000000..0x08003FFF`，DFU 永远不能擦写。
- 运行时检查 `FLASH_CFGR0.DBMODE`；目标不匹配时返回 `errTARGET`。
- 使用 8 KB RAM sector cache 支持非对齐、部分扇区更新。
- 以 256 字节快速编程页写入 Flash。
- 每次擦除后验证整个扇区。
- 每次编程后逐字验证整个扇区。
- Flash Busy/Write Busy 等待均有超时，避免永久卡死。
- Flash 操作结束后清状态并重新锁定普通和 Fast Program 控制器。
- 操作前关闭不适用于当前写入流程的 Enhance Mode。

CH32H417 擦除后的字不是 `0xFFFFFFFF`，而是：

```text
word:  0xE339E339
bytes: 39 E3 39 E3 ...
```

因此空白检查、擦除验证和 Upload 测试都使用 `0xE339E339`。

### 7. 差分更新和 Read-Modify-Write

每个 Download block 先写入 8 KB RAM cache：

1. 首次访问某扇区时读取完整 8 KB Flash 内容。
2. 将收到的数据覆盖到 cache 中相应位置。
3. 切换到下一扇区或收到零长度 DNLOAD manifestation 时刷新当前扇区。
4. 刷新前逐字比较 Flash 和 cache。
5. 内容完全一致时跳过物理擦除和写入。
6. 内容变化时执行整扇区擦除、编程和校验。

由此实现：

- 非 8 KB 对齐下载。
- 短数据 patch。
- 跨扇区连续下载。
- 保留未覆盖区域。
- 相同镜像二次下载时跳过不必要的 Flash 重写。

`ABORT` 会丢弃尚未刷入 Flash 的脏 cache，不会隐式完成未结束的下载。

### 8. RISC-V APP 跳转

CH32H417 不是 Cortex-M，APP 首地址不能解释为 `[MSP, Reset_Handler]`。

本工程采用 WCH EVT IAP 相同的方式：

1. 主机设置物理执行地址，例如 `0x08020000`。
2. Bootloader 转换为零基执行 alias：`0x00020000`。
3. 停止 USB、SysTick、GPIO、USART 和 RCC 相关状态。
4. 通过 Software IRQ handler 执行 RISC-V `jr` 跳到 alias 地址。
5. APP 从其 `_start` 指令开始运行。

默认物理执行地址是 `0x08004000`，对应 alias `0x00004000`。

如果所选 APP 首字为 `0xE339E339`、`0x00000000` 或 `0xFFFFFFFF`，manifestation 后不会跳入
空白 Flash，而是恢复到可继续操作的 `dfuIDLE`。

### 9. 串口和诊断

- 调试串口：USART1，PA9/PA10，115200 8N1。
- 当前测试板通过 WCH-Link 虚拟串口 `COM4` 读取日志。
- 串口发送具有超时保护，串口异常不会阻塞 DFU 主循环。
- `.noinit_dfu` 中保留 `dbg_dfu[6]`，记录最后一次 Flash 事件、地址、参数、
  job 类型和错误状态，便于 SWD 调试。
- 可通过构建选项启用详细 Flash UART 标签日志。

## 目标内存布局

| 区域 | 物理/编程地址 | V3F 执行 alias | 大小 | DFU 权限 |
|---|---:|---:|---:|---|
| Bootloader | `0x08000000..0x08003FFF` | `0x00000000..0x00003FFF` | 16 KB | 禁止 |
| Application | `0x08004000..0x080EFFFF` | `0x00004000..0x000EFFFF` | 944 KB | 读/写/擦除 |
| Flash 末地址 | `0x080F0000` | `0x000F0000` | end-exclusive | 只作为边界 |

DfuSe interface string：

```text
@Flash/0x08004000/118*8Kg
```

其中 118 个 8 KB 扇区合计 944 KB。Bootloader 的 16 KB 边界与
8 KB 擦除粒度对齐。

这是一次 APP ABI/布局变更。原来链接到 `0x00008000` 的 APP 不会再被
默认启动；默认 APP 需要重新链接到 `0x00004000`，并写入物理地址
`0x08004000`。指定地址跳转仍可在当次 DFU manifestation 中启动
`0x08008000` 的旧 APP，但该地址不会持久化为复位后的默认启动地址。

## USB 描述符

| 字段 | 值 |
|---|---|
| VID:PID | `0483:DF11` |
| Manufacturer | `WCH DFU` |
| Product | `CH32H417 DFU HS` |
| Serial | `00000001` |
| USB version | USB 2.0 |
| Interface | DFU mode，Class `0xFE`，Subclass `0x01`，Protocol `0x02` |
| Transfer size | 512 bytes |
| DFU version | `0x011A` |
| bmAttributes | `0x0B`: Download、Upload、Will Detach |
| Manifestation tolerant | 否 |
| Power | Bus-powered，200 mA descriptor |

## Boot 流程

1. V3F 启动并初始化系统时钟、延时和 USART1。
2. 固件打开 500 ms 启动窗口，便于调试器连接，也可通过调试串口请求 DFU。
3. 检查固定 SRAM magic：地址 `0x201100FC`，值 `0x44465521`；命中后先清零。
4. 检查 UART4 板级引脚：PC6/TX 持续输出高电平，PC7/RX 为下拉输入。PC6 与
   PC7 短接，或由后级主控把 PC7 拉高，都会触发 DFU；PC7 悬空不会误触发。
5. 启动窗口内调试串口 USART1/COM4 收到 Enter（`0x0D` 或 `0x0A`）或空格
   （`0x20`）也会触发 DFU；APP 已经运行后需由 APP 写 magic/reset。
6. 任一入口命中时初始化 CherryUSB USBHS Device；默认 APP 无效时也直接进入 DFU。
7. DFU class 请求每次都会刷新不活动计时器，正常 Download、Upload 和 GETSTATUS
   轮询不会在传输中途退出。
8. 未显式触发、但默认 APP 无效而进入 DFU 时使用 2 秒超时；UART4 RX、调试串口或
   SRAM magic 显式触发 DFU 时使用 30 秒超时，便于人工启动主机下载。
9. 超时没有 DFU 请求时取消未完成会话并尝试默认 APP；默认 APP 无效则继续 DFU。
10. 成功 manifestation 后跳到默认或通过 `SET_EXEC_ADDRESS` 指定的 APP；指定入口
   无效时恢复 `dfuIDLE` 并禁止不活动超时跳回其他 APP，直到有效 manifestation 或复位。
   Flash 错误状态也不会触发超时启动，需由主机先执行 CLRSTATUS 恢复。

固定 magic 是 APP 到 Bootloader 的稳定 ABI，定义在 [User/dfu_boot.h](User/dfu_boot.h)。
APP 可写入 magic、执行内存屏障并触发系统复位：

```c
*(volatile uint32_t *)DFU_BOOT_MAGIC_ADDRESS = DFU_BOOT_MAGIC_VALUE;
__asm volatile("fence rw, rw" ::: "memory");
NVIC_SystemReset();
```

Bootloader 内的 `dfu_request_reboot()` 实现了同一流程。该 4 字节区域由链接脚本
单独保留，不会被 Bootloader 或使用本仓库链接脚本的 APP 的 `.data/.bss` 覆盖。

## APP 镜像要求

### 物理地址与链接地址

APP 写入地址使用 `0x08000000` 区域，链接执行地址必须使用零基 alias。

| 写入物理地址 | Linker FLASH ORIGIN / `_start` |
|---:|---:|
| `0x08004000` | `0x00004000` |
| `0x08020000` | `0x00020000` |
| `0x08080000` | `0x00080000` |

换算公式：

```text
execution_alias = physical_address - 0x08000000
```

APP 二进制首地址必须直接包含可执行的 RISC-V `_start` 指令。不要生成或期待
Cortex-M 风格的 MSP/Reset_Handler 向量表。

工程提供 [tests/jump_app](tests/jump_app) 作为 `0x00004000` 和
`0x00020000` 链接/跳转参考。

## 构建环境

### 依赖

- Windows PowerShell。
- CMake 3.16 或更高版本。
- Ninja。
- MounRiver Studio 2 的 WCH RISC-V GCC12 工具链。
- 仓库内 `Common/` 已包含本工程使用的 CH32H417 WCH SDK。

当前 [CMakeLists.txt](CMakeLists.txt) 中工具链路径为：

```text
D:/APP/MRS/MounRiver_Studio2/resources/app/resources/win32/components/WCH/Toolchain/RISC-V Embedded GCC12
```

不同机器可通过 CMake 参数或环境变量覆盖工具链路径，无需修改源码：

```powershell
cmake --fresh -S . -B build -G Ninja `
  -DTOOLCHAIN_FOLDER="D:/path/to/RISC-V Embedded GCC12"

$env:WCH_TOOLCHAIN_ROOT = "D:/path/to/RISC-V Embedded GCC12"
cmake --fresh -S . -B build -G Ninja
```

### Release 构建

```powershell
cmake --fresh -S . -B build -G Ninja `
  -DDFU_DEBUG_STAY_IN_DFU=OFF `
  -DDFU_FLASH_DEBUG_LOG=OFF
cmake --build build -- -j8
```

输出文件：

- `build/ch32h417_dfu.bin`：烧录用原始二进制。
- `build/ch32h417_dfu.hex`：Intel HEX。
- `build/ch32h417_dfu.elf`：调试符号和 ELF。
- `build/ch32h417_dfu.lst`：反汇编列表。
- `build/ch32h417_dfu.map`：链接映射。

当前 Release `.bin` 为 15772 字节，满足 16 KB 限制。链接脚本已将
`FLASH LENGTH` 设为 16 KB，固件超限时链接会直接失败；当前剩余 612 字节。

### 调试构建

```powershell
cmake --fresh -S . -B build -G Ninja `
  -DDFU_DEBUG_STAY_IN_DFU=ON `
  -DDFU_FLASH_DEBUG_LOG=ON
cmake --build build -- -j8
```

| 选项 | 作用 |
|---|---|
| `DFU_DEBUG_STAY_IN_DFU=ON` | manifestation 后强制回到 dfuIDLE，便于连续测试 |
| `DFU_FLASH_DEBUG_LOG=ON` | COM4 输出 Flash 事件标签 |

调试构建也使用同一 16 KB 链接边界。详细运行状态另外保留在
`.noinit_dfu` 的 `dbg_dfu[6]` 中，可通过 SWD 读取。当前同时开启两个调试
选项时 `.bin` 为 16316 字节，也通过同一 16 KB 链接边界，剩余 68 字节。

UART Flash 短标签：`P*` 表示 prepare，`E*` 表示 erase，`F*` 表示
cache flush，`QW/QE` 表示写/擦除入队，`W*/E*` 中的 `T/A/B` 分别表示
target/address/busy 错误，`PE/PX/PD` 表示 erase poll 开始/失败/完成。

## VS Code Task 和烧录

[.vscode/tasks.json](.vscode/tasks.json) 提供：

- CMake Configure
- CMake Build
- CMake Clean
- WLINK Probe List
- Erase + Flash (wlink)
- Configure + Build + Flash

默认 Configure 使用 `--fresh` 和 Release 选项，避免 NMake/Ninja cache 冲突。

### 硬件开关流程

SWD 与 USBHS 共用硬件通路，必须人工切换：

1. 切换到 SWD 档位。
2. 运行 VS Code 默认 Build Task，或执行下方烧录脚本。
3. 等待烧录完成。
4. 切换到 USBHS/DFU 档位。
5. 再运行 DFU 主机测试或下载命令。

```powershell
powershell -ExecutionPolicy Bypass -File scripts/flash.ps1
```

[scripts/flash.ps1](scripts/flash.ps1) 使用经过硬件验证的流程：

1. `wlink erase --method pin-rst`，通过 NRST 连接并擦除。
2. `wlink flash --address 0x08000000`，写入 Bootloader。

仓库已在 `tools/wlink/wlink.exe` 集成 Windows x86 版 wlink 0.1.2，克隆仓库后不再
依赖 `F:` 盘固定路径或全局 PATH。`flash.ps1` 会优先使用该副本；也可以通过
`-Wlink D:/path/to/wlink.exe` 显式覆盖，仓库相对路径同样有效。固件参数也会相对
仓库根目录解析，因此从任意当前工作目录调用脚本均可使用默认配置。

通用 WLINK 命令可通过包装脚本执行：

```powershell
powershell -ExecutionPolicy Bypass -File scripts/wlink.ps1 --version
powershell -ExecutionPolicy Bypass -File scripts/wlink.ps1 list
powershell -ExecutionPolicy Bypass -File scripts/wlink.ps1 `
  -d 0 --chip CH32H41X status
```

无需连接 SWD、只通过物理 nRST 复位目标板：

```powershell
powershell -ExecutionPolicy Bypass -File scripts/wlink.ps1 `
  reset pin-rst --hold-ms 500
```

该扩展直接控制 WCH-LinkE 主控的 PC8 对外复位引脚；必须将 PC8 对应的 RST 输出
连接到目标 CH32H417 的 nRST。命令按拉低、拉高、浮空的顺序执行，不会先连接目标
SWD，也不要求目标芯片当前可被 SWD attach。WCH-LinkE v2.22 上板验证时，COM4
随即重新输出 Bootloader 和 APP 启动日志。

PC8 外接复位的适用边界：

- 适合 APP 卡死、SWD 被 USBHS 复用占用、`wlink reset halt` 报 `0x55` 时先把
  目标拉回 Bootloader。
- 只负责产生目标 nRST 脉冲，不会自动完成 DFU 入口触发、USBHS/SWD 硬件档位切换
  或 APP 下载。
- 如果需要让复位后稳定停在 DFU，应配合 UART4 RX 高电平入口、magic word 入口、
  或保持 APP 无效入口；否则有效 APP 会在启动延时后正常跳转。
- 调试该功能时不要用 `status`、`dmstatus havereset` 作为唯一依据，应以 COM4 新
  Bootloader 日志或示波器/逻辑分析仪观测 nRST 为准。

工具来源、版本、SHA-256 和许可证见 [tools/wlink/README.md](tools/wlink/README.md)
及 [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md)。

注意：该 `erase` 步骤应按整片擦除处理，原有 APP 和 APP 区数据会丢失。
只需要更新 APP 时应使用 USB DFU，不要运行 Bootloader 烧录脚本。

USBHS 初始化期间会关闭 SWJ，因此设备仍运行在 DFU 且硬件处于 USBHS 档位时，
独立执行 `wlink reset halt` 可能返回 WCH-Link underlying protocol error `0x55`。
已连接 PC8/RST 时可改用 `reset pin-rst`，该命令不依赖 SWD attach。
当前 port 会在 USB deinit、跳转 APP 前恢复 SWJ 配置；该恢复逻辑已通过构建，完整
WLINK 卡死恢复硬件闭环仍列为 T23 待测。

### APP 卡死恢复设计（T23 待完整硬件验证）

恢复路径为 `WLINK RESET -> DFU 进入 -> 下载 APP -> manifestation 跳转`：

1. 短接 PC6 (UART4 TX) 与 PC7 (UART4 RX) 并保持短接；也可以由后级主控持续
   拉高 PC7。
2. 通过 WCH-LinkE PC8/RST 执行 `scripts/wlink.ps1 reset pin-rst --hold-ms 500`
   复位设备；该复位子步骤已由 COM4 启动日志验证。若 Bootloader 本身损坏，切到
   SWD 档位后执行 `scripts/flash.ps1` 重新烧录 Bootloader。
3. 切到 USBHS/DFU 档位后运行下载命令。显式触发 DFU 后有 30 秒不活动窗口；下载
   开始后计时按 DFU 请求刷新，不会因镜像传输时间超过 30 秒而退出。
4. 下载 APP 并发送零长度 DNLOAD；入口有效时 Bootloader 自动跳转。
5. 移除 PC6/PC7 短接或释放后级主控对 PC7 的高电平，否则下一次复位仍会进入 DFU。

SWD 与 USBHS 复用，重新烧录 Bootloader 和 USB DFU 下载之间仍需手动切换硬件。
若切换超过 30 秒且旧 APP 有效，
设备会先启动旧 APP，需要重新执行 WLINK 复位；旧 APP 无效时设备会一直留在 DFU。
当前已验证卡死测试 APP 可以下载并运行，也已验证 PC8 pin-reset 可真实复位目标；
尚未把 UART4 触发 DFU、重新下载和跳转串成一次完整硬件闭环，因此此流程暂不作为
完整已验证发布能力。

## Windows 主机环境

设备应绑定 WinUSB 驱动。推荐使用 Python 3.11、PyUSB 和 `libusb-package`：

```powershell
python -m pip install pyusb libusb-package
```

快速检查设备：

```powershell
python -c "import sys;sys.path.insert(0,'scripts');import dfu_download as d;dev=d.find_device();print(hex(dev.idVendor),hex(dev.idProduct),dev.speed,d.get_status(dev))"
```

预期 VID/PID 为 `0x483 0xdf11`，High-Speed 的 PyUSB speed 值为 3，
空闲状态为 `(0, 0, 2)`。

## 主机调用方法

主工具为 [scripts/dfu_download.py](scripts/dfu_download.py)。
协议级调用约束见 [DFU_PROTOCOL.md](DFU_PROTOCOL.md)。

### 标准 DFU 下载

```powershell
python scripts/dfu_download.py app.bin --standard
```

标准模式始终从物理地址 `0x08004000` 开始。

### DfuSe 默认地址下载

```powershell
python scripts/dfu_download.py app.bin --addr 0x08004000 --erase
```

### 指定地址下载并跳转

```powershell
python scripts/dfu_download.py app.bin `
  --addr 0x08020000 --erase --exec 0x08020000
```

APP 必须链接到 alias `0x00020000`。

`--erase` 会把镜像覆盖范围向下/向上取整到 8 KB 扇区并先显式擦除。
不使用 `--erase` 时，Flash 引擎仍可通过 RMW 更新并保留扇区其余内容。
需要利用差分跳过时不要使用 `--erase`；显式擦除会先破坏原内容，使后续下载必然产生
Flash 编程操作。

### Upload

```powershell
python scripts/dfu_download.py `
  --upload readback.bin --addr 0x08004000 --length 0x2000
```

标准 Upload：

```powershell
python scripts/dfu_download.py `
  --upload readback.bin --standard --length 0x2000
```

### dfu-util

标准 DFU/DfuSe 工具可以用于常规 Download/Upload，但自定义
`SET_EXEC_ADDRESS 0x51` 建议通过 `dfu_download.py --exec` 调用。
本仓库不附带 `dfu-util.exe`，当前硬件回归以 Python/PyUSB 工具为准。

## Manifestation 和跳转行为

Release 构建不是 manifestation tolerant：

1. 主机发送零长度 DNLOAD。
2. Flash 引擎刷新最后一个脏扇区。
3. 状态进入 `dfuMANIFEST-WAIT-RESET`。
4. 等待约 50 ms。
5. 若入口有效则 USB deinit 并跳转 APP。
6. 若入口无效则恢复 `dfuIDLE`，不会执行空白或非法 Flash。

调试构建启用 `DFU_DEBUG_STAY_IN_DFU` 后，即使 APP 有效也会回到
`dfuIDLE`，便于在一个 USB session 内连续运行测试。

## DFU 错误和恢复

| 内部错误 | DFU bStatus | 常见原因 |
|---|---|---|
| TARGET | `errTARGET (0x01)` | DBMODE 不符合 CH32H417ME 目标配置 |
| WRITE | `errWRITE (0x03)` | 编程超时或写保护错误 |
| ERASE | `errERASE (0x04)` | 擦除超时或写保护错误 |
| VERIFY | `errVERIFY (0x07)` | 擦除/编程后数据校验失败 |
| ADDRESS | `errADDRESS (0x08)` | 越界、错误块号或 ERASE 未对齐 |
| BUSY | `errNOTDONE (0x09)` | 上一个 Flash job 尚未完成 |

设备进入 `dfuERROR` 后，主机应发送 `CLRSTATUS`。`ABORT` 用于取消当前
Download/Upload session，并丢弃尚未刷写的 cache。

## 测试

### 16 KB 完整回归

Release 固件优先运行：

```powershell
python scripts/dfu_full_regression_16k.py
```

覆盖 T01、T03(High-Speed)、T04-T14 和 T17。脚本会将 manifestation 的
执行地址指向已擦空的安全扇区，因此不会跳转到随机测试数据。
当前 16 KB 固件已在硬件上全部通过这一回归。

### 快速布局测试

16 KB Release 布局的快速安全检查：

```powershell
python scripts/dfu_16k_layout_test.py
```

该脚本验证新描述符、`0x08004000` 可写、
`0x08002000` 仍受保护、标准 Download/Upload 与 DfuSe Upload，并在
结束前擦除测试扇区。

### 历史专项脚本

```powershell
python scripts/dfu_extended_test.py
python scripts/dfu_smoke_test.py
```

这两个脚本保留用于单项定位。日常回归以
`dfu_full_regression_16k.py` 为准，避免重复运行同一批 Flash 擦写。

这些脚本会修改 APP Flash 测试区域，但不会写 Bootloader 区。

### 跳转测试 APP

默认地址：

```powershell
cmake --fresh -S tests/jump_app -B build/jump_app_04000 -G Ninja `
  -DAPP_ALIAS=0x00004000
cmake --build build/jump_app_04000
python scripts/dfu_download.py `
  build/jump_app_04000/jump_app.bin --addr 0x08004000 --erase
```

指定地址：

```powershell
cmake --fresh -S tests/jump_app -B build/jump_app_20000 -G Ninja `
  -DAPP_ALIAS=0x00020000
cmake --build build/jump_app_20000
python scripts/dfu_download.py `
  build/jump_app_20000/jump_app.bin `
  --addr 0x08020000 --erase --exec 0x08020000
```

APP 成功启动会在 COM4 输出：

```text
[JUMP-APP] PASS alias=0x00004000
[JUMP-APP] PASS alias=0x00020000
```

可用 [scripts/dfu_jump_test.ps1](scripts/dfu_jump_test.ps1) 在下载时同步捕获
COM4，并自动检查 Bootloader 物理地址和 APP alias 日志。

### 断电恢复 T18

```powershell
python scripts/dfu_power_loss_test.py interrupt
# 看到 POWER OFF NOW/block 计数后手动断电，再恢复供电并保持 DFU 档位
python scripts/dfu_power_loss_test.py recover
```

断电阶段只擦写 APP 区并故意放慢传输；`recover` 验证重新枚举、
干净下载和 Upload 回读。当前硬件测试在第 296/2048 个 block 后断电，
重新上电后恢复测试全部通过。

详细测试项目和结果见 [TEST_PLAN.md](TEST_PLAN.md)。

### 性能基线 T19

2026-08-03 在 USB High-Speed、Release 固件、512 KiB 镜像下测得：

| 场景 | 实际变化的 8 KiB 扇区 | 耗时 | 有效速度 | 相对全量变化 |
|---|---:|---:|---:|---:|
| 全量变化（非差分基线） | 64/64 | 4.302 s | 119.0 KiB/s | 1.00x |
| 镜像完全相同 | 0/64 | 2.843 s | 180.1 KiB/s | 1.51x |
| 仅三个扇区变化 | 3/64 | 2.997 s | 170.8 KiB/s | 1.44x |

测试范围为 `0x08040000..0x080BFFFF`，每组下载后均 Upload 全部 512 KiB
并通过 SHA-256 比较。计时只包含 SET_ADDRESS、1024 个 512 字节 DNLOAD block、
GETSTATUS 轮询和安全 manifestation，不包含准备与回读时间。

这里的“差分”发生在设备 Flash 层：主机仍发送完整镜像，固件按 8 KiB 扇区比较并跳过
相同扇区。因此它减少 Flash 擦写时间和磨损，但不会减少 USB 传输字节数。

## 工程结构

```text
CH32H417_DFU/
|-- LICENSE                            Apache License 2.0 全文
|-- NOTICE                             项目版权和第三方归属声明
|-- CMakeLists.txt                     主构建配置和 Release/Debug 选项
|-- Ld/Link.ld                        Bootloader V3F 链接脚本，限制 16 KB
|-- DFU_PROTOCOL.md                   主机协议、地址映射和状态机调用契约
|-- User/
|   |-- main.c                        Boot 入口、DFU 模式和 RISC-V 跳转
|   |-- dfu_usb.c                     DFU media binding、寻址和 manifestation
|   |-- dfu_flash.c/.h                Flash cache、差分写入、擦除和 Upload
|   |-- ch32h417_it.c                 Software IRQ 跳转 handler
|   |-- usb_desc.c/.h                 USB/DFU/DfuSe 描述符
|   |-- usb_config.h                  CherryUSB 工程配置
|   `-- serial.c/.h                   USART1 日志和延时
|-- Common/                           CH32H417 WCH SDK（Core/Peripheral/Startup）
|-- CherryUSB/
|   |-- core/usbd_core.c              CherryUSB Device Core
|   |-- class/dfu/usbd_dfu.c          DFU 状态机和扩展命令
|   `-- port/usb_dc_ch32h417_usbhs.c  CH32H417 USBHS DCD port
|-- scripts/
|   |-- flash.ps1                     pin-rst 擦除和 Bootloader 烧录
|   |-- wlink.ps1                     仓库内 WLINK 通用命令包装器
|   |-- dfu_download.py               Download/Upload 主机工具
|   |-- dfu_test_support.py           回归测试共享 DFU 会话和数据工具
|   |-- dfu_full_regression_16k.py    16 KB 布局主自动回归
|   |-- dfu_16k_layout_test.py        16 KB 布局快速检查
|   |-- dfu_jump_test.ps1             跳转下载和 COM4 日志校验
|   |-- dfu_power_loss_test.py        T18 断电与恢复两阶段测试
|   |-- dfu_smoke_test.py             历史差分、恢复和边界专项测试
|   `-- dfu_extended_test.py          历史标准 DFU/DfuSe/RMW 专项测试
|-- tools/wlink/                      wlink 0.1.2 Windows x86 和许可证
|-- tests/jump_app/                   RISC-V 指定 alias 跳转测试 APP
|-- TEST_PLAN.md                      硬件测试计划与结果
|-- CHERRYUSB_PORT_VALIDATION.md      CherryUSB port 验证范围
`-- THIRD_PARTY_NOTICES.md            第三方组件来源和许可证索引
```

## 已知限制和安全边界

- DFU 只能修改 APP 区，不能在线升级 Bootloader 本身。
- 当前没有镜像签名、加密、版本防回滚或加密认证。
- 当前没有整镜像 CRC/Hash 元数据；可靠性来自逐扇区写后校验和主机回读测试。
- 更新不是整镜像原子事务。跨扇区下载时，已经切换过去的扇区可能已写入；
  manifestation 前断电可能留下新旧混合镜像或部分编程扇区。
- Bootloader 不负责判定完整 APP 版本是否有效，只拒绝越界、未对齐，以及首字为
  `0xE339E339`、`0x00000000` 或 `0xFFFFFFFF` 的入口。
- 当前通用 CherryUSB port 尚未完成非 EP0 Bulk/Interrupt/Isochronous 验证。
- USB Full-Speed 强制降速路径尚未做硬件验证。
- UART4 RX 高电平入口（T20）和完整 WLINK 卡死恢复流程（T23）尚未完成硬件验证。
- DFU 使用协议不活动超时；显式触发为 30 秒，默认 APP 无效兜底为 2 秒。
  未完成会话会先取消，APP 无效时不会退出 DFU。

生产使用前建议在 APP 镜像中增加独立的完整性元数据、版本策略和启动确认机制。

## 许可证

本项目原创代码采用 [Apache License 2.0](LICENSE)，版权所有：
`Copyright 2026 YeziCZH`。

根目录许可证不重新授权仓库内的第三方组件。`CherryUSB/`、`Common/` 和
`tools/wlink/` 分别遵循其原始许可证或授权条款，完整说明见 [NOTICE](NOTICE) 和
[THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md)。分发源码或二进制时应一并保留这些
许可证、版权声明和第三方通知。

## 常见问题

### VS Code 报 Ninja 与 NMake generator 不匹配

运行 `CMake Configure` task。该 task 使用 `cmake --fresh`，会重新生成 Ninja build。

### wlink 返回 protocol error 0x55

- 确认硬件开关已切到 SWD。
- 如果只需复位且 WCH-LinkE PC8/RST 已连接，使用
  `scripts/wlink.ps1 reset pin-rst --hold-ms 500`，无需 SWD attach。
- 使用 `scripts/flash.ps1` 的 pin-rst erase 流程。
- 不要先执行独立的 `wlink reset halt`。

### 找不到 0483:DF11

- 确认 Bootloader 已烧录。
- 确认硬件开关已切到 USBHS/DFU。
- 检查 Windows 是否绑定 WinUSB。
- 检查 PC6/PC7 是否短接、后级主控是否把 PC7 拉高、启动窗口内是否向 COM4 发送
  Enter/Space，或 APP 是否在复位前写入固定 magic。
- 使用 COM4 115200 查看 Bootloader 日志。

### DFU 返回 errADDRESS

- 检查地址是否在 `0x08004000..0x080EFFFF`。
- ERASE 地址必须 8 KB 对齐。
- SET_EXEC_ADDRESS 必须 4 字节对齐。
- 标准 DFU 只能从默认 APP 基址开始。

### manifestation 后没有跳转

- 确认构建选项 `DFU_DEBUG_STAY_IN_DFU=OFF`。
- 检查 APP 首字不是 `0xE339E339`、`0x00000000` 或 `0xFFFFFFFF`。
- 检查 APP Linker ORIGIN 是否等于物理地址减 `0x08000000`。
- 通过 COM4 查看 `Leaving DFU mode` 和 `Jumping to APP` 日志。

## 相关文档

- [TEST_PLAN.md](TEST_PLAN.md)：DFU 硬件测试项目和当前结果。
- [CHERRYUSB_PORT_VALIDATION.md](CHERRYUSB_PORT_VALIDATION.md)：port 当前验证范围和下次工作。
- [DFU_PROTOCOL.md](DFU_PROTOCOL.md)：主机实现标准 DFU/DfuSe 调用时的协议契约。
- WCH CH32H417 Reference Manual：Flash、USBHS、RCC 和 V3F 地址 alias。
- WCH EVT IAP：Software IRQ + `jr` APP 跳转参考。
