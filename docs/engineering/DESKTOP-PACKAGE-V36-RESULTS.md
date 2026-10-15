# LuminaS 桌面试用包 V36 — 2026-10-14

已生成 Windows x64 便携试用包，保留 V31 旧包。S2 尚未宣布全部完成，未推进 S3。

- 目录：C:/Users/CascadeAI/Desktop/LuminaS-preview-20261014-v36。
- ZIP：同名 .zip，12,991,870 bytes；SHA256 E356011B59155014B13CE9630CE0AAC1DF5957F4BC4B24BD50BD34BC2BB9F170。
- 主程序 Windows GUI，带 LS 图标；包含 Qt Widgets、Windows/JPEG 插件、LibRaw、VC/OpenMP 运行库与许可。32 个运行/许可文件，另有逐文件 SHA256 的 package-manifest.json。未带样片、开发工具或 API 密钥。
- 新包包含 V32 去紫绿边、V33–V35 导航/数值框/启动布局改进和 V36 SDR 显示 ICC。大图长边 3840 预览，小图保持原尺寸。

## 实际证据

最终构建 exit 0：build/private-qa/display-v36-test-fix-build.txt。部署脚本 exit 0：package-v36-deploy.txt。windeployqt 再次无法复制 Qt DLL，脚本使用既有明确依赖复制回退；翻译目录和 DX 编译器缺失警告保留，原生 Widgets 启动通过。

清除 SDK PATH/Qt 环境，从新包目录启动原生窗口并自动退出：startup exit 0，5004ms；R7 JPEG exit 0，10316ms；R7 CR3 exit 0，11443ms。照片烟测在成功提交预览后关闭，包含初始化/解析/烟测等待，不是解析速度基准。12张原片SHA256零变化。证据：package-v36-startup-smoke.json、package-v36-photo-smoke.json、package-v36-source-check.json。

桌面 LuminaS 试用版.lnk 已指向 V36；V31 目录保留。ZIP逐项哈希核对见 package-v36-archive-check.json。

完整CTest最初21/22通过（411.05秒，offscreen环境）；横向色差窗口随后原生单测3 passed（33762ms，拖动中2次预览/2次绘制），原生单项CTest亦通过（34.45秒）。所有22个目标已有通过证据，但不是同一轮22/22；原失败记录保留。

最终模块专项：显示转换 5 passed（33ms）、队列 7 passed（565ms）、呈现 16 passed（225ms）、胶片条 7 passed（294ms）。原生生成图诊断 exit 0，画布/缩略图内容可见；系统 DISPLAY1 读取 sRGB Color Space Profile.icm，3144 字节、查询已结束。合成非 sRGB 显示目标画布像素亦通过；这些不代替校准屏实测。

## 限制

正式导出界面未实现；照片编辑/选片会话关闭后不持久化，自定义预设库可保存。未验收另一台干净电脑、校准多屏或 HDR。offscreen整轮曾有横向色差连续绘制测试失败，原生复查通过；未重跑整轮来抹去这条环境差异记录。

## 委托复核

DeepSeek DSH ac2769c4-3019-4e7f-9f9e-bd67cc5fa6dc 只修改打包脚本 README；GPT 对照备份确认只改该块，并修正版本变量插值、保持 UTF8 BOM。实际工具参数审查仅涉及指定脚本文本及语法检查，无照片/密钥读取。去重日志 input 4804、output 2932、cacheRead 69760，total 77496；不推算人民币余额。打包、归档和实际启动由 GPT 执行。
