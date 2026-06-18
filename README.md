# DiskLens 磁盘洞察

> 面向 Windows 的磁盘空间分析、全系统快速搜索与安全清理工具。本地运行、便携发布、清晰的 C++ 模块边界，目标是接近专业桌面工具的扫描速度、流畅界面与可解释的清理建议。

[![Platform](https://img.shields.io/badge/platform-Windows%2010%2B-0078D4)](#系统要求)
[![Qt](https://img.shields.io/badge/Qt-6.8%20MSVC%2064--bit-41CD52)](#构建)
[![License](https://img.shields.io/badge/license-MIT-blue)](LICENSE)

## 功能特性

- **磁盘空间分析**：目录树 + 目录内容表格 + 矩形空间占比图（Treemap），快速定位占用大户。
- **NTFS 极速扫描**：管理员权限下优先直接读取 NTFS 主文件表（MFT），失败时自动回退到多线程兼容扫描，兼顾速度与兼容性。
- **全系统快速搜索**：缓存固定磁盘索引，支持分页加载、路径 / 文件名匹配，覆盖整盘。
- **大文件 / 类型统计 / 疑似重复**：多视角洞察磁盘内容。
- **安全清理**：按清理类别分组，区分「安全」「谨慎」「高级」项，支持普通清理与深度清理；可移入回收站、可撤销。
- **三套主题**：浅色专业（默认）、暗色大师、蓝色清爽，菜单一键切换并记忆。
- **自绘图标体系**：界面图标全部由 QPainter 在 16 单位逻辑网格上手绘，按屏幕 devicePixelRatio 渲染，HiDPI 下清晰；不依赖任何第三方产品图标资源。

## 系统要求

- **操作系统**：Windows 10（1809 及以上）/ Windows 11（64 位）。
  - 限制来自 Qt 6.8 运行时本身，详见 [兼容性说明](#兼容性说明)。
- **运行权限**：NTFS 极速扫描需管理员权限（程序已声明 `requireAdministrator`）；无管理员权限时自动降级为兼容扫描。
- **运行时依赖**：动态链接 MSVC 运行时，目标机需装有 VC++ Redistributable；便携包已随附所需运行时 DLL。

## 构建

需要 Visual Studio 2022（或更新）与 Qt 6 MSVC 64 位套件。

```powershell
cmake -S . -B build-qt -G "NMake Makefiles" -DCMAKE_BUILD_TYPE=Release -DBUILD_QT_UI=ON -DCMAKE_PREFIX_PATH=E:\Qt\6.8.3\msvc2022_64
cmake --build build-qt --target DiskLens
```

产物为 `build-qt\磁盘洞察.exe`。使用 `scripts\package-release.ps1` 可进一步生成便携包与单文件包。

## 项目结构

```
DiskLens/
├── CMakeLists.txt          # 构建脚本（DiskLensNative 控制台版 + DiskLens Qt 版）
├── src/
│   ├── app/                # 原生控制台入口、资源与图标
│   ├── core/               # 扫描与格式化核心：目录扫描、NTFS MFT、长路径助手
│   └── qt/                 # Qt Widgets 界面：主窗口、图标、表格模型、空间图
├── docs/                   # UI 框架路线、开发步骤
├── tools/                  # 单文件启动器、资源打包工具
└── scripts/                # 发布打包脚本
```

## 兼容性说明

本工具在系统兼容性上做了针对性处理：

- **超长路径（> 260 字符）**：兼容扫描对深层目录（如 `node_modules`、`.git`、深层配置目录）启用 `\\?\` 前缀，不再静默漏扫；长路径的占用统计同样修正。
- **应用清单**：嵌入完整清单，声明 `supportedOS`（Windows 10 / 11）、`PerMonitorV2` DPI 感知、`longPathAware` 长路径支持、公共控件 v6，并保留管理员提权。
- **高分屏 / 多显示器**：在 1.25× / 1.5× 等分数缩放下采用向下取整圆整策略，避免文字发糊、线条错位；图标按高 DPR 基线渲染，在混 DPI 多显示器副屏上保持清晰。

> 关于「为何仅支持 Win10 / Win11」：这是 Qt 6.8 运行时的硬性要求，并非本项目清单限制。如需支持 Windows 7 / 8，需降级到 Qt 5.15 LTS 重新编译。

## 开发原则

- 所有源文件使用 UTF-8（无 BOM），中文直接写入源码与文档，不使用 `\uXXXX` 转义。
- 核心扫描、索引、清理逻辑全部本地执行，不依赖任何云端服务。
- 不使用任何第三方成熟产品的商标、图标、界面资源或专有实现。
- 所有公开函数与参数都提供注释说明。

## 文档

- [UI 框架路线](docs/UI_FRAMEWORK.md)
- [开发步骤](docs/DEVELOPMENT_STEPS.md)

## 品牌与发布名

| 项 | 值 |
|---|---|
| 英文项目名 | `DiskLens` |
| 中文产品名 | `磁盘洞察` |
| 主程序 | `磁盘洞察.exe` |
| 便携包 | `磁盘洞察-便携版.zip` |
| 单文件版 | `磁盘洞察-单文件版.exe` |
| 设置键 | `SunnyFan / DiskLens` |
| 缓存目录 | `%LOCALAPPDATA%\DiskLens` |

## 许可证

本项目基于 [MIT License](LICENSE) 开源，Copyright © 2026 SunnyFan。
