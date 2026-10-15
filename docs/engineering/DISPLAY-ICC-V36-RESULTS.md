# 显示 ICC V36 — 2026-10-14

S2继续，尚未全部完成。主显示分支新增Windows当前屏幕ICC查询、编码RGB到设备RGB的Little CMS转换及有界后台呈现队列。原图/工作帧/Recipe/直方图/完整源输出仍为既有合同；显示器配置不进入成片。

Windows采用GetICMProfileW标准查询，不读注册表/EDID绕过系统；Advanced Color缺省可能返回无profile，按官方约定使用sRGB，不自行开启ICC兼容/HDR。依据 [Microsoft ICC/Advanced Color](https://learn.microsoft.com/en-us/windows/win32/wcs/advanced-color-icc-profiles)、[GetICMProfileW](https://learn.microsoft.com/en-us/windows/win32/api/wingdi/nf-wingdi-geticmprofilew)。本模块仍是SDR，未宣称HDR或超出sRGB内容色域的显示能力已验收。

核心transformDisplay由GPT实现：输入标签或无标签sRGB→RGB显示目标，相对色度、alpha复制、4MiB限制、非法class/version拒绝、64行取消检查，不修改源图。原始输出/导出参考仍独立；设备代码只给画布。查询与转换不在GUI做全图运算。

队列一在途/一最新等待；同上下文不中断在途，以保留真实中间反馈，换上下文或显式失效用generation丢弃旧结果。呈现上下文包含照片/会话/提交revision/几何/view；不包含每个live draft/job。屏幕/配置改变清除旧设备帧，配置查明后重放最新canonical；同屏周期查询不取消正在转换的有效帧。

## 实际窄口径结果

- 构建exit0：build/private-qa/display-v36-gpt-build-poll-fixed.txt。
- DisplayTransform 5 passed（31ms）；125个合成sRGB色块的快速输出与LCMS参考最大DeltaE2000=0。包括不同RGB目标ICC、独立参考逐码值<=1、alpha/源图不变、非法配置/取消。这是数字转换对照，不是相机色卡或校准屏实测。
- DisplayQueue 7 passed（568ms）：中间A与最新C、跳过B、旧context、重用context后的invalidate、析构取消、回调重入及最大并行1。
- DisplayPresenter 15 passed（97ms）：配置/上下文变化、源不变、配方不变、非sRGB标签失败不假转换、明确sRGB回退、消息清理、真正帧可用才计绘制。
- 原生生成图诊断exit0，DPR1.25，窗口截图/几何保存 build/private-qa/ui-v36-system-display。仍须检查真实R7窗口与新全量回归，不能用V35的18/18替代新模块验收。

## 修复与限制

最终增量构建 exit 0：display-v36-test-fix-build.txt。最新专项为转换5 passed（33ms）、队列7（565ms）、呈现16（225ms，新增真实画布目标码值验证）、胶片条7（294ms，转换/失效/源角色不变/非法配置回退）。首次胶片条草稿有Qt构造/类型编译错误，以及QVariant(QIcon)和图像格式比较错误；GPT修复，失败日志保留，不采信工作者测试声明。

本机原生诊断 ui-v36-final-native exit 0：DISPLAY1 实际读取 sRGB Color Space Profile.icm、3144字节，pending=false；生成图的画布/胶片条显示可见。RAW/JPEG原生先前最新显示分支各3 passed（94512/80612ms），拖动中4/4及6/6预览/绘制；这些在最终缩略图接入之前运行，不替代最终包烟测。

最终完整CTest使用offscreen环境，21/22 passed（411.05秒），横向色差窗口失败。随后的原生Windows单项复查3 passed（33762ms），约2秒2次中间预览/2次真实绘制。两种运行结果分别保留，不将第一次记录改写为22/22。

查询/队列/胶片条实际会话日志已审查并保存.tools/audit-<UUID>.json：查询input8542/output6516/cache167296；队列18468/13270/510848；胶片条28272/10592/681856。读取工程文本，胶片条草稿另查window/cmake文本；没有发现读取私人图像或密钥。日志总数不代表人民币余额。

工作者草稿仅作草稿：GPT修复GetICMProfile首次不足缓冲返回FALSE的正常大小查询；重写混淆serial/generation且不能正确投递的队列；修正错误Qt测试API、重评估消耗semaphore的等待断言、手动配置被忽略、查询未完成先呈现回退帧、同屏轮询无故失效、跨屏查询在途新请求漏派发、无真实帧仍计paint等问题。初次失败记录保留。

DSH presenter会话b26eaadd-93cf-4e58-b6cc-f14681f0701f越界尝试构建并重试，最终900秒限时取消；不采信其构建/验证声明。已由GPT自己的工具链构建。队列会话54f4a63e-b22d-4772-9326-3b3bb21e472d、查询会话1ac82045-6cf9-4839-a82d-cbd56fca164c的实际参数/用量审计已保存，见上方最终追加。

待继续：显示配置与完整源输出隔离的端到端对照、摄影质量/资源及S2逐项收口。原生交互、缩略图转换和系统配置元数据已有上述窄口径证据；多显示器/校准硬件及真正HDR按原S5边界保留外部验证，不能称已通过。
