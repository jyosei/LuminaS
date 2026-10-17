# S3 新设备交接

当前开发分支：`feat/s3-local-editing`，仓库：[jyosei/LuminaS](https://github.com/jyosei/LuminaS)。冻结提交、包内文件与哈希以迁移包根目录的`manifest.json`为准，不以历史日志中的HEAD代替。验收结论以`docs/engineering/STATUS.md`最新记录为准。

用户要求：S3验收后冻结功能，上传并准备开发迁移包，随后停止等待换设备。新设备接手先验证基线；只有用户继续授权后再讨论生产GPU或S4，不自动开展新功能。

## 恢复源码与历史

迁移包包含`source/`源码快照、`LuminaS-history.bundle`及精选数字证据。可在线克隆该分支，或从本地bundle恢复：

```powershell
git clone --branch feat/s3-local-editing ./LuminaS-history.bundle LuminaS
Set-Location LuminaS
git remote set-url origin https://github.com/jyosei/LuminaS.git
git status --short
git rev-parse HEAD
```

HEAD应与包内manifest的freezeCommit一致。`source/`是同一提交的备用快照，不带`.git`；开发优先用clone得到的仓库。

包不含私人样片、源照片/预览/截图、工具链、构建缓存、密钥、账户认证或旧桌面包。样片需要另行复制到被忽略的`pictures4test/`；已有项目缺源时通过应用的源链接入口选择哈希相同的原片，不把依赖下载清单当照片来源。

## 新机依赖

Windows、MSVC2022 x64 C++工具、CMake≥3.24、Ninja；Qt6.8.3 MSVC x64，包含Widgets/Concurrent/Svg/Sql/Test及SQLite插件。GPU实验默认OFF，生产CPU路径无需Shader Tools。Qt/引导工具的历史安装说明在`docs/engineering/ENVIRONMENT.md`，按新机实际位置配置，不能沿用旧机的绝对路径。

在VS2022 x64开发终端中，先安装固定版本依赖：

```powershell
./tools/install-libraw.ps1
./tools/build-libraw-openmp.ps1
./tools/install-lcms.ps1
```

第一个脚本取得并校验LibRaw0.22.2归档，第二个使用该归档构建独立OpenMP SDK，不把旧编译对象当新SDK。Little CMS使用tag`lcms2.19.1`核心，关闭GPL插件。

LibTIFF4.7.1/zlib1.3.1源码来源和SHA见`assets/licenses/export/SOURCES.json`；按`docs/engineering/BUILD.md`的“图片导出v1依赖”段放置源码、构建并安装zlib。CMake不隐式下载这些源码。该清单只用于开发依赖。

依赖就绪后，在同一终端设置三个实际路径：

```powershell
# QT_ROOT设置为新机实际安装的msvc2022_64目录。
$env:LIBRAW_ROOT = (Resolve-Path '.tools/LibRaw-openmp/LibRaw-0.22.2').Path
$env:LCMS_SOURCE_DIR = (Resolve-Path '.tools/lcms/Little-CMS-lcms2.19.1').Path
./tools/dev.ps1 configure
./tools/dev.ps1 build
./tools/dev.ps1 test
./tools/dev.ps1 smoke
```

`QT_ROOT`不能留空。构建与运行入口会配置Qt bin/plugins；RAW worker须与新构建的LuminaS.exe同目录，不能混用旧worker。CTest报告位于`build/dev/test-logs/`，默认私照/性能slot的skip不代表完成实照验收。

## 首轮验证与后续边界

先核对新机完整CTest、实际程序启动、R7 JPEG/CR3原片SHA、3840长边预览、非空局部层与撤销、项目往返/缺源链接、全源TIFF及成片100%查看。对照本包数字证据，重新记录机器和资源结果；旧机时间不是新机性能承诺。

局部全源测试的私有入口为`export_local_render_tests privateSamplesAnd45Mp`，显式设置`LUMINA_S3_EXPORT_SAMPLES`（多个源以`|`分隔）；合成45MP需另设`LUMINA_S3_EXPORT_45MP=1`。原生导出/检查入口为`export_ui_tests`，设置`LUMINA_EXPORT_UI_REAL_SOURCE`，不设置截图变量。运行前配置Qt bin/plugins及LCMS_SOURCE_DIR，必要时使用工程内可写TEMP。真实45MP RAW与8GB设备仍未认证。

GPU保持隔离；实验依据见工程GPU报告，不能把实验通过自动当作生产启用。首次批量解析和复杂空间算子性能仍有改进空间；30/60fps是额外目标。

## 可选开发协作工具

应用运行不需要DeepSeek或在线API。新机使用开发协作时另装Node.js22+并加入PATH，按`AGENTS.md`、`tools/deepseek/README.md`、`tools/dsh/README.md`配置。`tools/dsh/overlay.yml`的pwshPath应改为新机实际pwsh路径；launcher中的旧机Node后备路径不能当成可用环境。

密钥由用户在新机环境中自行配置，不从包中恢复认证信息。新建受限文本DSH会话并由GPT审查实际工具日志；不得恢复禁用会话`639b7ee6-4948-41bf-8d25-e520d321ecb4`，不得向工作者提供私人照片、缩略图或截图。旧机`.tools`中的QA助手、worker sessions和绝对路径不随源码迁移。
