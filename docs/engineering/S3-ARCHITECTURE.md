# S3模块边界与协议草案

V32去边色S2基线（2026-10-14，仅作未来兼容约束，无任何S3生产实现）：手动紫绿边去色是独立于V31横向通道配准的算子，插入位置必须在几何/白平衡前缀之后、全局影调（曝光/曲线/HSL/分级等）之前，tile/worker不得把它排到影调之后或与横向色差合并成一步。输入仍是不可变straight linear工作RGB；输出只写新帧，不得回写源、准备帧或已烘焙WB的中间图。缓存身份须包含输入QImage key、几何/翻转/WB前缀、原尺寸、RAW标记、cameraToWork、draftBalance、实际X/Y支持尺度与目标边长，处理结果另含七参数；未来tile/shared snapshot必须携带同一配方字段与阶段版本，不能只用node/局部统计重建。临时范围视图（V26/V22类）需独立view身份与view epoch，不能进入去色缓存或持久化为Recipe，视图epoch变化必须使旧结果失效。当前窄口径证据仅core10/JSON7与合成·RAW·JPEG原生窗口各3通过；完整CTest16与RAW/JPEG/合成45MP完整源probe已通过，S3设计不得据此声称去色质量或性能已认证。详见DEFRINGE-V32-CONTRACT.md与DEFRINGE-V32-RESULTS.md。

V31色差准备合同：outputToSource代表绿色参考坐标，红/蓝按原始全源中心和原图像素单位另取样；RAW通过可逆cameraToWork恢复相机基底，JPEG为虚拟线性sRGB基底，均不裁负值/HDR。每通道alpha归一后共同alpha取最小覆盖，源邻居暗角先于取样。色差包含prepare/cache身份，烘焙后清零。未来tile/ROI必须传全源尺寸/偏移并覆盖三通道源足迹与halo，不能以tile中心重新锚定。S3仍仅准备，自动/紫绿边去色和摄影质量尚未通过。详见CHROMATIC-ABERRATION-V31-CONTRACT.md。

V30源几何合同：distortion是通用有界各向同性径向回源系数，k±.4、中心固定、半对角线归一，径向导数至少.55。当前映射顺序为flip/crop/angle→逆透视→径向回源，源邻居镜头暗角随后在采样前应用；不是亮度增益或镜头标定。4K准备/源/WB缓存身份包含distortion，烘焙后清零。未来tile/ROI需携带方向归一全源尺寸及偏移，使用完整非线性链计算源范围/halo；局部Jacobian与抗混叠质量仍待验证，不能沿用纯仿射尺度。S3仍仅准备。详见DISTORTION-V30-CONTRACT.md与DISTORTION-V30-RESULTS.md。

V28光学准备：后续tile/ROI的镜头暗角必须使用方向归一原始全源尺寸与ROI偏移计算径向坐标，不能使用tile中心或裁剪中心；需在prepare/cache key携带lensShading状态，已烘焙输入上不得再乘一遍。当前源校正先于几何采样/空间/全局色彩，局部与仿制的未来复合顺序必须显式维护这一边界。本批只有全局手动源径向补偿，没有标定镜头资料库或S3生产工具。

V27透视准备边界：crop现在属于校正全画布，S3源坐标笔触/仿制取样应通过包含逆单应的outputToSource定位方向归一原始源；不能把4K准备帧再变形一次。tile/halo/ROI必须处理非线性分母、无效映射和透明边界，不能仅用旧平移比例或假定原始传感器支持均匀；局部Jacobian与抗混叠质量仍需原型验证。完整源几何引用有独立生命周期：显示LRU只存普通图，取消/换图/零透视精算后应释放引用，允许有界在途任务共享。以下仍是S3准备要求，尚无tile/笔触/仿制生产实现。

V26已落地全局RenderView身份/普通显示缓存隔离与主窗口分色历史。S3 tile范围预览必须沿用分色前参考RGB和V26按编辑幅度加权归一，不能把零调整样本变成抑制旧效果的覆盖层。范围/蒙版可视化同属临时视图，结果匹配需携带view和局部版本，普通filmstrip/输出配方保持分离；取消取色后忙碌解除必须有状态通知，不能仅依赖成功采样回调恢复工具。这里仍是S3准备要求，未实现tile或局部工具。

V25分色准备约束：global pointColors为最多8项固定参考描述子与范围，不能以4K显示sRGB或每tile的局部统计重新取样；所有tile应对同一影调阶段的RGB评估权重，重叠归一规则保持。取色异步结果除已有session/revision/draft/job/geometry，还须包含取色取消generation与完整配方；S3未来新增local/repair版本时纳入就绪/结果门禁。范围预览为临时视图，需要独立请求身份与缓存隔离，不能持久化为Recipe或污染完整源重放；当前尚未实现该视图或局部工具。详见POINT-COLOR-V25-FOUNDATION.md。

V24颗粒准备约束：未来tile必须传入完整输出坐标原点偏移和独立XY源像素足迹比例，噪声域不可在每tile从0开始；grain应在局部/仿制复合与全局影调之后按完整配方执行，halo/裁剪/翻转不应改变噪声锚定语义。当前只有整幅4K缓存及完整源直接重放，尚未实现tile/local/repair；这些是S3设计要求，不作为生产能力验收。

2026-10-13。对应S3-IMPLEMENTATION-PLAN，以下类型/协议均为准备方案，尚无worker/蒙版/仿制生产实现；不能当作S3验收证据。

## 数据、坐标和命令

V23模型边界：点曲线最多64结点，Recipe可携带各结点显式切线及左右域外斜率（n+2数组），缺省仍为PCHIP。预设参数强度把结点并集上的值与切线作凸组合，保留三次函数和域外行为；亮度HDR右侧使用各分支非负外推斜率。未来tile/全局快照必须携带相同切线元数据，不能只传结点重新生成PCHIP。显示曲线消费同一元数据；手动编辑恢复该通道PCHIP，其他通道保持原状。强度基线/预设快照为当前窗口临时交互状态，提交结果是可独立保存、完整源重放的普通Recipe；不需要保存预览像素或嵌套渲染树。此处为S3准备，尚无tile生产实现。

V22准备边界：Recipe增加monochromeStrength（0–1、旧JSON缺省1）。部分强度在ToneOperator内独立完成彩色/黑白分支的RGB曲线、HSL与分级，再在线性工作RGB混合；alpha不变、无两幅整图缓存。未来tile须共享相同全局快照与查表，不能将它降格为RGB空间编码后的屏幕淡入。关闭黑白等效彩色分支，比例值保留；全局编辑保存/重放及缓存身份均须包含新字段。预设总强度仍未实现，需要另行解决应用前快照、曲线形状连续插值、手动调整失效与一次手势历史，不能将这个黑白比例当作总强度完成。

Asset保持source path/hash、归一完整尺寸、RAW WB/ICC facts。当前Recipe扩充全局字段仍为独立值，不把蒙版RGBA塞进Recipe。DocumentSnapshot下一步组合asset identity、global recipe、ordered local adjustments、clone strokes和revision，全部后台只读共享。局部参数变化/笔触提交走同一个EditHistory时间线：GlobalChange、LocalChange、StrokeCommit、CloneCommit；可记录小before/after状态引用，禁止一次笔触复制整张45MP float图。

V16原照白平衡语义：全局warmth/tint为相对偏移，零值RAW恢复相机记录的cam_mul，JPEG移除额外平衡但保留已编码机内颜色。未来worker/快照不得将零值解释成单位通道增益或自动白平衡；恢复操作只修改全局两个字段，未来局部/仿制笔触及引用需保留并按新源基底重新渲染。当前只实现S2复位，尚未实施上述S3复合行为。

V19裁剪后暗角：强度/中点/羽化为全局Recipe字段，当前在ToneOperator（包括RGB/HSL/分级）之后按最终构图的像素中心归一坐标乘RGB曝光增益，alpha不变；不是源坐标镜头校正。16385项增益LUT及x/y轴平方表最多约64KiB+4(W+H)，不新增全幅float蒙版；空间基底仍中性，不烘焙此效果。未来tile必须使用完整构图尺寸和tile绝对偏移，不能逐tile居中；局部复合后的全局最终效果顺序需按此定义验证。crop/angle/flip或构图尺寸变化应更新坐标表。纯翻转现已在CPU浮点/融合显示中按精确索引重排，不走双线性串行重采样；tile同样要用全图索引和一致alpha处理。这里规定S3准备边界，尚无局部/tile生产实现。

笔触记录：stroke_id、layer_id、源坐标采样序列(x/y、pressure、可选timestamp)、源像素半径、hardness、opacity、增/减操作。坐标是在方向归一原图中的像素中心，半径同样为原图像素；显示DIP和屏幕物理像素都不是持久化单位。输入端用Canvas逆变换得到输出像素，再用共享outputToSource的crop/angle/flip逆变换回完整源；每个渲染目标按同一源坐标重放。均匀采样时4K半径=源半径/sourcePixelsPerPixel；真实覆盖率按逆映射后的源坐标距离计算，避免非等比整数舍入或变换造成笔头错位。采样间距按源半径比例重采样，避免不同鼠标事件密度造成笔迹缺口。

mask覆盖率float0–1，软笔头径向平滑，增/减覆盖率合成公式需先冻结。建议增M'=M+(1−M)×a、减M'=M×(1−a)，a为笔头覆盖率×opacity×pressure；重复经过会累积，独立于时间戳。局部曝光/饱和度先以CPU参考定义在全局编辑之后、SDR之前；曝光RGB×[1+M×(2^EV−1)]，饱和度沿Rec.2020 Y用1+M×(sat−1)混合。命名/显隐/反选属于局部状态；全局曲线等仍共享现有合同。若后续改变算子顺序，应增加算法版本，不能静默重解旧数据。

仿制每笔保存source anchor、target anchor、源像素半径/软覆盖率和不可变取源基底标识。初版选择从未修饰的解码基底取样，防止边画边读自己导致递归污染；这一行为必须在工具提示中明示，再决定是否支持取样已有修饰。修饰重放在全局光色/局部之前；源ROI可以位于裁框外，不能只从当前裁后4K图取源。4K反馈可使用完整几何的有界代理，完整目标从真实源ROI重放，不放大代理。始终保存可重放笔触，缓存修饰像素可淘汰。

## tile与内存

TileKey至少含asset hash、work/algorithm version、geometry/mip、局部/修饰revision、tile x/y与tileSize；不同算子阶段不能共用错误key。先256/512两种逻辑tile，仅为缓存单位；RAW仍完整AHD。按有限空间算子support计算halo，r_source映射到mip为ceil(r_source/scale)+采样边界余量；clone另请求取样位移覆盖的源ROI。mask覆盖率不当作RGB/ICC数据。

缓存条目区分ready/in-flight/pinned，显示或正在使用的共享缓冲不被淘汰/修改。BudgetManager统计父/worker完整源、输出、ROI/halo、mask、4K帧、当前任务和旧任务同时存在的峰值；只限制tileLRU不足以证明资源合格。不得因取消立即释放其他线程仍持有的内存。最初局部tile预算建议256MiB，显示仍512MiB、临时work仍4GiB；这是待测起点，峰值超目标时明确调优而非宣称预算即实测。完整源与RAW入场许可仍只有1个。

S2空间基底现已实现，S3必须纳入预算：RGBA32F几何图+两张float细节图=24bytes/像素，R7 3840×2560为225MiB，方形3840×3840为337.5MiB；不是固定16:9估算。12张预缓存+一张空间编辑的本机测量稳定阶段私有提交上界约841MiB，预解析/准备峰值1355MiB，见spatial-v4-steady-resources.json；采样尾部包含关闭释放，不把最小值当稳定编辑占用。这是QtTest开发机，不替代8GB/真实45MP RAW进程树验收。

当前texture/clarity在几何采样后、全局光色前，曝光/曲线/HSL等不会重建基底。其三轮扩展盒核的实际有限support为3×(r+1)像素（保守包含零端权重），后续tile应按各阶段采样尺度计算halo并留双线性余量；R7原图清晰度sigma36.25，对应约108源像素support。256/512对照需测试宽halo下的真实代价与跨tile一致，不能把原图固定像素半径直接移到mip。强制取消正在算的tile也不能释放已借出的共享4K基底。

## 任务组织与取消

传统降噪已加入S2：4K至多新增DY/DCr/DCb三张float，空间全开总40bytes/像素，R7 4K375MiB、3840方形562.5MiB，另加至多约8MiB去雾系数。完整源不保留三张45MP差值图，而以128行条带+原始尾部halo备份进行原位组合；条带/整幅与原位/缓存数值对照已通过。自引导Gaussian两次均值需halo=6(r+1)，源σ为亮度2px/色差4px，按sourcePixelsPerPixel换算，不能只留一次模糊的support。S3 tile应从不可变源读邻域，仿制revision必须使统计失效；不能直接读取已修改的相邻输出tile。当前单次45MP合成全空间组合峰值2418.05MiB，真实45MP RAW/目标机仍待测。首次4K统计存在秒级准备延迟，持续反馈与首次响应须分别验收。

锐化扩充后，同开纹理/清晰度/锐化的4K基底为28bytes/像素：R7 262.5MiB、3840方形393.75MiB，另加去雾系数至多约8MiB。USM半径明确是原图像素标准差（.5–4），不能按原图长边再次放大；tile有限support使用既有三轮扩展盒公式并加采样余量。45MP合成JPEG不裁剪、同时开启四个空间效果的完整源计算峰值私有提交2417.33MiB，接近2.5GiB目标，分块与取消实现须测在途基底/输出重叠，不能只统计tile预算。这不是实际45MP RAW或目标8GB机认证。

V17新增独立锐化边缘蒙版：按需缓存uint16归一系数，另加2bytes/像素，R7 4K18.75MiB、方形28.125MiB；同开空间/降噪持久基底上界改为42bytes/像素（R7 393.75MiB、方形590.625MiB），另计去雾/在途缓冲。蒙版低通sigma=2/sourceScale、中心差分额外1像素，tile保守halo为3(r+1)+1再加几何采样余量。原图/调色仍float32，uint16仅存[0,1]派生权重。首版float系数45MP峰值2585.77MiB超过暂定2.5GiB；uint16同配方2498.83MiB，真实RAW/JPEG/45MP六个预览/完整输出与float参考差≤1码值且alpha不变。该配方仅锐化/蒙版/纹理/曝光/翻转，不是全部空间算子同开或真实45MP RAW/8GB机证明。后续源tile/仿制/几何变化须失效边缘系数；改变蒙版强度可复用固定系数。仍无生产tile实现。

S2新增去雾后，tile实现不能为每块单独估计大气光，否则产生接缝和不一致：A和小图a/b必须按整幅当前几何中性图估计并作为不可变共享基底。上界1024×1024两张float约8MiB，额外计入空间预算；每tile用一致全图坐标插值系数并读取其真实RGB。这不同于有限卷积的halo要求。翻转也已进入几何recipe，笔触输入逆变换须复用outputToSource的crop/angle/flip链，并以左右/上下/双轴镜像后的笔迹重放为必测，不自行另写符号规则。

从EditLoader抽出PrepareRequest/InteractiveRenderRequest/PrefetchRequest的实际调度到jobs，保留现有UI接口。JobTag继续session/committed revision/draft/job/geometry，新增local/repair revision可合并进document revision但必须在key与入场门禁中覆盖。UI线程是唯一提交者：入队/任务开始/呈现再次检查；后台不改history或控件。

取消顺序：先提升标签使结果失效，清等待项，再设置共享cancel token；源读取阶段/CPU行块或tile循环轮询。LibRaw progress callback尽力响应，已有QFuture cancel不能自动中断任意QtConcurrent::run代码。完成后释放资源许可，过期输出只删除本会话临时产物。即时4K lane仍独立，取消旧完整准备不能阻断有效缓存上的拖杆；新请求可覆盖等待项，已执行任务协作退出。

## RAW worker v1

父进程QProcess异步启动同包绝对路径raw-worker.exe、QStringList传参，不经过shell。Windows启动配置应隐藏控制台窗口，使用Qt6.8的create-process参数modifier与Win32对应标志，实施时验证stdout/stderr仍可通信。固定当前LibRaw0.22.2/MSVC x64/OMP4 PASSIVE与色彩合同；不调用历史浏览器。一次仅1个RAW worker在途，先一次请求一个进程以简化隔离/清理，再以测量决定是否常驻。父界面不阻塞waitForFinished。

控制通道为有长度上限的逐行JSON，stdout只协议、stderr诊断。Hello声明protocol=1/algorithmVersion/LibRaw与LCMS版本；Request含jobTag、sourcePath/hash、有效45MP限制、RAW WB、geometry、target4K、session-private output token。Prepare只应用WB与几何生成不可变4K工作帧，不烘焙曝光/曲线/局部等全局recipe，供父端继续实时渲染。Progress含phase/current/total但不假设所有阶段可线性累加成可靠ETA。Done含tag、源hash、归一尺寸、matrix/WB facts、binary路径/token/字节数/像素hash，Error含明确code与有限诊断；未知版本/缺字段/超限拒绝。

大float payload放会话私有二进制临时文件，不用stdout/base64。先沿现有PreviewCache的校验思路拆出可复用codec：固定magic/version、endianness、RGBA32F、width/height/stride、元数据长度和payload长度/hash；读取前用安全整数校验尺寸/预算，拒绝截断/多余/非法float或不一致facts。worker只能写父端分配的会话目录token目标，不接收任意用户输出路径；临时写完提交，父端验证后导入缓存。强杀残留按session归属清理，不能扫任意系统目录。

取消/关闭先令tag无效并停止派发，向worker发送Cancel；短宽限期后kill作为隔离兜底，Windows再用Job Object保证子进程树退出（仅在真实实现与故障实测后算通过）。worker内LibRaw回调读取cancel token；正在open/unpack等不可即时取消阶段不假装已中断。崩溃/超时输出不提交；源只读且前后hash核验。完整源导出与真实100%查看接口留S4，不在Prepare里冒充产品导出。

## 交付组织与入口检查

V20 自动白平衡仍读取当前几何准备后的 pre-global float 工作帧，不能从已烘焙曝光/曲线/局部修改的显示图统计。RAW 统计近似还原到原照 WB，依赖 cameraToWork、cameraWB 与实际显影暖冷/色调元数据；无法撤销截断或白平衡影响的插值。未来 worker/缓存协议必须保留这些事实及就绪版本，不能把估算元数据当成新的独立相机标定。估算只提交普通 WB recipe/一次历史，随后精确源显影；本条仅为 S3 准备边界。

单人开发按S3.0→.1→.2→.3→.4→.5推进，每切片提交实际代码/命令/截图或数学证据，再更新STATUS/06-PROGRESS。不存在空文件搭出架构即完成。首个实现请求应读取本文件、S3计划、当前COLOR-CONTRACT、S2审计及最后实际日志；发现现有行为与提案不一致时按实测修正。

参考：[Qt6.8 QProcess](https://doc.qt.io/qt-6.8/qprocess.html)、[QThreadPool](https://doc.qt.io/qt-6.8/qthreadpool.html)、[LibRaw API](https://www.libraw.org/docs/API-notes.html)。设计中采用现有6.8 API，不使用6.9+服务级别或升级默认Qt。Windows Job Object需实施前按[微软官方文档](https://learn.microsoft.com/en-us/windows/win32/procthread/job-objects)确认限制和嵌套行为；当前尚未测试。

S2 V13边界补充：全局Recipe现在含四区域参数曲线/三分界与独立亮度点曲线；按LUMA-CURVE-V13-RESULTS先参数再亮度、再RGB曲线等颜色操作，4K与完整源同算子。Prepare仍只产出中性几何/WB基底，不烘焙全局曲线。未来每次global snapshot创建共享曲线LUT，tile不能各自改变区域分界或用不同近黑阈值；缓存key须纳入完整recipe，不省略新字段。局部/仿制复合验收增加参数极端分界、近黑过渡、负亮度/超白、alpha、旧JSON缺省与全局曲线变化后旧tile失效。此处是S3准备要求，尚无tile实现。

V15就绪门禁：工具可用状态应查询同一份实际快照能力，不能从任意previewChanged次数或显示缓存存在推断浮点数据就绪。当前白平衡吸管已在Window显示完整tag匹配后，调用EditLoader的路径/几何/WB/源属性与无等待任务门禁；旧结果/比较/裁剪/交互中禁用。未来S3加入local/repair revision时，能力判定必须包含这些版本及所需源ROI，显示状态文案与实际工具API共用判定，控件更新后还需重查请求是否被同步事件更新。此处是准备要求，不是已实施局部工具。


