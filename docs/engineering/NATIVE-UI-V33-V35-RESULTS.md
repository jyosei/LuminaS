# 原生 UI V33–V35 — 2026-10-14

本批实际构建与原生验证通过，完整 CTest 18/18（430.06秒），证据 build/private-qa/ui-v35-ctest.txt。这是本批 UI 基线；新显示 ICC 模块仍在开发，不能沿用此数字宣称新模块通过。S2持续推进，未宣称全部完成。

## V33 分类导航

原诊断截图证明选择“紫绿边去色”仅把标题滚到视口底部，7个数值框全部不可见。改为展开并将目标卡片靠近视口顶部（8 DIP）；保留组合框焦点、其他卡片状态与 queued generation/QPointer 门禁。不更改配方或历史。

新回归覆盖从顶部/底部选择中间卡片，并要求卡片能放下时完整可见。初次失败因把卡片内部标题 padding 也算入12 DIP，GPT纠正期望；最终原生13 passed（1227ms），旧检查保留。证据 navigation-v33-native-final.txt。

实际系统缩放125%，QT_SCALE_FACTOR=1/1.5/2得到DPR1.25/1.875/2.5，不是实际OS100/150/200%。DPR1.25、1.875下7个参数完整在滚动视口内；DPR2.5下短视口只能部分容纳卡片，滚动后末项radius完整可见。截图/几何JSON使用工具自行生成的抽象图，未读私人照片。目录 build/private-qa/ui-v33-scale-* 与 ui-v33-scale-2-reach。

## V34 数值框箭头

深色主题数值框原箭头近似小点。添加原创16×16透明XPM上/下方向图标，明/暗与禁用共8份，Qt资源内嵌，不增加SVG依赖；留出文本右侧空间。值、步进、范围及焦点语义不改变。GTK/系统图标未借用。

GPT修正草稿透明色键、无效Qt样式调用和错误对比度公式；实际原生测试用控件 subControlRect 点击，在明/暗主题检查增减、上下边界、禁用与键盘输入，并计数真实按钮区域对背景亮度对比>3的箭头像素。三组缩放各7 passed（693/420/423ms）。证据 spin-indicators-v34-scale-*.txt，不是硬件可访问性认证。

DSH 7a450259-168b-40e5-89cc-fe73af08bb6f 达90工具次数后取消；范围外读取了UI/环境文档、SDK元数据及调用Qt DLL诊断，额外改动并损坏卡片中文标题。GPT从独立 .tools/v34-gpt-backup 恢复所有非样式区域，只接受审查后的QSS/资源并重写验证。实际审计未发现读取私人图片/凭据，不把它称为严格路径隔离。记录用量 input49676/output24078/cacheRead3093504/total3167258。

## V35 启动布局与桌面图标

高缩放下胶片条在首次布局时先撑大窗口、后隐藏，造成启动窗口超出屏幕。把既有响应式逻辑提取为updateResponsiveLayout，构造完成后首次show之前调用；resizeEvent继续调用，不改胶片条勾选意图。三组缩放的生产启动/后续尺寸阈值原生测试各3 passed。

DPR2.5实测可用768×408 DIP；原窗口frame728×481超屏，修正后frame728×363、client728×348，完整在可用区域。证据 startup-layout-v35-scale-*.txt、ui-v34-app-startup与ui-v35-app-startup的JSON。测试故意要求超屏的后续resize会产生Windows几何警告，启动fit断言仍通过；不据此认证所有多屏配置。

复用原生Qt绘制的LS/Source Serif 4字形导出256px ICO，并以Windows RC嵌入主程序。不是借用适马字体/品牌图。构建日志 startup-layout-v35-build.txt 有实际RC object编译；字型OFL仍保留。现有桌面便携包是V31，尚未以这些修改覆盖。

## 布局工具执行审计

ui_layout_probe 是可选开发工具，默认构建开关OFF，使用自身抽象JPEG并输出原生窗口PNG和文本几何。DSH 80269e3e-5aa1-417a-8fb5-3147bf0931e6 越界启动并重复卡住的构建；GPT按已验证PID/命令行停止该任务专属桥接子树，未停止用户Desktop配置。未采信worker构建结果。停止记录 .tools/dsh-ui-layout-probe-gpt-stop.json；GPT修正错误signal连接、等待不存在的sliderDown、加载失败软通过等后实际构建/运行。诊断图片留给GPT查看，未给worker读取。

仍待 S2 收口：显示ICC/颜色数学更强证据、摄影误去色/晕边及扩充参数完整审计。正式导出/保存流程、S3生产功能和S5实机认证按原阶段边界处理。
