# UI 框架路线

## 选择

市场版界面建议使用 Qt Widgets，而不是继续堆 Win32 原生控件。

原因：

- 目录树、表格、标签页、分割面板、状态栏都是 Qt Widgets 的成熟能力。
- 适合磁盘分析器这种高密度工具型界面。
- 可以复用现有 C++ 扫描核心，不需要把扫描逻辑搬到其他语言。
- Windows 部署可使用 Qt 官方 `windeployqt` 生成可分发目录。

## 当前工程状态

工程已经保留现有 Win32 版本，并新增可选 Qt 版本：

```powershell
cmake -S . -B build-qt -G "NMake Makefiles" -DCMAKE_BUILD_TYPE=Release -DBUILD_QT_UI=ON -DCMAKE_PREFIX_PATH=C:\Qt\6.x.x\msvc2022_64
cmake --build build-qt
```

如果 Qt 不在默认搜索路径，需要设置 `CMAKE_PREFIX_PATH` 或 `Qt6_ROOT` 指向 Qt 安装目录。

## 部署

Qt 官方建议 Windows 部署使用 `windeployqt`，它会收集运行所需的 Qt DLL、插件和模块：

```powershell
windeployqt .\build-qt\DiskLens.exe
```

## 后续 UI 目标

- 使用 Qt Model/View 替代一次性填表。
- Treemap 改为自绘控件。
- 扫描结果、类型统计、重复候选共用数据模型。
- 顶部命令栏改为图标按钮和分组操作。
- 支持主题色、暗色模式、列排序、右键菜单、路径面包屑。
