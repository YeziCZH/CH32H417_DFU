# CH32H417ME DFU 硬件测试计划

本文记录 CH32H417ME 960 KB Flash、16 KB Bootloader 布局的测试方法和
2026-08-03 起的硬件基线。协议细节见 [DFU_PROTOCOL.md](DFU_PROTOCOL.md)。

## 测试对象

| 项目 | 基线配置 |
|---|---|
| 芯片 | CH32H417ME V3F，960 KB Flash，DBMODE=1 |
| Bootloader | `0x08000000..0x08003FFF`，16 KB |
| APP | `0x08004000..0x080EFFFF`，944 KB |
| Release 镜像 | `build/ch32h417_dfu.bin`，15772 字节（上限 16384） |
| USB | `0483:DF11`，USB High-Speed，WinUSB |
| DfuSe 描述符 | `@Flash/0x08004000/118*8Kg` |
| 主机 | Windows、Python 3.11、PyUSB、libusb-package |
| 调试串口 | COM4，115200 8N1 |
| 硬件限制 | SWD 与 USBHS 复用，需要人工切换硬件档位 |

CH32H417ME 的 960 KB 容量由具体器件型号确定，本板无需再单独通过 SWD 读取
DBMODE bit 28。固件仍保留运行时目标检查，异常目标会返回 `errTARGET`。

## 当前结论

- T01-T18 已通过 16 KB Release 固件硬件验证。
- T03 已在 High-Speed 路径通过；Full-Speed hub 路径尚未执行。
- T15 默认地址跳转和 T16 指定地址跳转均由 COM4 APP 日志确认。
- T18 在第 296/2048 个 block 后断电，恢复供电后重新枚举、完整下载、Upload
  字节比较和清理擦除均通过。
- T19 已记录 512 KiB 全量变化、完全相同和三扇区变化的性能基线。
- CherryUSB port 对本 DFU/EP0 用途已验证；通用非 EP0 端点不在本测试计划的
  已完成范围内。
- 固定 magic 和调试串口 Enter/Space 已于 2026-08-04 通过上板验证；不活动超时策略
  已于 2026-08-05 改为显式触发 30 秒、兜底 DFU 2 秒，T22 需重新上板验证。
  UART4 RX 高电平入口仍待单独验证，完整 WLINK 卡死恢复流程按当前计划暂缓。
- USB deinit 已增加恢复 SWJ 配置的处理并通过构建；T15/T16 的 APP 跳转已验证，
  但新增 SWJ 恢复处理尚未完成 T23 硬件闭环。

## 测试分组

| 分组 | 项目 | 是否需要人工操作 |
|---|---|---|
| 日常自动回归 | T01、T03(HS)、T04-T14、T17 | 否，保持 USBHS/DFU 档位 |
| 跳转验证 | T15、T16 | 需要准备测试 APP，并在 APP 运行后恢复 DFU |
| 断电恢复 | T18 | 需要按脚本提示人工断电和恢复供电 |
| 性能基线 | T19 | 否，但会覆盖指定 APP 测试区域 |
| 启动入口 | T20-T22、T24 | T20 和 T22 需要复测；T21/T24 已有硬件结果 |
| 卡死恢复 | T23 | 已暂缓；恢复测试需要人工切换 SWD/USBHS |
| 可选链路 | T03 Full-Speed | 需要 Full-Speed hub 或等效链路 |

日常回归不要重复执行 T18。只有用户在设备旁并明确准备断电时，才运行
`dfu_power_loss_test.py interrupt`。

## 测试矩阵

| ID | 测试项目 | 方法与通过条件 | 当前结果 |
|---|---|---|---|
| T01 | 固件尺寸 | Release `.bin` 不超过 16 KB；链接脚本必须在超限时失败 | PASS（15772 B） |
| T02 | 目标容量 | 运行时目标检查接受有效 Flash 操作，末扇区 `0x080EE000..0x080EFFFF` 可擦写回读 | PASS |
| T03 | USB 枚举 | 枚举为 `0483:DF11`，High-Speed，配置和控制请求无异常 Stall；可选 Full-Speed 路径另测 | PASS (HS)，FS 未跑 |
| T04 | 擦除值 | 擦除 `0x08004000` 后 Upload，内容必须为 `39 E3 39 E3 ...`，即字 `0xE339E339` | PASS |
| T05 | 擦除边界 | 接受对齐 APP 地址；拒绝未对齐地址、Bootloader 地址和 `0x080F0000` | PASS |
| T06 | 标准 DFU | 标准 block 0 Download、manifestation、标准 Upload 后逐字节一致 | PASS |
| T07 | DfuSe 下载 | SET_ADDRESS 到 `0x08020000`，从 block 2 下载并 Upload 回读一致 | PASS |
| T07A | DfuSe Upload 转换 | SET_ADDRESS 后无需 ABORT，立即从 block 2 Upload，进入 `dfuUPLOAD-IDLE` | PASS |
| T08 | 部分扇区 RMW | 覆盖非对齐短区间，前缀和后缀保持不变 | PASS |
| T09 | 跨扇区下载 | 下载至少 9 KiB 跨越 8 KiB 边界，完整回读一致 | PASS |
| T10 | Flash 末扇区 | 末扇区可写可读；从 `0x080F0000` Upload 返回零长度结束 | PASS |
| T11 | 差分更新 | 相同镜像不改变数据并跳过物理重写；只改一个扇区时其余扇区保持一致 | PASS |
| T12 | 命令消歧 | 普通数据 block 33/65/81 不得误识别为 `0x21/0x41/0x51` 命令 | PASS |
| T13 | ABORT 恢复 | 未 manifestation 的脏 cache 经 ABORT 丢弃，随后新下载成功 | PASS |
| T14 | 错误恢复 | 非法访问进入 `dfuERROR` 并返回正确 bStatus；CLRSTATUS 后有效操作成功 | PASS |
| T15 | 默认跳转 | 物理 `0x08004000` 跳到执行 alias `0x00004000`，APP 输出 PASS 日志 | PASS |
| T16 | 指定地址跳转 | 物理 `0x08020000` 跳到执行 alias `0x00020000`，直接进入 RISC-V `_start` | PASS |
| T17 | 空白 APP 保护 | 入口首字为 `0xE339E339`、`0` 或 `0xFFFFFFFF` 时不执行，恢复/停留在 Bootloader | PASS |
| T18 | 下载中断电 | manifestation 前断电，恢复后 Bootloader 可枚举并完成干净下载和回读 | PASS |
| T19 | 下载性能 | 512 KiB 镜像测试全量变化、完全相同和三扇区变化，并在每组后完整回读校验 | PASS，见下表 |
| T20 | UART4 电平入口 | PC7 悬空/低电平时有效 APP 正常启动；PC6/PC7 短接或后级主控拉高 PC7 时进入 DFU；PC6 始终为高 | 待硬件验证 |
| T21 | Magic 入口 | APP 写 `0x201100FC=0x44465521` 后复位进入 DFU；magic 被一次性清除 | PASS |
| T22 | 不活动超时 | 默认 APP 无效兜底 DFU 保持 2 秒节奏；显式触发 DFU 后约 30 秒才退出；持续请求和长下载不退出；默认或指定入口无效时继续枚举 | 需复测 |
| T23 | 卡死恢复 | WLINK 复位、UART4 RX 高电平进入、下载 APP、manifestation 跳转完整恢复 | 暂缓，未完成硬件闭环 |
| T24 | 调试串口入口 | 启动 500 ms 窗口内向 COM4 发送 Enter 或空格进入 DFU；窗口外 APP 已运行时不由 Bootloader 监听 | PASS（Enter/Space） |

## T19 性能基线

测试条件：USB High-Speed、Release 固件、512 KiB 镜像、512 字节 block、
64 个 8 KiB 扇区。目标区域为 `0x08040000..0x080BFFFF`，安全 manifestation
入口为已擦空的 `0x080D0000`。

| 场景 | 实际变化扇区 | 耗时 | 有效速度 | 加速 | 节省时间 |
|---|---:|---:|---:|---:|---:|
| 全量变化 | 64/64 | 4.302 s | 119.0 KiB/s | 1.00x | 0% |
| 完全相同 | 0/64 | 2.843 s | 180.1 KiB/s | 1.51x | 33.9% |
| 三扇区变化 | 3/64 | 2.997 s | 170.8 KiB/s | 1.44x | 30.3% |

计时范围包括 SET_ADDRESS、完整 512 KiB DNLOAD、每个 block 的 GETSTATUS
轮询和安全 manifestation，不包括基线准备与 Upload 回读。每组之后 Upload
完整 512 KiB，并通过 SHA-256 比较。

当前差分机制是设备侧 Flash 扇区差分，不是主机侧增量包：即使只有三个扇区变化，
主机仍发送完整 512 KiB。收益来自减少 Flash 擦写和磨损，USB block 数量不变。
性能回归时不要预先显式 ERASE，否则会破坏比较基线并使扇区重新编程。

## 推荐命令

### 自动完整回归

```powershell
python scripts/dfu_full_regression_16k.py
```

覆盖 T01、T03(HS)、T04-T14 和 T17。脚本会修改 APP 测试区，使用空白安全
入口结束 manifestation，不需要切换 SWD。

### 快速布局检查

```powershell
python scripts/dfu_16k_layout_test.py
```

### 跳转测试

```powershell
powershell -ExecutionPolicy Bypass -File scripts/dfu_jump_test.ps1 `
  -Image build/jump_app_04000/jump_app.bin `
  -Address 0x08004000 -ExpectedAlias 0x00004000

powershell -ExecutionPolicy Bypass -File scripts/dfu_jump_test.ps1 `
  -Image build/jump_app_20000/jump_app.bin `
  -Address 0x08020000 -ExecAddress 0x08020000 `
  -ExpectedAlias 0x00020000
```

### Magic word 入口测试 APP

```powershell
cmake --fresh -S tests/jump_app -B build/jump_app_magic -G Ninja `
  -DAPP_ALIAS=0x00004000 -DJUMP_APP_ENTER_DFU=ON
cmake --build build/jump_app_magic -- -j8
python scripts/dfu_download.py `
  build/jump_app_magic/jump_app.bin --addr 0x08004000 --erase
```

让 PC7 保持低电平后运行此项，APP 日志应先出现 `Request DFU by magic`，随后复位并
重新枚举 `0483:DF11`。显式触发后主机有 30 秒窗口开始发送 DFU 请求；测试完再下载普通 APP。

### 断电恢复

```powershell
python scripts/dfu_power_loss_test.py interrupt
# 只在看到 POWER OFF NOW 后人工断电
python scripts/dfu_power_loss_test.py recover
```

### 单次 Download/Upload

```powershell
python scripts/dfu_download.py app.bin --standard
python scripts/dfu_download.py app.bin `
  --addr 0x08020000 --erase --exec 0x08020000
python scripts/dfu_download.py `
  --upload readback.bin --addr 0x08004000 --length 0x2000
```

## 结果记录要求

新一轮完整硬件验证应记录：

- 固件版本或源码快照、`.bin` 大小和构建选项；
- 板卡、芯片型号、USB 主机系统和 USB 速度；
- 执行命令、测试地址和镜像长度；
- PASS/FAIL、DFU bStatus、失败 block 和 COM4 日志；
- 失败后的 CLRSTATUS/ABORT/复位恢复结果。

擦除、边界和性能测试会修改 APP Flash。测试前确认目标区没有需要保留的数据，
不要把任何测试地址放入 `0x08000000..0x08003FFF`。Bootloader 烧录、SWD/USBHS
切换、Full-Speed hub 和人工断电属于独立硬件步骤，不应混入无人值守自动回归。
