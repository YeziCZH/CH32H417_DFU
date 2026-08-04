# CherryUSB Subset

此目录是 CherryUSB 1.6.1 的项目内最小子集，只保留 CH32H417 USBHS DFU
Bootloader 编译需要的文件：

- `common/`：CherryUSB 公共 Device API 头文件；
- `core/`：USB Device Core；
- `class/dfu/`：DFU 状态机和 DfuSe 扩展；
- `port/`：CH32H417 USBHS Device Controller Driver。

上游 demo、文档、Host 栈和其他 Device Class 不参与本工程构建，因此没有纳入本
仓库。port 验证范围见根目录 `CHERRYUSB_PORT_VALIDATION.md`。

CherryUSB 文件使用 Apache License 2.0，许可证全文见 [LICENSE](LICENSE)。
