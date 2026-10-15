# 本机环境与依赖（S0–S2）

S2当前组合仍是Qt6.8.3 / MSVC2022 x64 / CMake3.31.6，新增Little CMS核心源码静态构建，LibRaw使用独立OpenMP0.22.2 SDK、4线程。以下保留S0/S1历史，最新依赖如下。

Little CMS官方tag `lcms2.19.1`，来源 https://github.com/mm2/Little-CMS/releases/tag/lcms2.19.1 ，归档SHA-256 `8b7fdc5a708d9f5f41baeecafd2c32b993f0614448f2533eea5deaf841ea1c78`。本机 `.tools/lcms/Little-CMS-lcms2.19.1`；LCMS_SOURCE_DIR控制路径。上游此tag的头文件LCMS_VERSION及运行时cmsGetEncodedCMMversion均为2190，CMake项目版本2.19，不能把tag的patch号误写为运行时2191。实测头/库一致。

只构建MIT许可核心静态target lcms2；保留官方LICENSE，关闭工具/测试/shared及GPL fast_float/threaded插件，无插件注册。安装脚本tools/install-lcms.ps1校验固定源码归档；应用不依赖额外LCMS DLL。正式自有LICENSE仍未决定。

记录日期：2026-10-12（Asia/Shanghai）。执行前检查根目录与递归源码清单，没有 CMake/C++ 生产工程，没有适用的 AGENTS.md；历史 docs、prototype、review-notes、deliverables 和交接规格均保留。没有复用浏览器图像引擎。

## 实际机器

| 项目 | 实测 |
|---|---|
| OS | Windows 11 家庭版 中文版，10.0.26200，x64 |
| CPU | AMD Ryzen 5 5500U with Radeon Graphics |
| OS 可见物理内存 | 6,297,477,120 bytes，约 5.87 GiB（不是已确认的 8GB 验收机） |
| GPU / 驱动 | AMD Radeon(TM) Graphics / 31.0.21912.3005；S0 未使用 GPU 图像计算 |
| 系统 Python | 3.11.7，64-bit；只用于开发依赖引导，不是应用运行依赖 |

## 固定的本轮组合

| 组件 | 实际版本与位置 |
|---|---|
| Visual Studio | Community 2022，17.14.37301.10（17.14.32） |
| MSVC | cl 19.44.35227.0；工具目录 14.44.35207，Hostx64/x64 |
| Windows SDK | 10.0.26100.0 |
| Qt | 6.8.3，官方 win64_msvc2022_64 qtbase 动态库包 |
| Qt 运行 ABI | QtTest 输出 x86_64-little_endian-llp64 shared release，by MSVC 2022 |
| CMake | 3.31.6 |
| Ninja | Python 包 1.11.1.4；实际 executable 报 1.11.1.git.kitware.jobserver-1 |
| aqtinstall | 3.3.0，用来下载 Qt 官方包；引导包完整清单见 evidence/bootstrap-packages.txt |

初始 PATH 只有 Python/Git，没有 cl、CMake、Ninja、qmake。磁盘存在 VS 2022 C++ 工具及另一套 VS 2026 BuildTools 18.6.11806.211 / cl 19.51.36243.0。第一次自动发现选中了后者且构建成功，但最终以 VS 2022 重新配置构建，避免本轮组合漂移。工具脚本在普通 PowerShell 中用 vswhere 限定 [17.0,18.0)；已有开发者 Shell 时采用该 Shell，调用者应选择 x64 VS 2022。

本轮新增 `.tools/bootstrap` 独立 Python venv 和 `.tools/Qt/6.8.3/msvc2022_64`，没有更改系统 PATH，没有安装 LibRaw、Little CMS、SQLite、WebEngine。候选 Qt 6.12 并非本轮必要条件；选择官方存档中的 6.8.3 MSVC 2022 包作为固定 S0 验证组合，不代表后续版本决策。

```powershell
python -m venv .tools/bootstrap
./.tools/bootstrap/Scripts/python.exe -m pip install aqtinstall==3.3.0 cmake==3.31.6 ninja==1.11.1.4
./.tools/bootstrap/Scripts/python.exe -m aqt install-qt windows desktop 6.8.3 win64_msvc2022_64 -O .tools/Qt --archives qtbase
```

这些命令本机实际退出 0；aqt 下载 qtbase，重定向 qt.mirror.constant.com，完成约 43 秒。只安装 qtbase，不下载所有模块。Qt 来源：[官方二进制目录](https://download.qt.io/online/qtsdkrepository/windows_x86/desktop/qt6_683/qt6_683/qt.qt6.683.win64_msvc2022_64/)，兼容依据：[Qt 6.8 Windows 平台说明](https://doc.qt.io/qt-6.8/windows.html)。

开发安装中保留 Qt 自带 `sbom/qtbase-6.8.3.spdx.json`、source SPDX 等材料。Qt 和内置 JPEG/第三方依赖的正式分发许可、通知与源码交付仍属 S5/S7，S0 没有分发软件或创建正式源码 LICENSE。应用仅链接 Qt Widgets/Core/Gui/Concurrent，测试增加 QtTest；Python/aqt/CMake/Ninja 不链接进应用。

## 配置诊断

最终配置、编译与启动均退出 0，见 evidence/configure.log、build-final.log、native-startup.log。首次 vcvars 命令的 PowerShell/CMD 引号错误已修复，再次配置通过。最终 MSVC /W4 构建没有编译警告。

配置出现 `Could NOT find WrapVulkanHeaders`，以及 POSIX pthread 探测失败后 `Found Threads: TRUE`。这是可选 Vulkan 与 Windows 线程探测结果；本轮 QWidget/QImage CPU 预览不依赖 Vulkan，配置和链接均成功。未把这些信息当作 Vulkan/GPU 支持通过。

本机依赖路径仅在此环境记录、环境变量与实际日志中出现；共享 CMake/源码不硬编码 Qt 绝对路径。

## S1追加（2026-10-12，当前状态）

以上为S0环境历史；S1现已新增并验证LibRaw0.22.2官方Win64 MSVC2022 SDK，路径由LIBRAW_ROOT指定。本机SDK为 `.tools/LibRaw-0.22.2/LibRaw-0.22.2`，应用动态链接libraw.lib/libraw.dll，头文件版本与运行时0.22.2-Release核对通过。官方归档SHA-256为ac64fa12bb00a7581332d4c6ab918c0533fb3f119d6b668d47a6875410dca948；DLL为6a459c22039abf0eac4d263673337c8ed5f223acbd372fcf77610debf80ac8cd。

CMake/Qt/MSVC/SDK组合未更换，实际完整26步构建与原生JPEG/CR3启动均通过；共享构建未硬编码LibRaw本机路径。使用官方预编译库，不宣称本轮源码编译LibRaw。完整配置、来源、许可证、runtime capabilities=0的说明及内存实测见RAW-BASELINE.md。未增加LCMS、SQLite、GPL demosaic packs、GPU或RAW隔离worker。tools/install-libraw.ps1提供固定归档安装/哈希验证方式。

2026-10-15 UI补充：aqt同版本qtsvg archive安装exit0（25.50秒），Qt6Svg.dll 528520 bytes，Qt6.8.3/MSVC2022 ABI一致；CMake增加Svg模块用于图标，不参与照片色彩/采样。

2026-10-15 导出依赖：LibTIFF4.7.1源码及zlib1.3.1静态MSVC构建已接入；来源/本地归档SHA及许可见assets/licenses/export；构建步骤见BUILD.md导出依赖段。未生成或更新桌面包。
