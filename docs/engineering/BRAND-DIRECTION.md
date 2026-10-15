# LuminaS 名称与字体方向

2026-10-13用户明确更名：仅用英文 **LuminaS**，大写L与末尾S，小写umina；中文名撤下。当前窗口标题/字标、Qt applicationName、CMake项目、构建输出与启动脚本已统一。工作目录仍按实际C:/Users/CascadeAI/Desktop/Lumina，不因品牌更名搬动用户目录；历史设计/日志/交接包文件名保留历史真实性，README明确其历史范围。

## 字体与原生实现

用户喜欢适马的衬线方向。其设计方[Stockholm Design Lab](https://www.stockholmdesignlab.se/work/sigma)说明Sigma Serif为具有工程几何基础的半衬线。LuminaS取结构清晰、衬线克制与摄影器材的精确感作为方向；当前字标使用开放字体 **Source Serif 4 Regular**，参数控件继续使用清晰的系统UI字体。字体是实际Qt资源，离线加载，窗口标题与字标始终由原生文本保证精确拼写。

来源：[Adobe Source Serif官方仓库](https://github.com/adobe-fonts/source-serif)。固定commit 80d3f8894c09c937bebfa9011247d2e1c79fd6f4，TTF 261868 bytes，SHA-256 e5a4ee6a3d87bb9024796be390c6771e2a0eb1883dae25effaf57ca01668e24b。原始OFL1.1许可、字体与来源manifest在assets/fonts，未经修改；qrc同时嵌入字体与许可。没有下载或分发品牌专属Sigma字体，项目正式源码LICENSE仍未选择。

原生LS应用图标由QPainter与嵌入字体绘制，灰底、浅色L和紫色S，在16–256尺寸生成；任务栏/窗口使用真实QIcon。当前未加入Windows EXE资源ICO的发行包装；实际程序名称已为LuminaS.exe。自绘控制图标仍按现有ui_primitives体系，不拿生成海报替代小尺寸功能图标。

## 平面设计稿

[LuminaS-graphic-concept-v1.png](../../assets/branding/LuminaS-graphic-concept-v1.png)为内置image_gen生成的原创灰紫平面探索，英文名已目视核对为LuminaS。它是品牌视觉概念，不是生产字体/图像引擎或商业适马素材。图片已从Codex生成目录复制进当前工程，原生成文件保留；未使用或上传用户照片。

使用的imagegen技能：[SKILL.md](C:/Users/CascadeAI/.codex/skills/.system/imagegen/SKILL.md)，内置工具模式；没有CLI/API fallback。最终完整prompt如下：

> Create an original flat graphic design poster for a photography editing application called exactly LuminaS. Spell and capitalize the only text exactly 'LuminaS' (capital L, lowercase umina, capital S), no other words and no Chinese text. Landscape editorial brand design, approximately 3:2 aspect. Charcoal grey #242528 paper background, off-white typography, restrained dusty lilac #bea3e9. Large refined engineered serif wordmark LuminaS on the left with strong clear stems and compact sharp serifs, calm precise typography; loosely inspired by contemporary Japanese optical instrument editorial design, an original identity without any existing brand logo. On the right an abstract flat optical composition made of concentric discs, apertures and thin intersecting light rays, stepped neutral greys and a single purple crescent, abundant negative space. Clean two-dimensional geometry, no 3D render, no gradients, no photographs, no camera illustration, no UI mockup, no extra initials, no Sigma logo or Greek sigma symbol. A polished usable graphic design concept sheet, minimal and intentional.

成图有轻微纸面/明暗质感，没有严格达到纯平涂；作为初版探索保留，尚未作为程序背景/品牌最终稿发布。后续视觉决定可在现有方向上迭代，功能开发继续按S2审计/S3计划推进。

## 实际检查

luminas-brand-build-final.log构建退出0；原生窗口精确标题/字标LuminaS、实际QFontInfo为Source Serif 4、QIcon非空、2×DPI两种屏幕预算和分级/黑白手势共4通过0失败、11578ms，见evidence/luminas-brand-native.txt。窗口抓图位于build/private-qa/s2/luminas-brand-dpi200，已目视检查。tools/dev.ps1 smoke实际运行新LuminaS.exe退出0；之后清除build/dev旧lumina.exe以防误启动旧品牌。当前生成稿已复制到工程assets/branding，不将私人窗口截图纳入公开设计素材。
