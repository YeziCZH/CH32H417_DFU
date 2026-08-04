# CherryUSB CH32H417 USBHS Port 验证说明

## 结论

[CherryUSB/port/usb_dc_ch32h417_usbhs.c](CherryUSB/port/usb_dc_ch32h417_usbhs.c)
已经满足本工程 USB High-Speed DFU Bootloader 的使用要求。EP0 枚举、控制传输、
DFU Download/Upload、DfuSe 命令、总线复位、USB deinit 和 APP 跳转均已通过硬件
验证。USB deinit 现已增加 SWJ 恢复处理，代码和构建已确认；WLINK 卡死恢复的完整
硬件闭环尚未完成，不能把这部分单独标记为已验证。

这个结论不等于“CH32H417 CherryUSB 通用 DCD port 已全面认证”。Bulk、Interrupt、
Isochronous、多端点并发、Full-Speed、远程唤醒和长稳测试仍需专用固件验证。

## 适配依据

- WCH CH32H417 Reference Manual 的 USBHS、RCC、GPIO/AFIO 和中断章节；
- WCH EVT `EXAM/USBHS/DEVICE` 的 EP0 DMA、USBHS PHY/PLL 和中断处理方式；
- CherryUSB Device Controller Driver API 和 Device Core 回调约定；
- 本工程 DFU 的 512 字节 EP0 多包 Control OUT/IN 实际行为。

主要代码：

- `CherryUSB/port/usb_dc_ch32h417_usbhs.c`：CH32H417 USBHS DCD；
- `CherryUSB/port/usb_ch32h417_usbhs_reg.h`：USBHS 寄存器辅助定义；
- `User/usb_config.h`：CherryUSB 对齐、EP0 和 High-Speed 配置；
- `User/usb_desc.c`：设备、配置、Qualifier、Other-Speed 和 DFU 描述符；
- `CherryUSB/core/usbd_core.c`：Device Core；
- `CherryUSB/class/dfu/usbd_dfu.c`：DFU 状态机。

## 实现约束

| 项目 | 当前实现 |
|---|---|
| 控制器 | CH32H417 USBHS Device，busid 0 |
| 速度 | 配置为 High-Speed；代码可报告 Full-Speed，实际 FS 尚未验证 |
| 端点数量 | EP0..EP7，双向状态数组 |
| EP0 MPS | 64 字节 |
| 非 EP0 最大 MPS | 1024 字节 |
| DMA 对齐 | 非 EP0 数据缓冲必须 4 字节对齐，否则返回错误 |
| EP0 DMA | 固定指向 4 字节对齐的 `usbhs_ep0_buf[64]` |
| EP0 多包 | ISR 在固定 DMA 缓冲与 CherryUSB 请求缓冲之间按包复制 |
| 地址设置 | SET_ADDRESS 状态阶段完成后延迟写入 `DEV_AD` |
| 数据 Toggle | EP0 软件维护；非 EP0 非 Iso 使用硬件自动 Toggle |
| SWD 影响 | USB 初始化会关闭 SWJ；USB deinit 会恢复 SWJ；本板仍需人工切换 SWD/USBHS |
| Cache | CH32H417 本配置无数据 Cache，`USB_NOCACHE_RAM_SECTION` 为空 |

EP0 DMA 地址不在 SETUP、Control OUT 和 Control IN 阶段动态切换。这一点来自 WCH
官方例程的固定缓冲策略，用于避免 SETUP 与数据阶段竞争同一个 DMA 指针。

## 验证矩阵

状态含义：`已验证` 表示有当前硬件证据；`已实现` 表示代码路径存在但缺少专项硬件
覆盖；`待实现/确认` 表示完整通用能力尚不能给出承诺。

| 能力 | 代码状态 | 硬件状态 | 证据或剩余工作 |
|---|---|---|---|
| USBHS PLL、UTMI、控制器初始化 | 已实现 | 已验证 | Windows 以 High-Speed 枚举 `0483:DF11` |
| Device/Configuration/String 描述符 | 已实现 | 已验证 | VID/PID、产品字符串和 DfuSe interface string 可读 |
| Device Qualifier/Other-Speed 描述符 | 已实现 | 部分验证 | HS 主机可读取；实际 Full-Speed 链路未跑 |
| SETUP 和无数据控制请求 | 已实现 | 已验证 | 枚举、GETSTATE、GETSTATUS、ABORT、CLRSTATUS |
| 多包 Control OUT | 已实现 | 已验证 | 单个 512 字节 DFU DNLOAD 拆成八个 64 字节包 |
| 多包 Control IN | 已实现 | 已验证 | 单个 512 字节 DFU UPLOAD 和完整回读通过 |
| Control ZLP/状态阶段 | 已实现 | 已验证 | manifestation、短包结束和反复会话通过 |
| 延迟设备地址生效 | 已实现 | 已验证 | 枚举稳定完成 |
| EP0 Stall/Clear Stall | 已实现 | 部分验证 | 异常控制请求未影响后续回归；仍需独立 halt/clear-feature 测试 |
| USB Bus Reset | 已实现 | 已验证 | reset/re-enumeration 后回到 `dfuIDLE` |
| USB deinit | 已实现 | 部分验证 | T15/T16 跳转 APP 成功；新增 SWJ 恢复处理待 T23 闭环 |
| Suspend/Resume 事件转发 | 已实现 | 未专项验证 | ISR 路径存在，需要总线挂起/恢复测试 |
| Remote Wakeup | 已实现 | 未验证 | 需要支持远程唤醒的测试描述符和主机流程 |
| LPM | 控制位已启用 | 未验证 | 需要 L1 进入/退出和长稳测试 |
| Bulk IN/OUT | 基础路径已实现 | 未验证 | 需要非 EP0 loopback 固件和 libusb 压测 |
| Interrupt IN/OUT | 基础路径已实现 | 未验证 | 需要周期、短包和取消测试 |
| Isochronous IN/OUT | 寄存器路径已实现 | 未验证 | 需要帧时序、丢包和带宽测试 |
| 非 EP0 open/close/stall/clear | 已实现 | 未验证 | 需要逐端点生命周期和 Toggle 恢复测试 |
| 多端点并发 | 状态结构支持 | 未验证 | 需要 IN/OUT 并发、不同类型端点组合 |
| Full-Speed | 描述符和速度读取存在 | 未验证 | 需要 Full-Speed hub 或等效强制降速链路 |
| 长时间随机传输 | 未形成测试 | 未验证 | 需要随机长度、reset、stall、取消和错误注入 |

## 已有硬件证据

当前 DFU 回归覆盖了以下 port 行为：

- T03：High-Speed 枚举、描述符和基础控制请求；
- T06/T07：标准 DFU 与 DfuSe 多包 Control OUT；
- T07A/T10：多包 Control IN、短包和零长度 Upload 结束；
- T12：大 block 编号下的连续 EP0 数据传输；
- T13/T14：ABORT、CLRSTATUS、错误状态和后续恢复；
- T15/T16：USB deinit 后 RISC-V APP 跳转；SWJ 恢复调用已实现并通过构建；
- T17：安全 manifestation 后恢复 `dfuIDLE`；
- T18：断电后重新枚举和完整 Download/Upload；
- T19：512 KiB、1024 个 DFU block 连续传输和完整 SHA-256 回读。

详见 [TEST_PLAN.md](TEST_PLAN.md)。这些证据足以支持“当前 DFU 应用可用”，但不能
替代非控制端点专项测试。

T23 曾确认卡死测试 APP 可以下载并进入模拟死循环，随后暴露出旧版 deinit 未恢复
SWJ 的问题。当前修复在 `usb_dc_deinit()` 中恢复 SWJ，但按当前测试安排暂不继续需要
人工切换的恢复闭环，因此保留为部分验证。

## 完整通用验证方案

下次完整 port 测试应创建独立的 composite loopback 固件，而不是在 16 KB DFU
Bootloader 中继续塞入测试功能。建议至少提供：

- Bulk EP1 IN/OUT，用于大吞吐、短包、ZLP 和随机长度回环；
- Interrupt EP2 IN/OUT，用于周期调度和连续小包；
- Isochronous EP3 IN/OUT，用于帧号、丢包率和持续带宽；
- Vendor Control 请求，用于 open/close、stall/clear、remote wake 和错误注入；
- 端点统计信息，用于主机核对包数、字节数、短包、错误和 reset 次数。

Python/libusb 主机测试至少覆盖：

1. 长度 `0、1、63、64、65、511、512、513` 和端点 MPS 边界；
2. 4 字节对齐与故意未对齐缓冲的拒绝行为；
3. 连续 open/close、stall/clear-stall 后 Toggle 从 DATA0 恢复；
4. 多个 IN/OUT 端点并发和随机取消；
5. 活动传输期间 USB reset、拔插、suspend/resume；
6. Full-Speed hub 下重复全部非 Iso 测试；
7. 至少数小时随机传输，数据逐字节或 hash 校验且无死锁。

完整验证需要人工配合 SWD/USBHS 硬件切换，并准备 Full-Speed hub。只有上述测试通过
后，才能把该 port 标记为适用于 CherryUSB 的通用设备类。

## 当前发布判断

- 用于本仓库 DFU Bootloader：可发布和继续集成 APP。
- 用于其他仅 EP0 的设备类：代码基础可复用，但仍应重新做类级硬件回归。
- 用于 Bulk/Interrupt/Isochronous 产品：暂不应直接宣称 port 已完整验证。
