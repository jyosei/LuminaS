# 调整预设与原生风格卡片

2026-10-13，LuminaS。新增真实预设应用、保存、读取并应用；摄影算法沿现有CPU float32路径，未使用浏览器原型。

## 行为与文件合同

四个内置起点：复位风格、自然影调、柔和黑白、清晰细节。应用替换全部全局调色与细节参数，保留当前照片warmth/tint、crop/angle和双轴翻转；复位风格也只复位风格。不是智能场景识别或商业配置文件等价。一次应用一条历史，Undo/Redo可整体恢复；选择名称不改图。读取文件成功后加入当前窗口自定义列表并立即应用，失败不改参数或预设集合。

保存当前调色与细节为JSON，文件format=`LuminaS.adjustment-preset`、version=1，包含name/adjustments。参数精度由现有recipeJson保留；不包含照片路径/hash、ICC或相机/构图/WB字段。读入最多256KiB+1字节检查上限，拒绝未知字段/照片专属字段/不支持版本/非法类型或范围；成功验证后才更新调用方。空调整对象表示中性风格，缺省新参数取recipe默认。外部XMP/CUBE/商业预设格式没有兼容声明。

QSaveFile原子提交，参数或文件名无效时不覆盖旧JSON；核心保存仅接受.json后缀，不能覆盖JPEG/CR3。UI读取/应用/保存仅在正常编辑与数据就绪时使用，原图比较或裁剪中禁用并有调用保护。保存不是照片输出或项目保存；重新打开窗口需再读自定义文件，当前没有持久预设库/自动扫描/强度混合。

## 当前证据

- 构建日志presets-v10-build、final-build、final-ui-build、real-build均退出0。
- `presets-v10-native.txt`：6 passed、0 failed、1745ms；格式/精度与排除字段、拒绝无效版本/类型/WB字段、保留旧文件、JPEG后缀拒写，窗口应用/一条Undo/Redo/保存读取/失败不改当前图/比较禁用，以及既有历史保护和布局通过。原生风格卡片截图已查看。
- `presets-v10-real.txt`：真实横幅RAW9579、竖幅JPEG0705均四个内置预设+保存读取通过，3 passed、0 failed、57515ms；始终长边3840、crop/angle/水平翻转保留，实际源读取与现有算子参与。最后自定义画面截图已本地查看。
- `presets-v10-dpi150.txt`、`presets-v10-dpi200.txt`：真实JPEG分别3 passed、0 failed、11749/11643ms；1366×768和1920×1080屏幕预算、Qt实际DPR1.5/2布局/主题/stage/卡片/小手裁剪检查。2×小窗口预设按钮在下方，可滚动访问，画布下限保持；不是Windows系统设置/跨屏/触控设备验收。

全局实现清单仍以S2-COMPLETION-AUDIT为准，颗粒/预设强度、光学/校准/取色/边缘蒙版等未完成；首次降噪响应仍秒级，不能把预设入口当性能修复。S3准备边界不变，尚未实现完整项目/批量同步/正式照片输出。

最终默认CTest3/3、0失败、130.02秒（S2 126.30秒），见`presets-v10-ctest.log`；私有可选用例由上述独立运行证明，未将跳过算通过。12份原片SHA最终均与manifest一致，见`presets-v10-source-hashes.json`；最终LuminaS.exe Windows原生启动/关闭退出0，见`presets-v10-startup.log`。
