# LuminaS 构建：S2 / 4K缓存编辑

2026-10-15 GPU 首轮实验是独立可选目标，生产后端仍为 CPU。需要额外安装与 Qt 匹配的 Shader Tools 6.8.3（含 qsb），再执行 `cmake --preset dev -DLUMINA_BUILD_GPU_EXPERIMENT=ON` 和常规 `./tools/dev.ps1 build`。默认选项为 OFF，不开启时不需要 Shader Tools。运行 `./tools/run-gpu-experiment.ps1 -ReportDirectory build/private-qa/gpu-new -Size 4k`，或另用新目录加 `-Present` 测试独立原生画布；目录必须为空且位于 build 内。结果与未验证项见 [首轮 GPU 实验](GPU-EXPERIMENT-ROUND1-RESULTS.md)。不生成桌面包。

本机默认系统 TEMP 曾导致 Qt 的 QTemporaryDir 创建失败（S3 A0 首轮 20 个目标因此失败）；若出现同类环境问题，先创建工程内可写临时目录，再在构建/测试进程设置 `$env:TEMP="$PWD/build/private-qa/v18-temp"; $env:TMP=$env:TEMP`。此设置仅影响当前进程树，不改变系统配置；环境故障与测试断言失败分开记录。

项目名称固定为 **LuminaS**，仅英文名；生产窗口、字标、Qt applicationName、CMake项目与当前输出程序均已更名。运行程序为 `build/dev/LuminaS.exe`，tools/dev.ps1和run-s2.ps1使用新名称。内部lumina_* target、LUMINA_*测试环境变量及历史日志文件保留兼容，不是产品显示名。旧OpenMP实验构建须重新构建才有新命名程序，不把旧候选exe当最新生产窗口。

字标使用嵌入Qt资源的Source Serif 4 Regular，约256KiB；来源、commit/SHA与原OFL字体许可保存在assets/fonts。它是开放字体的自有字标实现，参考适马衬线/半衬线的方向；不是导入品牌专属字体。所有控件正常离线可用，不依赖系统是否安装该字体。

当前运行 `./tools/run-s2.ps1`，可用 `-Photos @('pictures4test/4X1A9579.CR3','pictures4test/4X1A9579.JPG')` 直接打开样片。前提是本机已构建build/dev；Qt开发依赖由该脚本配置，运行不需要Python或MSVC。已制作本机 V31 便携试用包；桌面快捷方式可直接启动。打包使用 tools/package-desktop.ps1，详见 DESKTOP-PACKAGE-V31-RESULTS.md；跨机发行验收尚未完成。

2026-10-13工作区更新：顶栏切换“选片/编辑”，导入按钮直接选择文件、下拉选择文件夹；画布工具为小手/裁剪/适屏/原图比较。底部导航、未标记/保留/排除、筛选与胶片条开关；选片不删除源文件。右侧调整卡片可折叠，包含白平衡、影调、色彩、RGB/R/G/B平滑点曲线、八色段HSL及构图。曲线点击加点、拖动、Delete删除内部点；一次拖动一条历史。窗口高度不足500 DIP会自动收起胶片条，扩大后按开关状态恢复。所有参数和标记仍仅本次会话，正式保存/导出尚未实现。

普通浏览：默认张开手，左键拖动时握手，松开恢复；滚轮围绕指针缩放（含仅pixelDelta触控板事件），双击适屏。只有主动裁剪时使用十字选择，完成或Esc退出回到小手。调色滑杆不改变浏览工具。

当前增加高光/阴影/白色/黑色/鲜艳度，均±1归一强度，单项↺复位可撤销。选片隐藏调整侧栏、编辑恢复；参数组箭头仅展开/收起、不关闭效果。侧栏可滚动，顶栏切深浅主题；直方图统计当前sRGB预览抽样，黑/白端提示不代表RAW传感器饱和。完整算法与边界见COLOR-CONTRACT.md。

当前还支持三范围颜色分级/八色黑白混色，以及“质感”卡片里的纹理、清晰度（±1）。两项采用真实邻域滤波、原图尺度半径与共享4K基底；没有去雾/锐化/降噪假滑杆。完整源按同一配方重放；其他全局参数与它们组合仍有持续拖动反馈。算子顺序、缓存格式/资源限制见COLOR-CONTRACT.md及第四批实测。

当前构建增加固定 Little CMS 2.19.1 核心静态库。以下S1段落是历史接口/实验说明；最新命令与编辑行为见本文S2段。

要求C++20、CMake≥3.24、Ninja、Qt≥6.8 Widgets/Concurrent/Gui、测试用QtTest和LibRaw0.22.2。Windows使用MSVC2022 x64及匹配Qt/LibRaw SDK；版本见ENVIRONMENT.md，RAW合同和实测限制见RAW-BASELINE.md。

## S2构建与检查

```powershell
./tools/install-lcms.ps1
$env:QT_ROOT = (Resolve-Path '.tools/Qt/6.8.3/msvc2022_64').Path
$env:LIBRAW_ROOT = (Resolve-Path '.tools/LibRaw-openmp/LibRaw-0.22.2').Path
$env:LCMS_SOURCE_DIR = (Resolve-Path '.tools/lcms/Little-CMS-lcms2.19.1').Path
./tools/dev.ps1 configure
./tools/dev.ps1 build
./tools/dev.ps1 test
./tools/run-s2.ps1
```

OpenMP SDK构建见OPTIMIZATION-EXPERIMENTS.md / tools/build-libraw-openmp.ps1；也可配置官方0.22.2 SDK，此轮S2性能数字来自OpenMP4线程。本机build/dev现在是S2路径，build/openmp旧S1产物不代表最新S2。应用默认OMP_NUM_THREADS=4、OMP_WAIT_POLICY=PASSIVE，保留调用者显式设置。

曝光、对比度、饱和度、RAW红/蓝及绿色平衡、JPEG RGB暖冷及绿色、校直、拖框裁剪、原图比较和跨照片Undo/Redo。调参只作用当前图，参数保留在会话内；一条拖杆一条历史，Ctrl+Z在文本焦点保留文本语义，Esc取消未提交调整。基本参数在4K缓存上计算，RAW WB拖动是缓存反馈、松手源精算；几何变化仍需生成相应4K采样缓存。长边3840，小图原尺寸。

导入后自动预解析全部新图片，胶片条标出4K就绪/失败，状态栏显示完成数。已经缓存的当前recipe点击即显示；后台复核源SHA并加载4K工作缓存，基本调参复用缓存，完整解析不逐张等点击才启动。内存显示缓存512MiB、会话临时浮点工作缓存4GiB，超过预算LRU淘汰；正常退出清理临时文件。首次后台尚未完成的图片仍需要解析，当前请求优先，缓存与永久保存分开。

默认CTest合成/数学/4K连续拖动；真实照片是可选本地测试。设置QT bin到PATH、QT_PLUGIN_PATH后：

```powershell
$env:QT_QPA_PLATFORM='windows'
$env:OMP_NUM_THREADS='4'
$env:OMP_WAIT_POLICY='PASSIVE'
$env:LUMINA_REAL_SAMPLES=(Resolve-Path 'pictures4test').Path.Replace('\','/')
$env:QTEST_FUNCTION_TIMEOUT='600000'
./build/dev/s2_tests.exe realR7Edits -o build/private-qa/s2/r7.txt,txt
$env:LUMINA_LIVE_SAMPLE=(Resolve-Path 'pictures4test/4X1A9579.CR3').Path
$env:LUMINA_LIVE_REPORT='build/private-qa/s2/live.json'
./build/dev/s2_tests.exe continuous4kDragFeedback -o build/private-qa/s2/live.txt,txt
$env:LUMINA_PREFETCH_REPORT='build/private-qa/s2/prefetch.json'
./build/dev/s2_tests.exe realImportPreparesAllBeforeClicks -o build/private-qa/s2/prefetch.txt,txt
./build/dev/s2_tests.exe liveWhileRawPrecisionIsRunning -o build/private-qa/s2/precision-live.txt,txt
```

工程全尺寸工具 `render_probe 输入路径 输出前缀 [recipe.json]` 从完整源计算预览和完整sRGB PNG并写JSON；输出前缀文件必须不存在，目录先建好。recipe键见core/document.cpp。它验证源计算，不是正式导出事务/UI。

去雾/构图批次已用同一路径验证`dehaze=.3,texture=.2,clarity=.15,flipHorizontal=true`及裁剪/校直组合，真实RAW/JPEG和45MP合成均退出0；数值与原生手势、最新CTest3/3记录在DEHAZE-GEOMETRY-V6-RESULTS.md。`flipHorizontal/flipVertical`必须是JSON布尔值；缺省false，去雾缺省0。

锐化批次`sharpenAmount=1,sharpenRadius=1.5,sharpenThreshold=.01`同时开启去雾/纹理/清晰度，无裁剪真实RAW/JPEG全尺寸与9000×5000合成JPEG通过；优化前后像素SHA相同。核心最终CTest3/3、115.28秒、原生4K反馈/历史、部署启动与限制见SHARPEN-V7-RESULTS.md。三个字段均须数值，缺省强度0/半径1/阈值0；半径为原图像素。

降噪批次`noiseLuminance=.5,noiseChroma=.6`组合上述空间效果，真实横幅RAW/竖幅JPEG/45MP合成完整源通过。新字段数值0–1、缺省0；源像素σ和条带/缓存合同见COLOR-CONTRACT。默认CTest3/3、131.34秒；新增用例后S2超过旧120秒，测试总时限改180秒（不改帧数/精度门槛），S1仍120秒。首次统计的秒级延迟与原生反馈/资源未通过项见DENOISE-V8-RESULTS.md。

预设批次增加风格卡片与自定义`.luminas-preset.json`保存/读取并应用；不是render_probe的裸recipe输入、项目保存或照片输出。保留当前WB/构图，单次历史，未知版本/字段拒绝。最新CTest3/3、130.02秒、真实RAW/JPEG与150/200%DPR、源SHA/启动详见PRESETS-V10-RESULTS.md。

```powershell
./build/dev/render_probe.exe pictures4test/4X1A9579.CR3 build/private-qa/s2/check build/private-qa/s2/recipe.json
```

屏幕sRGB SDR假设、基本参数公式、float范围、RAW桥接及误差见COLOR-CONTRACT.md。正式保存/恢复、局部/仿制、组同步、导出事务与发行尚未实现。

## S1历史：本机PowerShell

在工程根目录，已有S0的.tools/Qt和.tools/bootstrap时：

```powershell
./tools/install-libraw.ps1
$env:QT_ROOT = (Resolve-Path '.tools/Qt/6.8.3/msvc2022_64').Path
$env:LIBRAW_ROOT = (Resolve-Path '.tools/LibRaw-0.22.2/LibRaw-0.22.2').Path
./tools/dev.ps1 configure
./tools/dev.ps1 build
./tools/dev.ps1 test
./tools/dev.ps1 run
```

install-libraw从官方地址取得固定归档、核对本轮SHA-256、保留源码及许可证；已有依赖无需重新安装。Qt和引导工具安装命令见ENVIRONMENT.md，换机器设置实际QT_ROOT/LIBRAW_ROOT。

dev.ps1在普通Shell自动加载VS2022 x64环境，已存在cl时沿用调用者开发Shell；VSLANG=1033避免Ninja包含文件解析受中文编译器输出影响，优先本地bootstrap的CMake/Ninja。configure为cmake --fresh --preset dev，仅重建CMake缓存。共享预设通过环境读Qt/LibRaw路径，不硬编码本机路径；CMakeUserPresets.json可提供被忽略的用户配置。

正确开发Shell中也可直接执行cmake --preset dev、cmake --build --preset dev、ctest --preset dev。

应用是build/dev/LuminaS.exe，CLI是build/dev/raw_probe.exe。CMake复制LibRaw DLL至程序目录；Qt的bin须在PATH、plugins可找到，dev.ps1自动处理。未做干净机器部署/发行打包。非Windows需匹配的LibRaw0.22.2共享库及头文件，本轮未实测跨平台。

## 真实样片检查

```powershell
./tools/check-s1.ps1
```

依次对pictures4test/4X1A9817.CR3执行AHD/DCB完整RGB16基线、4K预览和同名JPEG对照，然后运行Windows原生JPEG/RAW测试，分别启动实际程序打开两种格式，核对源哈希。输出到新建build/private-qa/s1-时间戳目录；可用-OutputDirectory指定目录，已有probe输出不会覆盖，复用时加-SkipProbes。当前case针对这两个样本名，换样本需调整case/manifest，不把私人照片作为构建必需项。

```text
python tools/raw-probe/verify_outputs.py build/private-qa/s1-baseline
```

此独立校验只需Python标准库，Python不是应用依赖。实验PPM16是linear sRGB、RGB三通道、大端samples、无ICC，查看器可能按gamma图解释错误，必须结合JSON合同；PNG是encoded-sRGB 4K预览。

默认CTest未设置LUMINA_REAL_SAMPLES时真实case跳过。check-s1.ps1明确启用真实case；照片和截图留在被忽略的build目录。旧check-native.ps1用于合成JPEG；check-real-samples.ps1已更新为S1期望CR3启动成功，完整S1证据优先用check-s1.ps1。

## 使用与边界

打开照片支持多个JPEG/CR3，打开文件夹枚举当前目录、不递归。胶片条/上一张/下一张切图；Ctrl/Shift多选与▶当前图分开，标记仅作用当前图；三态筛选，排除不改/删源片，标记仅保存于本次内存会话。

滚轮指针中心缩放、左键平移、双击/适屏复位。当前预览长边3840，小图原尺寸，UI同时显示原图/实际预览尺寸，代理缩放不叫原图100%。JPEG后台缩放解码；RAW完整解码显影后缩小，默认AHD、质量标实验。胶片条小图不适用4K下限，未选中的RAW先占位，显影后更新。

唯一后台线程确保一个解码/显影在途，一项可替换的最新预览请求，当前任务优先于后续缩略图；token+路径拒绝旧结果。源文件只读，有效像素≤45,000,000。JPEG设置256MiB QImageReader分配限额、RAW设置1024MiB raw缓冲限额，两者不是整个进程内存总上限。没有固定忙刷新timer。

关闭清除待派发任务、断开回调并等待唯一在途任务结束；RAW关闭可能等待显影，隔离worker/完善取消属于S3。带输入的LuminaS.exe --smoke-test 路径：成功提交预览退出0，解码失败1，120秒未提交2；无输入只检查三秒启动/退出。

本阶段没有正式编辑、保存、完整导出UI。源CR3保留，实验RGB16不是无截断scene-linear母版；后续曝光/WB/色调与源重算由S2验证，正式导出事务由S4实现。

新增速度工具build/dev/speed_probe.exe，命令与测量边界见SPEED-CHECK.md。窗口路径跳过实验RGB16扫描/hash，源文件完整性检查及4K不变；raw_probe仍保留完整诊断。重复测试报告路径必须不存在，汇总工具校验优化前后像素一致并核对源hash。

另有独立多核候选：设置QT_ROOT后 `./tools/run-optimized.ps1 -Threads 4`，打开build/openmp/RelWithDebInfo/LuminaS.exe；该目录使用独立OpenMP SDK与LUMINA_DIRECT_RAW_PREVIEW=ON，默认dev保持对照配置。具体源码构建、VCOMP140运行依赖、实验缓存及下一步门槛见OPTIMIZATION-EXPERIMENTS.md。cache_probe仅为独立可行性实验，窗口尚未接入缓存。

2026-10-15 UI补充：构建需Qt6 Widgets/Concurrent/Svg；同版本QtSvg6.8.3模块已安装。本轮新UI验收未完成，见UI-UX-V43-PROGRESS.md；桌面V39保持。


## 图片导出v1依赖（2026-10-15）

新增静态LibTIFF4.7.1（ZIP/无压缩RGB和CMYK TIFF）及zlib1.3.1。固定源码来源与SHA-256见assets/licenses/export/SOURCES.json，许可在同目录。应用运行时不访问这些下载地址。

安装布局：LibTIFF源码.tools/export-deps/tiff-4.7.1，zlib源码.tools/export-deps/zlib-1.3.1；在已初始化MSVC2022 x64及Ninja的终端执行：

```powershell
cmake -S .tools/export-deps/zlib-1.3.1 -B .tools/export-deps/zlib-build -G Ninja -DCMAKE_BUILD_TYPE=Release -DZLIB_BUILD_EXAMPLES=OFF -DCMAKE_INSTALL_PREFIX="$PWD/.tools/export-deps/zlib-install"
cmake --build .tools/export-deps/zlib-build
cmake --install .tools/export-deps/zlib-build
```

随后沿用tools/dev.ps1 build。TIFF_SOURCE_DIR与ZLIB_ROOT可通过CMake缓存覆盖。cmake/ExportDependencies.cmake禁用TIFF工具/测试/其他压缩器，仅构建需要的库，不隐式联网下载。

导出专项：ctest --test-dir build/dev -R '^export_' --output-on-failure。Windows原生检查需QT_QPA_PLATFORM=windows和Qt bin/plugins环境；LCMS_SOURCE_DIR指向已有LCMS源码以读取其CMYK测试配置。真实R7与45MP验收为可选独立运行，设置LUMINA_EXPORT_TEST_SOURCES（路径以|分隔）及LUMINA_EXPORT_TEST_45MP=1后运行export_engine_tests realSamplesAnd45Mp。不要把私人照片加入源码或提交仓库。
