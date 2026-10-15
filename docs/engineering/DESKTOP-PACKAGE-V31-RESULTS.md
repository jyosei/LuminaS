# LuminaS 桌面试用包 V31 — 2026-10-14

已创建本机 Windows x64 便携包及桌面快捷方式；不宣称 S2 全部完成或正式发行验收。

- 程序目录：`C:/Users/CascadeAI/Desktop/LuminaS-preview-20261014-v31`
- 快捷方式：`C:/Users/CascadeAI/Desktop/LuminaS 试用版.lnk`
- ZIP：`C:/Users/CascadeAI/Desktop/LuminaS-preview-20261014-v31.zip`，12,919,508 bytes。
- 包含 Qt 6.8.3 Widgets、JPEG/Windows 插件、LibRaw、MSVC x64/OpenMP 运行库、字体和依赖许可文本。Little CMS 静态链接。包内无开发工具、工作者/API 密钥、样片或预览截图。
- ZIP SHA256：`2511CC4955A856DBF1AFC493DEB233BA149BF5CCF12AEED51CB9410E14A07639`。
- 解压内容 29,758,529 bytes，33 个文件；`package-manifest.json` 给出文件大小及 SHA256。

## 实际构建与运行

`tools/dev.ps1 build` 返回 0；CMake 的 LuminaS 主程序设置 `WIN32_EXECUTABLE TRUE`，桌面双击不附带控制台。处理算法没有因此改变。

`powershell.exe -NoProfile -ExecutionPolicy Bypass -File tools/package-desktop.ps1 -OutputRoot C:/Users/CascadeAI/Desktop` 完成；脚本拒绝覆盖已有包目录，采用本地 SDK 构建产物，不包含私人数据。

原始 windeployqt 两次返回 1：无法创建输出 Qt DLL，普通 PowerShell Copy-Item 成功。最终脚本保留错误，并使用明确的 Widgets 依赖清单复制回退；qt.conf 固定包内前缀/插件目录。运行库使用 VS 的 desktop x64 redist，排除 onecore。Qt SDK 缺少 LGPLv3 文本，已从官方 qtbase v6.8.3 LICENSES 补入项目/包中；来源记录在 assets/licenses/qt/SOURCES.txt。

最终包在 `PATH=C:/Windows/System32;C:/Windows` 且移除 QT_ROOT、QT_PLUGIN_PATH、QT_QPA_PLATFORM_PLUGIN_PATH、QML2_IMPORT_PATH、QT_QPA_PLATFORM 后，从包目录启动实际原生窗口：

| 检查 | 结果 | 整个进程耗时 |
| --- | --- | --- |
| 无文件启动/关闭 | exit 0 | 3936ms |
| R7 JPEG 4X1A0705.JPG 预览提交/关闭 | exit 0 | 10069ms |
| R7 CR3 4X1A0705.CR3 预览提交/关闭 | exit 0 | 11225ms |

耗时包含窗口初始化、解码、预览及烟测等待，不是解析速度基准。带样片的 smoke 仅在 previewChanged(success=true) 后返回 0；未收到预览有独立超时失败。

证据：build/private-qa/package-v31-build.txt、package-v31-deploy-final.txt、package-v31-final-smoke-results.json、package-v31-archive-check.json。此前 V31 处理回归 CTest 13/13 保持为此前证据；本次未重新声称完整 14 项回归通过。新去边色独立测试实际 7 passed（215ms），其中合成 3840×2560 操作 120ms、源不变；尚未接入生产参数/缓存/UI，不能作为试用功能。

## 限制和未通过项

- 正式导出界面未实现；关闭前未保存的照片编辑/选片状态不持久化。自定义预设库可持久化。
- 去边色、自动色差、镜头标定、显示 ICC 等剩余验收未完成，S3 仍仅准备。
- windeployqt 自动复制失败，明确清单回退已在本机验证；未来新增 Qt 模块必须同步清单。
- 未在另一台没有系统 VC 运行库的电脑验证，未验证全量 Windows 版本/驱动兼容性。清除 SDK PATH 不等于隔离所有系统 DLL。

## 执行审查

DSH 新会话 973df941-71f6-4eee-adea-8234c816c8d4 仅生成打包脚本草稿；GPT 修复 PowerShell 路径/UTF8 BOM、插件前缀、OpenMP 必需门禁、部署回退和说明，实际构建、部署、运行、打 ZIP/快捷方式。工作者的 parse claim 不替代 GPT 实际结果。

审查实际工具参数：读取指定工程文本/许可、SDK/VS 元数据和自己脚本；未发现读取私人图像内容或密钥。目录枚举比指定文本路径广，未把它描述为 OS 路径隔离。用量来自会话日志去重：input 14729、output 11231、cacheRead 413056、total 439016；不换算剩余人民币。审计保存 .tools/package-v31-audit.json。
