# Third-Party Components

本仓库包含构建 CH32H417 DFU Bootloader 所需的第三方源码子集。

## CherryUSB

- 来源：CherryUSB 1.6.1
- 上游项目：https://github.com/cherry-embedded/CherryUSB
- 许可证：Apache License 2.0
- 本仓库范围：`CherryUSB/common`、`CherryUSB/core`、`CherryUSB/class/dfu`
  和 `CherryUSB/port` 中 CH32H417 USBHS 相关文件
- 本项目修改：CH32H417 USBHS DCD port、DFU 状态机与 DfuSe 指定执行地址扩展

许可证全文见 `CherryUSB/LICENSE`。各源文件保留原始版权和 SPDX 标识。

## WCH CH32H417 SDK

- 版权方：Nanjing Qinheng Microelectronics Co., Ltd.（WCH）
- 本仓库范围：`Common/Core`、`Common/Debug`、`Common/Peripheral`、
  `Common/Startup` 和 `Common/Common`
- 用途：CH32H417 启动文件、RISC-V Core 支持和外设驱动

WCH 文件保留原始版权声明。使用和再分发时应同时遵守 WCH 提供这些文件时附带的
许可条款及适用法规。

## wlink

- 名称：ch32-rs/wlink 0.1.2，Windows x86 native-driver build
- 上游项目：https://github.com/ch32-rs/wlink
- 许可证：MIT OR Apache-2.0
- 本仓库范围：`tools/wlink/wlink.exe` 及其上游说明和许可证
- SHA-256：`55A20C7D4B70E6A5729A901CBC0EF3BEA4D6222D6BE824F9635AE28C674745F4`

许可证全文见 `tools/wlink/LICENSE-MIT` 和 `tools/wlink/LICENSE-APACHE`。
