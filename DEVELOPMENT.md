# Worldbench 开发文档

> 文档版本：0.3
>
> 最后更新：2026-11-19（Asia/Hong_Kong）
>
> 项目阶段：规划；尚未开始产品实现
>
> 文档性质：项目规划与后续开发的唯一开发文档

## 1. 阅读规则与规划约束

### 1.1 唯一文档基准【已确认】

规划阶段，仓库仅跟踪本文件 `DEVELOPMENT.md`，不另建 README、AGENTS、独立需求文档、架构文档、路线图、任务清单、会议记录或开发日志，也不加入产品代码、脚手架、样例资源及构建配置。

本约束针对仓库中的项目内容；Git 自身的 `.git` 元数据不属于开发文档。进入实现阶段后，代码、测试、资源和必要配置可以按届时确认的范围增加；是否增加其他文档，应由项目发起人明确决定，不能由 LLM 自行拆分。

本文件维护当前有效状态，旧方案通过 Git 历史追溯。方案被替换后，应更新或删除对应正文，不能把互相冲突的新旧方案堆叠在文件中。变更记录只记录简短事实，不复制已失效方案。

### 1.2 信息优先级与状态标记

信息优先级：项目发起人的最新明确指示 > 本文件当前已确认内容 > 本文件建议方案和待验证假设 > 外部参考资料。用户的新指示与文档不一致时，先按新指示更新文档，再继续相关工作。

| 标记 | 含义 | 执行规则 |
| --- | --- | --- |
| 【已确认】 | 项目发起人明确提出或确认的要求 | 后续规划与实现必须遵守，除非用户变更要求 |
| 【建议】 | 当前分析提出的候选方案 | 不能当作最终架构、技术选型或承诺 |
| 【待决策】 | 会影响范围、架构或交付的未定事项 | 相关结论必须保持条件化 |
| 【待验证】 | 尚未通过原型、测试或性能测量验证的假设 | 不能在代码、文档或汇报中描述为已实现 |

未单独标记的算法、数据结构、阶段划分和验收指标均为规划建议。源码观察仅代表指定参考提交的实现，不代表 Worldbench 已具备相同能力。用户允许继续规划，不等于确认了本文全部建议。

### 1.3 LLM 协作规则

1. 每次继续项目工作前读取本文件，核对当前阶段、已确认需求和未决事项。
2. 用户确认新的决策后，直接更新相关章节；不要创建平行文档保存另一套说法。
3. 不得把示例数字、候选语言、参考项目能力或尚未执行的测试写成已确认事实。
4. 开发前先明确本次实现范围及可验证结果；没有授权时不把规划工作扩展成产品开发。
5. 修改文档后检查各章节是否一致，尤其是状态、范围、技术选型、路线图和验收标准。
6. 以 Git 保存历史。不得保留诸如 `DEVELOPMENT_old.md`、`plan_v2.md`、`TODO.md` 的并行规划文件。
7. 进入开发后仍维护本文的实际进度；不得仅凭生成了代码就标记功能完成。

## 2. 项目身份与愿景

| 项目 | 当前内容 | 状态 |
| --- | --- | --- |
| 名称 | Worldbench | 已确认 |
| 托管 | GitHub 私有仓库 | 已确认 |
| 仓库 | https://github.com/DanielFQZ/Worldbench | 已创建，私有 |
| 产品类型 | Minecraft 地图编辑与创作软件 | 已确认 |
| 主要交互 | 直接在 3D 视图中编辑与预览；始终体素/方块显示，不采用平滑地形预览 | 已确认 |
| 功能范围 | 从基础到进阶对齐 WorldPainter 的完整可用功能，保留实验与兼容边界 | 已确认；功能级静态清单见 5.5–5.9 |
| 常见地图规模 | 优先面向 8K × 8K、16K × 16K；128K × 128K 为极端目标 | 已确认优先级；精确 K 定义沿用分析假设 |
| 首轮测试版本 | 暂定 1.20.1，沿用当前 Java Edition 分析范围 | 版本已确认；其他版本列表待决策 |
| 创作输出 | 可选版本的 Minecraft 存档；导出时完整烘焙 | 已确认 |
| 实时目标 | 创作过程中的流畅编辑与实时预览 | 已确认；数值验收指标待定 |
| 实验功能 | 扩展默认世界高度，支持几千格高的创作 | 已确认方向；原版、数据包和模组边界见 8.3 |
| 目标硬件 | 常见创作者配置，参考 UE 等游戏引擎的要求 | 已确认方向；建议测试档位见 9.4 |
| 发布方向 | 不以盈利为目的，正式发布时开源，接受合规复用 GPL 代码 | 已确认；最终 SPDX 许可证待定 |
| 开发方式 | 先完成规划，后续逐步完善需求 | 已确认 |

产品希望让创作者在同一 3D 工作环境中完成大范围地形塑造、地貌实例调整、道路规划、建筑放置、地下空间创作和最终地图导出。

项目参考 WorldPainter 的功能与 Minecraft 数据处理经验，并优先评估可复用的开源实现。已经接受合规参考或复用 GPL 代码；具体复用模块、集成方式及是否分叉 WorldPainter，尚未决定。当前私有仓库保持私有，未来正式发布开源不等于现在立即更改仓库可见性。

## 3. 已确认的功能需求

### R-01：大型世界的 3D 编辑与预览

- 重点保障 8K × 8K 与 16K × 16K 的常见创作需求；128K × 128K 是最极端的规模目标。
- 创作者能够在 3D 视图中进行编辑和预览操作。
- 实时主要指创作时的预览和交互流畅，不要求每次操作立即完成最终质量的全图生成。
- 创作完成后通过一次完整烘焙导出 Minecraft 地图存档。
- 始终保持体素/方块预览，不提供平滑曲面地形预览模式；远处可参考 Distant Horizons 的 LOD 思路简化。
- 高度场、距离场等仍可作为内部存储或算法表示；具体体素 LOD、近远景衔接与更新方案尚未选择。
- 全图预览精度、局部编辑半径、目标帧率与后台计算时限尚未确认；硬件面向常见创作者配置。

### R-02：高度图地貌实例

- 导入高度图，将其作为地貌实例叠加到地形层。
- 支持程序化混合。
- 可调整实例位置、混合模式、侵蚀、噪波等参数。
- 具体输入格式、修改器顺序、混合模式列表和侵蚀模型待规划。

### R-03：路网绘制

- 在地表放置节点。
- 自动连接创作者指定的节点，生成地表路网。
- 自动连接的算法、道路横断面、交叉口、桥梁和隧道的范围待规划。

### R-04：建筑结构导入

- 导入 schematic 类文件。
- 在地图中快捷放置建筑和其他结构。
- 支持哪些具体格式与版本、实体数据处理和实例变换规则待规划。

### R-05：高度可自定义的洞穴

- 提供高度可自定义的洞穴创作能力。
- 支持导入 schematic 格式的洞穴结构并放入地下。
- 希望自动区分内部、外部和出口。
- 自动识别的可保证边界、人工校正方式和额外元数据方案待决策与验证。

### R-06：持续扩展

需求将在后续规划与开发中逐步完善。WorldPainter 从基础到进阶的完整功能对齐已纳入长期范围（R-09）；阶段交付范围仍需规划。其他未提出的新产品功能不能自行加入当前交付范围。

### R-07：多版本可选导出

- 首轮测试版本暂定为 1.20.1；当前继续按 Java Edition 规划。
- 后续应能像 WorldPainter 一样选择目标地图版本。
- 版本选择必须处理真实数据差异，不能只修改版本号字段。
- 首版是否即支持多个版本、首批版本清单和向旧版本转换的策略尚未决定。

### R-08：实验性超高世界

- 支持扩展默认原版世界高度，让创作者制作几千格高的地图。
- 编辑工程的可表达范围与 Minecraft 目标运行环境的可加载范围分开定义。
- 必须明确标注实验配置需要的数据包或模组，不能把超出原版边界的存档描述为原版可直接加载。
- 具体最大高度、Y 坐标范围、目标模组或是否自研配套修改尚未决定。

### R-09：WorldPainter 全功能复刻与优化

- 从基础编辑到高级图层、维度、导入导出、存档合并、脚本与扩展工作流，逐项纳入长期对齐范围。
- 功能按稳定编号与父子关系维护在本文；现有可用功能、条件功能、Alpha 与未接入代码必须区分，见 5.5–5.9。
- 可用 3D 操作与新架构优化旧工作流，不要求逐像素复刻 Swing 界面或复制历史技术限制。
- 原型和里程碑可以分期，但不能以早期最小功能替代完整范围；当前仍只做规划，尚未开始实现。

## 4. 可行性基线与规模估算

### 4.1 分析假设【待确认】

- 8K、16K、128K 暂分别按 8,192、16,384、131,072 个方块解释。
- 地图规模暂指单个维度的水平范围。
- 以下完整存档估算按整个方形范围全部生成计算。
- 首轮测试版本暂定 1.20.1；Edition 沿用 Java Edition 分析范围。多版本导出是已确认方向，完整支持列表未定。
- 384 层对应 1.20.1 默认主世界的垂直范围，并用于基础估算；实验性超高世界不以此为产品高度上限。
- 以下容量使用二进制单位：1 GiB = 2^30 字节，1 TiB = 2^40 字节。

### 4.2 可复核计算

常见工作范围与极端范围对照：

| 水平边长 | 水平位置数量 | 单张 32 位高度图 | 16 × 16 区块数量 | Region 区域数量 |
| --- | --- | --- | --- | --- |
| 8,192 | 67,108,864 | 256 MiB | 262,144 | 256 |
| 16,384 | 268,435,456 | 1 GiB | 1,048,576 | 1,024 |
| 131,072 | 17,179,869,184 | 64 GiB | 67,108,864 | 65,536 |

128K 极端情况的进一步估算，令 N = 131,072：

| 项目 | 计算 | 结果 |
| --- | --- | --- |
| 水平位置 | N² | 17,179,869,184 |
| 单张 16 位高度图 | N² × 2 字节 | 32 GiB |
| 单张 32 位高度图 | N² × 4 字节 | 64 GiB |
| Minecraft 区块数量 | (N / 16)² | 67,108,864 |
| 512 × 512 Region 区域数量 | (N / 512)² | 65,536 |
| 384 层、每体素 2 字节的稠密数据 | N² × 384 × 2 字节 | 12 TiB |

Region 数量仅表示单个维度完整覆盖该水平范围的区域数量，不包含额外实体、兴趣点或其他维度的数据文件。

上述容量是未压缩数组规模，不包含索引、材质、生态群系、实体、撤销数据、网格和缓存。稀疏存储、压缩和程序化表示可以减少实际存储与工作集，但无法保证任意高复杂度内容都很小。

### 4.3 可行性结论【待验证】

技术上存在可行路径：全域低精度预览、近处按需精确预览、按需计算及后台生成。研发优先级是让 8K/16K 的真实创作体验可用，同时保证架构可扩展至 128K。尚无 Worldbench 原型或基准测试证明目标帧率、编辑延迟、内存上限或导出速度。

“实时预览 + 完整烘焙导出”已经确认；以下操作分级仍是具体实现建议：

| 操作 | 建议行为 |
| --- | --- |
| 全域浏览、缩放、飞行 | 按需加载，分级细节显示 |
| 局部雕刻、材质绘制 | 优先处理当前工作区域，快速反馈 |
| 移动地貌或建筑实例 | 即时操纵反馈，局部结果逐步更新 |
| 大范围侵蚀、复杂选线 | 可取消、可报告进度的后台计算 |
| 全量存档导出 | 对固定工程快照完整求值、烘焙和写出；可恢复、可验证 |

## 5. WorldPainter 参考结论

参考仓库：https://github.com/Captain-Chaos/WorldPainter

本次检查提交：`8e3329aa7e7014dcda08f7ad25b8cb8fa97b4d2b`

检查方式：阅读固定提交的源码、构建说明、许可证及官方文档，并审计菜单/工具注册、表单、图层/格式提供者和导入导出路径；完整功能级清单见 5.5–5.9。未编译、未启动 GUI、未执行游戏加载或性能测试。

### 5.1 已观察到的能力

- `Tile` 以 128 × 128 水平块组织高度、地形、水位和图层数据。
- 存在高度图变换和组合能力，包括平移、旋转、缩放与求和。
- 存在 `.schematic` 及 Sponge `.schem` 解析，后者包含格式版本 1、2、3 的分支。
- 存在自定义洞穴层及洞底、洞顶、洞壁、噪波和挖掘流程。
- 存在 3D 预览；所检查的 `ThreeDeeView` / `Tile3DRenderer` 以 Tile 图像渲染和组合为基础。
- 导出流程包含 Region 并行生成、跨区域修正、后处理和光照处理。
- 存在 master/detail 组合等机制；不能将其简单概括成完全没有分级或分块能力。

### 5.2 对 Worldbench 的启示【建议】

优先参考格式处理、方块语义、导出顺序与边界修正经验。不要把既有 3D 预览直接等同于自由相机下的实时三维编辑架构，也不要假定接上新渲染器就自然满足 128K 的工作集限制。

是否复用导出器需要验证它与 `World2`、`Dimension`、Tile 集合等对象的依赖；拆出模块并不自动获得磁盘流式加载或新工程格式。

### 5.3 许可证与依赖【开源方向已确认，具体协议待决策】

- Worldbench 不以盈利为目的，计划正式发布时开源；允许在遵守许可证的前提下参考或复用 GPL 代码，不再将保留闭源发行作为选型目标。
- WorldPainter 使用 GPLv3。具体代码复用时保留版权和许可证声明、记录修改、提供对应源码，并核对依赖与所用文件的许可。非营利不意味着免除许可义务。
- 若采用 WorldPainter 代码，建议评估 GPL-3.0-or-later 作为 Worldbench 的项目许可；这尚未成为用户确认的最终 SPDX 选择，也尚未添加 LICENSE 文件。
- 私有仓库仅决定访问权限；当前保持私有，正式发布时再按用户指示公开。
- 独立进程或语言改写不能被当作许可证兼容性的自动结论。若选择 UE，还需复核其与 GPL 组件的组合许可，见 9.2。
- 完整 GUI 的构建文档涉及商业 JIDE 依赖，采用完整分叉路线需要额外处理。
- 当前没有复制任何 WorldPainter 产品源码到 Worldbench，也没有为 Worldbench 选定许可证。

### 5.4 固定版本的源码入口

- [项目说明与许可证](https://github.com/Captain-Chaos/WorldPainter/blob/8e3329aa7e7014dcda08f7ad25b8cb8fa97b4d2b/README.md)
- [构建说明](https://github.com/Captain-Chaos/WorldPainter/blob/8e3329aa7e7014dcda08f7ad25b8cb8fa97b4d2b/BUILDING.md)
- [Tile 数据结构](https://github.com/Captain-Chaos/WorldPainter/blob/8e3329aa7e7014dcda08f7ad25b8cb8fa97b4d2b/WorldPainter/WPCore/src/main/java/org/pepsoft/worldpainter/Tile.java)
- [高度图变换](https://github.com/Captain-Chaos/WorldPainter/blob/8e3329aa7e7014dcda08f7ad25b8cb8fa97b4d2b/WorldPainter/WPCore/src/main/java/org/pepsoft/worldpainter/heightMaps/TransformingHeightMap.java)
- [3D Tile 预览](https://github.com/Captain-Chaos/WorldPainter/blob/8e3329aa7e7014dcda08f7ad25b8cb8fa97b4d2b/WorldPainter/WPGUI/src/main/java/org/pepsoft/worldpainter/threedeeview/Tile3DRenderer.java)
- [Schem 导入](https://github.com/Captain-Chaos/WorldPainter/blob/8e3329aa7e7014dcda08f7ad25b8cb8fa97b4d2b/WorldPainter/WPCore/src/main/java/org/pepsoft/worldpainter/layers/bo2/Schem.java)
- [洞穴层](https://github.com/Captain-Chaos/WorldPainter/blob/8e3329aa7e7014dcda08f7ad25b8cb8fa97b4d2b/WorldPainter/WPCore/src/main/java/org/pepsoft/worldpainter/layers/tunnel/TunnelLayer.java)
- [世界导出](https://github.com/Captain-Chaos/WorldPainter/blob/8e3329aa7e7014dcda08f7ad25b8cb8fa97b4d2b/WorldPainter/WPCore/src/main/java/org/pepsoft/worldpainter/exporting/AbstractWorldExporter.java)
- [Region 文件处理](https://github.com/Captain-Chaos/WorldPainter/blob/8e3329aa7e7014dcda08f7ad25b8cb8fa97b4d2b/WorldPainter/WPCore/src/main/java/org/pepsoft/minecraft/RegionFile.java)
- [Sponge Schematic v3 格式](https://github.com/SpongePowered/Schematic-Specification/blob/master/versions/schematic-3.md)：外部规范链接未固定提交，实现前应重新核对并固定版本。

### 5.5 功能审计基准与范围【2026-11-19 静态审计】

本节是 Worldbench 的 WorldPainter 功能对齐基线。项目发起人已要求从基础到进阶全面复刻/优化；阶段拆分只决定先后，不删除高级能力。所有 Worldbench 实现状态均为**未开始**，不能把下表的 WorldPainter 源码状态理解为本项目完成状态。

- 固定源码提交仍为 `8e3329aa7e7014dcda08f7ad25b8cb8fa97b4d2b`；本轮核对远端 `master` 与此一致。源码父 POM 标示 `2.27.2-SNAPSHOT`，不是对正式发行版的认证。
- 审计方法：交叉检查菜单与工具注册、上下文菜单、动态选项、70 个 `.form` 文件、图层/平台/对象格式提供者、核心生成与导出路径、脚本与扩展接口，并参考官方手册。不是仅按类名生成清单。
- 覆盖 WPGUI、WPCore 和 WPDynmapPreviewer 中的用户工作流及支撑接口；独立诊断工具、实验/未接入代码另列，第三方插件的全部功能不冒充内置功能。
- **未编译、未启动 WorldPainter、未逐项执行交互、未运行导出或性能测试**。这是功能级静态审计，不是逐行正确性、安全性、无缺陷或全平台兼容性审计；未来发现漏项应在本文补充，不能把清单视为“不可能再有遗漏”的证明。
- 官方 Wiki 部分内容早于当前源码。例如旧命令行导出示例使用旧 API，自定义笔刷页的半径上限也不应覆盖当前可配置值。冲突时以固定提交的实际入口与实现为本轮依据。

### 5.6 编号、层级与对齐规则

`WP` 是对齐目录根节点；`WP-01` 等是一级功能域；`WP-01.01` 等是其直接子功能。编号作为后续任务和验收引用，新增条目追加编号，不能为排序便利重新编号。各表的标题给出父节点，因此父子关系不依赖文字缩进。`WB-*` 则表示 Worldbench 增量，不宣称源自 WorldPainter。

| 源码状态 | 含义 |
| --- | --- |
| A | 在固定提交发现可到达入口及对应实现；未运行验收 |
| C | 已接入，但受目标格式、维度、运行环境、输入或其他前置条件限制 |
| E | 已接入且源码/UI 明确标为实验或 Alpha |
| D | 存在代码/界面，但未发现正常产品入口、注册被注释，或实现是占位 |
| I | 内部 API、开发工具或支撑机制；不等同于普通用户菜单 |

| Worldbench 处理 | 对齐含义 |
| --- | --- |
| 复刻 | 保留可完成的创作任务、输入输出语义与关键参数；UI 可重新组织 |
| 优化 | 保留上述能力，并改进 3D 操作、非破坏编辑、性能或可靠性；具体方案仍待设计 |
| 适配 | 保留工作流，按 Worldbench 工程、目标版本或发布方式实现；差异必须明示 |
| 参考 | 记录源项目能力/原型，不自动承诺复制未发布功能、实现细节或品牌入口 |

A/C/E 功能全部进入长期对齐范围。C 的条件不能省略，E 必须重新验证后再宣称可用。D/I 中支撑正式功能的机制仍需有对应实现，但不要求复刻全部内部类和维护脚本。这里的“优化”不是已验证性能承诺。

其中明确标为“参考”的原作者捐赠、周边等品牌入口仅用于完整记账，不作为地图编辑功能交付项；不影响创作、兼容与维护能力的全量对齐要求。

兼容 WorldPainter 的 `.world`、`.layer`、`.terrain` 资产与复制其 Java 序列化实现是不同事项：交换/迁移需求纳入规划，但是否直接读取旧二进制、通过转换器迁移或复用其模块，要另行设计与验证。不能仅完成 Worldbench 自有保存格式就标记旧文件互通完成。

### 5.7 WorldPainter 功能父子清单

#### WP-01 工程生命周期与历史

源码：[App.java:928][wp-src-app-java-l928]、[WorldIO.java][wp-src-worldio-java]、[WorldHistoryDialog.java][wp-src-worldhistorydialog-java]。

| 子功能 ID | 功能与源码行为 | 状态 | Worldbench 处理 |
| --- | --- | --- | --- |
| WP-01.01 | 新建、打开 `.world` 工程，识别载入失败与旧工程信息 | A | 适配：原生工程与旧工程迁移分开验收 |
| WP-01.02 | 最近打开文件列表与再次打开 | A | 复刻 |
| WP-01.03 | 保存、另存为、未保存修改与退出提示 | A | 优化：大工程增量保存及一致性 |
| WP-01.04 | 工程备份及可配置备份数量 | A | 优化：明确保留、恢复和空间策略 |
| WP-01.05 | 自动保存、启动时检测可恢复自动存档 | A | 优化：崩溃恢复与主存档隔离 |
| WP-01.06 | 撤销、重做、笔画/操作保存点、可配置历史深度 | A | 优化：分块差量，避免全图复制 |
| WP-01.07 | 世界历史：创建、导入、变换、保存、载入、导出、合并等记录及显示过滤 | A | 适配：保留可追溯操作历史 |
| WP-01.08 | 工程中的自定义材料、图层、维度设置和资源引用一起保存 | A | 优化：缺失资源定位与可重定位引用 |
| WP-01.09 | 对旧格式/高度/旋转信息执行迁移并提示 | C | 适配：版本化工程迁移和备份 |

#### WP-02 建图、种子与地图范围

源码：[NewWorldDialog.java][wp-src-newworlddialog-java]、[TileEditor.java][wp-src-tileeditor-java]、[TileSelector.java][wp-src-tileselector-java]、[BiomesViewerFrame.java][wp-src-biomesviewerframe-java]。

| 子功能 ID | 功能与源码行为 | 状态 | Worldbench 处理 |
| --- | --- | --- | --- |
| WP-02.01 | 设置世界名称、目标地图格式、最低与最高建造边界 | C | 适配：目标版本配置独立于工程边界 |
| WP-02.02 | 创建矩形或圆形世界，设置尺寸 | A | 优化：8K/16K 优先，128K 流式建立 |
| WP-02.03 | 以 128×128 方块 Tile 组织编辑范围与尺寸显示 | A | 适配：保留区域管理，不强制沿用相同内部 Tile 尺寸 |
| WP-02.04 | 平坦/丘陵初始地形，基础高度、丘陵高度和水平尺度 | A | 优化：参数可重调的生成源 |
| WP-02.05 | 初始水位、水/岩浆、海滩、地表材料 | A | 复刻 |
| WP-02.06 | 简单地表方案与高级高度主题配置 | A | 复刻：高级配置不等同于未接入的 FancyTheme 编辑器 |
| WP-02.07 | 地形随机种子、Minecraft 种子和世界类型；随机及陆地/海洋预设入口 | C | 适配：区分编辑生成种子与游戏后续生成种子 |
| WP-02.08 | 新建/扩展时沿用默认生成设置，范围预览与步行距离估计 | A | 优化：显示成本估计与 3D 范围预览 |
| WP-02.09 | 增加/扩展/删除 Tile，框选、全选、反选、清除选择 | A | 优化：后台生成与可恢复删除 |
| WP-02.10 | 从生态群系浏览器选定区域建立世界 | C | 适配：受对应种子算法可用性限制 |
| WP-02.11 | 旧格式的扩展数字方块 ID 选项 | C | 适配：仅对相关旧版/模组配置开放 |

#### WP-03 多维度、Master/Detail 与天花板

源码：[App.java:3897][wp-src-app-java-l3897]、[Dimension.java][wp-src-dimension-java]、[DimensionPropertiesEditor.java][wp-src-dimensionpropertieseditor-java]、[World2.java][wp-src-world2-java]。

| 子功能 ID | 功能与源码行为 | 状态 | Worldbench 处理 |
| --- | --- | --- | --- |
| WP-03.01 | 主世界、下界、末地的创建、切换、删除及独立设置 | C | 复刻：受目标平台能力约束 |
| WP-03.02 | 各维度独立的地形、流体、主题、种子和生成器配置 | A | 复刻 |
| WP-03.03 | 主世界 Master/Detail：Master 按 1:16 比例描述更大范围，Detail 覆盖细节区域 | C | 优化：保留宏观/局部创作语义，不等同于渲染 LOD |
| WP-03.04 | 增加、删除 Master，Master/Detail 快捷切换及对应范围编辑 | C | 优化：明确覆盖、坐标映射与缺失 Detail 行为 |
| WP-03.05 | 创建、切换、删除倒置 ceiling 维度 | C | 优化：在 3D 中直接观察顶底关系 |
| WP-03.06 | ceiling 高度、与主维度的关联及导出组合 | C | 复刻：验证交叠、越界与空隙 |
| WP-03.07 | 洞底及浮空层的辅助编辑维度和返回入口 | C | 优化：统一地下/悬空编辑导航 |
| WP-03.08 | 维度属性与可用操作根据角色启用或禁用 | C | 适配：公开能力矩阵和错误原因 |

#### WP-04 导航、二维辅助视图与工作区

源码：[App.java:4115][wp-src-app-java-l4115]、[ConfigureViewDialog.java][wp-src-configureviewdialog-java]、[ConfigureOverlayDialog.java][wp-src-configureoverlaydialog-java]、[InfoPanel.java][wp-src-infopanel-java]。

| 子功能 ID | 功能与源码行为 | 状态 | Worldbench 处理 |
| --- | --- | --- | --- |
| WP-04.01 | 地图平移、缩放、重置缩放，滚轮与快捷键导航 | A | 优化：3D 主视口并保留正交顶视工作流 |
| WP-04.02 | 跳转出生点、世界原点、当前鼠标/编辑位置关联 | A | 优化：大范围坐标定位与相机书签可另设计 |
| WP-04.03 | 网格显示及网格尺寸 | A | 复刻 |
| WP-04.04 | 等高线显示及高度间隔 | A | 复刻：作为体素视口的辅助信息 |
| WP-04.05 | 地形材质、隐藏地形、彩色高度渐变、灰度高度显示 | A | 适配：分析着色模式不改变几何 |
| WP-04.06 | 旋转光照方向、配色方案切换 | A | 适配：统一体素预览的显示参数 |
| WP-04.07 | Minecraft 渲染距离圈、步行距离和边界显示 | A | 复刻：明确仅为辅助估计 |
| WP-04.08 | 多张参考覆盖图的新增、移除、启用、缩放、偏移、透明度及适配地图范围 | A | 优化：3D 地表投影/顶视参考，保持图像坐标可校验 |
| WP-04.09 | 背景颜色、背景图片及布局，地图外生态群系背景 | C | 适配：背景生物群系受旧版算法依赖限制 |
| WP-04.10 | 信息面板：坐标、高度、坡度、流体、地形、生态群系、图层值、选区状态 | A | 优化：加入拾取深度及当前编辑对象 |
| WP-04.11 | 可停靠面板、工具/笔刷/图层/调色板布局，保存/载入/重置工作区 | A | 复刻：不要求使用 Swing/JIDE |
| WP-04.12 | 图层 Show/Solo、地形/流体/生态群系/标注可见性 | A | 复刻：显示隐藏不等于禁止导出 |

#### WP-05 三维预览与局部生成预览

源码：[ThreeDeeFrame.java][wp-src-threedeeframe-java]、[ThreeDeeView.java][wp-src-threedeeview-java]、[Tile3DRenderer.java][wp-src-tile3drenderer-java]、[EditLayerDialog.java][wp-src-editlayerdialog-java]、[DynmapPreviewer.java][wp-src-dynmappreviewer-java]。

| 子功能 ID | 功能与源码行为 | 状态 | Worldbench 处理 |
| --- | --- | --- | --- |
| WP-05.01 | 独立 3D 预览窗口，Tile 图像组合的轴测显示 | A | 优化：替换为可直接编辑的实时体素主视口 |
| WP-05.02 | 预览平移、缩放、90° 方位旋转、原点/出生点定位 | A | 优化：自由相机并保留对齐方位 |
| WP-05.03 | 预览窗口置顶、与当前维度关联 | A | 适配：多视口或独立预览窗口 |
| WP-05.04 | 不显示图层、同步主视图、显示地表图层等预览模式 | A | 复刻：逐层诊断不改变导出设置 |
| WP-05.05 | 编辑后脏 Tile 更新、后台渲染、延迟刷新 | A | 优化：分级脏区更新和任务优先级 |
| WP-05.06 | 导出三维预览图片及图像尺寸检查 | A | 优化：大图分块输出，防止整图数组爆内存 |
| WP-05.07 | 自定义图层编辑中的生成样板预览、预览图案选择和参数变更重算 | C | 优化：与正式烘焙共享生成语义 |
| WP-05.08 | Dynmap 局部方块预览的方位/倾角/缩放/重置 | C | 适配：作为新体素渲染器的资产/图层预览能力 |
| WP-05.09 | 对象偏移设置中的模型观察与基点预览 | A | 优化：直接操纵实例基点 |

不将 WorldPainter 的轴测视图误称为已有自由 3D 编辑器，也不将局部 Dynmap 预览误称为全世界流式体素引擎。Worldbench 始终使用体素/方块几何；远处可以采用 Distant Horizons 式简化体素 LOD，见 WB-01。

#### WP-06 笔刷、输入与快捷操作

源码：[App.java:2897][wp-src-app-java-l2897]、[App.java:3593][wp-src-app-java-l3593]、[MouseOrTabletOperation.java][wp-src-mouseortabletoperation-java]、[BitmapBrush.java][wp-src-bitmapbrush-java]。

| 子功能 ID | 功能与源码行为 | 状态 | Worldbench 处理 |
| --- | --- | --- | --- |
| WP-06.01 | 圆形/方形笔刷，尖峰、线性、余弦、平台、穹顶、恒定强度分布 | A | 复刻 |
| WP-06.02 | 内置噪声和裂纹位图笔刷 | A | 复刻：素材许可另核对 |
| WP-06.03 | 灰度图片自定义笔刷、缩放、旋转和预览 | A | 优化：热导入及资源管理方案待设计 |
| WP-06.04 | 按文件夹组织笔刷面板，支持文件夹图标 | A | 适配：笔刷库分组与搜索 |
| WP-06.05 | 半径、强度、最大笔刷尺寸设置；强度数字键 | A | 优化：大笔刷成本提示与分块执行 |
| WP-06.06 | 半径按比例或单步改变，旋转 ±15°、30°、45°、90°及重置 | A | 复刻 |
| WP-06.07 | 鼠标正向/反向操作、右键/Alt 反向语义 | A | 适配：与 3D 相机输入明确分工 |
| WP-06.08 | 数位笔压力影响力度、橡皮端反向操作 | C | 复刻：驱动/平台支持与鼠标回退分别测试 |
| WP-06.09 | 快捷键切换工具、撤销重做、维度与显示选项；Esc 中断/清理交互 | A | 适配：保留快捷操作，键位配置另设计 |
| WP-06.10 | 自定义笔刷目录打开与重启后载入工作流 | A | 优化：热加载，不继承重启要求 |

#### WP-07 地形塑造与流体工具

源码：[App.java:3067][wp-src-app-java-l3067]、[App.java:3114][wp-src-app-java-l3114]、[Height.java][wp-src-height-java]、[Flatten.java][wp-src-flatten-java]、[Fill.java][wp-src-fill-java]、[Flood.java][wp-src-flood-java]、[Sponge.java][wp-src-sponge-java]。

| 子功能 ID | 功能与源码行为 | 状态 | Worldbench 处理 |
| --- | --- | --- | --- |
| WP-07.01 | Height 按笔刷抬高/降低地形 | A | 优化：体素视口拾取与局部反馈 |
| WP-07.02 | Flatten 以落笔高度整平 | A | 复刻 |
| WP-07.03 | Flatten 仅抬高、仅降低模式 | A | 复刻 |
| WP-07.04 | Smooth 平滑高度变化 | A | 复刻：改变地形数据，显示仍是体素阶梯 |
| WP-07.05 | Mountain 按笔刷轮廓堆山/挖坑 | A | 优化：增加可重调实例路线但保留直接笔刷 |
| WP-07.06 | 正向金字塔与旋转金字塔塑形 | A | 复刻 |
| WP-07.07 | 地形工具的 Apply theme：塑形时重新应用地表主题 | A | 复刻 |
| WP-07.08 | Fill 的有界区域抬升，以及附带绘制或应用主题 | A | 复刻：不要遗漏为普通油漆桶 |
| WP-07.09 | 灌水/灌岩浆；点击现有流体逐级升降水位 | A | 优化：区域边界可视化 |
| WP-07.10 | 水与岩浆互换 | A | 复刻 |
| WP-07.11 | Sponge 清除水和岩浆，反向恢复默认流体类型与高度 | A | 复刻 |
| WP-07.12 | 放置世界出生点与位置有效性提示 | C | 适配：按目标版本/维度校验 |

#### WP-08 绘制、取样与条件过滤

源码：[SprayPaint.java][wp-src-spraypaint-java]、[Pencil.java][wp-src-pencil-java]、[Text.java][wp-src-text-java]、[BrushOptions.java][wp-src-brushoptions-java]、[CombinedFilter.java][wp-src-combinedfilter-java]、[Eyedropper.java][wp-src-eyedropper-java]。

| 子功能 ID | 功能与源码行为 | 状态 | Worldbench 处理 |
| --- | --- | --- | --- |
| WP-08.01 | SprayPaint 以笔刷及强度绘制地形/图层/生态群系等 paint | A | 复刻 |
| WP-08.02 | Pencil 绘点、连续自由线、从上次位置连直线 | A | 复刻 |
| WP-08.03 | Pencil 的轴向/45° 约束 | A | 复刻：它不是道路拓扑系统 |
| WP-08.04 | Fill 连通区域填充，反向去图层/恢复主题/恢复自动生态群系 | A | 复刻 |
| WP-08.05 | Text 将文字作为所选 paint 绘入地图；字体、字号、粗斜体和角度 | A | 优化：3D 放置预览和编辑历史 |
| WP-08.06 | Eyedropper 从地图选择地形、图层、生态群系等当前画料 | A | 优化：多层/地下命中选择 |
| WP-08.07 | 高度上下限过滤及羽化 | A | 复刻 |
| WP-08.08 | 按坡度高于/低于阈值过滤 | A | 复刻 |
| WP-08.09 | only on / except on：地形、图层、生态群系、水、岩浆、陆地 | A | 复刻 |
| WP-08.10 | 按自动生态群系与特定标注颜色过滤 | A | 复刻 |
| WP-08.11 | 选区内/选区外限制，可与其他条件组合 | A | 复刻 |
| WP-08.12 | Add Another 复合条件；源码组合过滤器按顺序约束力度 | A | 优化：明确布尔和强度语义，不误称任意 OR 查询 |
| WP-08.13 | 用地图取样设置过滤条件，过滤器用于笔刷与全局操作 | A | 优化：可查看实际受影响区域 |

#### WP-09 选区、复制与全局操作

源码：[EditSelectionOperation.java][wp-src-editselectionoperation-java]、[CopySelectionOperation.java][wp-src-copyselectionoperation-java]、[CopySelectionOperationOptionsPanel.java][wp-src-copyselectionoperationoptionspanel-java]、[FillDialog.java][wp-src-filldialog-java]。

| 子功能 ID | 功能与源码行为 | 状态 | Worldbench 处理 |
| --- | --- | --- | --- |
| WP-09.01 | 用圆/方形及旋转笔刷增加、减去选区 | A | 优化：保留地表范围选区，三维体积选区另扩展 |
| WP-09.02 | 清除选区，选区存在状态及边界显示 | A | 复刻 |
| WP-09.03 | 把选区复制到指定位置 | A | 优化：预览冲突和跨块影响 |
| WP-09.04 | 分别选择复制高度、地形、流体高度/类型、图层、生态群系、标注 | A | 复刻 |
| WP-09.05 | 复制时边缘混合、清除目的区域原内容 | A | 复刻 |
| WP-09.06 | 复制到不存在的 Tile 时可创建 Tile；源码提示相关操作不可撤销 | C | 优化：可恢复操作，不能照搬不可撤销限制为目标 |
| WP-09.07 | 全局填充地形/图层/生态群系，删除或反转图层 | A | 优化：后台分块及取消 |
| WP-09.08 | 全局重置自动生态群系，或把自动结果转为显式生态群系 | A | 复刻 |
| WP-09.09 | 全局恢复地形/图层到主题，重置流体 | A | 复刻 |
| WP-09.10 | 全局增加/移除选区，与高度、坡度、画料过滤结合 | A | 复刻 |
| WP-09.11 | 全局操作的撤销、完成后保持对话框打开、关联变换入口 | A | 适配：统一任务面板与历史 |

#### WP-10 地形目录、方块与混合材料

源码：[Terrain.java][wp-src-terrain-java]、[CustomMaterialDialog.java][wp-src-custommaterialdialog-java]、[MaterialSelector.java][wp-src-materialselector-java]、[MixedMaterial.java][wp-src-mixedmaterial-java]、[App.java:1617][wp-src-app-java-l1617]。

| 子功能 ID | 功能与源码行为 | 状态 | Worldbench 处理 |
| --- | --- | --- | --- |
| WP-10.01 | 草地/裸草、泥土/粗泥、灰化土、泥巴、黏土、菌丝、苔藓等地表 | C | 复刻：按目标版本过滤材料 |
| WP-10.02 | 石头、岩石混合、石料混合、花岗岩、闪长岩、安山岩、深板岩、凝灰岩、方解石、圆石/苔石、基岩 | C | 复刻 |
| WP-10.03 | 沙、砂砾、砂岩、红沙/红砂岩、沙漠/红沙漠、恶地分层 | C | 复刻：包含材料内置附属生成差异 |
| WP-10.04 | 陶瓦与 16 色染色陶瓦 | C | 复刻 |
| WP-10.05 | 海滩/裸海滩、水、岩浆、黑曜石、岩浆块、深雪、泥土小径等特殊地形 | C | 复刻：区分水/岩浆“材料”与流体工具 |
| WP-10.06 | 下界岩、玄武岩、黑石、灵魂沙/土、菌岩、Netherlike、末地石 | C | 复刻 |
| WP-10.07 | 地形预设的地表/地下不同材料、植物装饰与默认生态群系语义 | C | 复刻：不能仅用一个颜色近似预设 |
| WP-10.08 | 数字 ID/data 和命名方块 namespace/properties 的材料选择 | C | 适配：版本化方块注册表与合法性检查 |
| WP-10.09 | 自定义材料命名、显示色、简单单方块模式 | A | 复刻 |
| WP-10.10 | 噪声混合、斑块/团簇混合，成分权重及尺度 | A | 复刻 |
| WP-10.11 | 分层材料、层厚/变化、重复、X/Z 倾斜及相对高度锚定 | A | 复刻 |
| WP-10.12 | 自定义地形槽位和现有材料共享；源码有 96 个 custom terrain 槽位 | A | 优化：不把固定槽位上限移植为产品限制 |
| WP-10.13 | 创建、编辑、删除、从文件/其他世界导入、导出材料文件 | A | 适配：包含 `.terrain` 迁移/互通设计 |
| WP-10.14 | 弃用旧地形如 Snow on Rock 的旧工程保留语义 | C | 适配：迁移提示，不无声替换 |

#### WP-11 高度主题与噪波参数

源码：[SimpleThemeEditor.java][wp-src-simplethemeeditor-java]、[SimpleTheme.java][wp-src-simpletheme-java]、[NoiseSettingsEditor.java][wp-src-noisesettingseditor-java]、[NoiseSettingsDialog.java][wp-src-noisesettingsdialog-java]。

| 子功能 ID | 功能与源码行为 | 状态 | Worldbench 处理 |
| --- | --- | --- | --- |
| WP-11.01 | 按高度区间配置地形材料，新增/编辑/删除区间 | A | 复刻 |
| WP-11.02 | 按高度范围配置图层及范围边缘羽化 | A | 复刻 |
| WP-11.03 | 主题高度边界随机化/噪声边缘 | A | 复刻 |
| WP-11.04 | 海滩与水位相关的主题行为 | A | 复刻 |
| WP-11.05 | 新世界/扩展 Tile 默认主题与当前维度主题 | A | 复刻 |
| WP-11.06 | 已有地图按主题重算，塑形时应用主题 | A | 优化：非破坏主题层与显式烘焙操作 |
| WP-11.07 | 多处共用的噪声高度变化、尺度和粗糙度配置 | A | 优化：参数命名一致，保存可重复种子 |
| WP-11.08 | 高度变换时同步调整主题/相关图层设置 | C | 复刻：变换前后说明受影响参数 |

#### WP-12 生态群系与种子浏览

源码：[BiomesPanel.java][wp-src-biomespanel-java]、[CustomBiomeDialog.java][wp-src-custombiomedialog-java]、[BiomesViewerFrame.java][wp-src-biomesviewerframe-java]、[Dimension.java][wp-src-dimension-java]、[DefaultPlugin.java][wp-src-defaultplugin-java]。

| 子功能 ID | 功能与源码行为 | 状态 | Worldbench 处理 |
| --- | --- | --- | --- |
| WP-12.01 | 目标格式对应的内置生态群系选择、绘制与显示 | C | 复刻 |
| WP-12.02 | 自动生态群系根据地形、水、森林/雪等信息推导 | A | 复刻：不是完整 Minecraft 原生地形生成器 |
| WP-12.03 | 显式生态群系覆盖自动结果，擦除恢复自动 | A | 复刻 |
| WP-12.04 | 自定义生态群系数字 ID 或命名 ID、名称、颜色/图案 | C | 复刻：注册标识不等于自动生成所有游戏数据包定义 |
| WP-12.05 | 2D、3D 和命名生态群系的导出能力差异 | C | 适配：垂直体素生态群系与目标粒度验证 |
| WP-12.06 | 地下默认生态群系、洞穴生态群系、浮空区域生态群系 | C | 复刻 |
| WP-12.07 | 按种子查看原版生态群系，切换支持的算法/普通与大型群系 | C | 适配：源码浏览器主要为 1.1–1.12 算法，不能宣称支持全部新版本 |
| WP-12.08 | 种子浏览缩放/重置、复制种子到当前世界 | C | 复刻 |
| WP-12.09 | 从种子浏览范围创建世界、在所选位置创建生存/创造存档 | C | 适配：与实际可用游戏生成器能力绑定 |

#### WP-13 图层、调色板与资源交换

源码：[CustomLayerController.java][wp-src-customlayercontroller-java]、[Palette.java][wp-src-palette-java]、[EditPaletteDialog.java][wp-src-editpalettedialog-java]、[ImportCustomItemsDialog.java][wp-src-importcustomitemsdialog-java]、[DeleteLayersDialog.java][wp-src-deletelayersdialog-java]。

| 子功能 ID | 功能与源码行为 | 状态 | Worldbench 处理 |
| --- | --- | --- | --- |
| WP-13.01 | 内置/自定义图层选择、强度绘制、擦除与显示 | A | 复刻 |
| WP-13.02 | 自定义图层名称、描述/显示 paint 及类型特有设置 | A | 复刻 |
| WP-13.03 | 编辑、复制、移除图层；个别类型限制复制 | C | 优化：资源依赖及关联维度一并处理 |
| WP-13.04 | Find 定位最近使用该图层的区域 | A | 优化：检索与 3D 跳转 |
| WP-13.05 | 自定义图层导入/导出文件，部分类型禁止独立导出 | C | 适配：包含 `.layer` 迁移及依赖打包设计 |
| WP-13.06 | 从另一世界选择导入图层/地形等自定义项 | A | 适配：冲突、重复标识、缺失资源提示 |
| WP-13.07 | 从另一维度复制图层为独立身份和设置 | A | 复刻 |
| WP-13.08 | 新建调色板、图层跨板移动、重命名、人工顺序和字母排序 | A | 复刻 |
| WP-13.09 | 调色板整体 Show/Solo、隐藏组件图层和空板清理 | A | 复刻 |
| WP-13.10 | 清理未使用图层及删除前选择 | A | 优化：先分析依赖，避免删除间接引用 |
| WP-13.11 | 图层实际导出开关、按导出 pass 排序、选择/重置顺序 | C | 复刻：与显示开关明确分离 |
| WP-13.12 | 组合图层内嵌组件导入与隐藏图层维护 | A | 复刻 |
| WP-13.13 | 按维度角色过滤可用图层及其编辑动作 | C | 适配：能力限制可解释 |

#### WP-14 内置生成层与维度环境

源码：[DefaultPlugin.java:38][wp-src-defaultplugin-java-l38]、[DimensionPropertiesEditor.java][wp-src-dimensionpropertieseditor-java]、[ReadOnly.java][wp-src-readonly-java]、[NotPresent.java][wp-src-notpresent-java]、[ResourcesExporter.java][wp-src-resourcesexporter-java]。

| 子功能 ID | 功能与源码行为 | 状态 | Worldbench 处理 |
| --- | --- | --- | --- |
| WP-14.01 | Frost 冰雪覆盖，雪层形态与树下结霜选项 | C | 复刻：雪层方块仍是体素/原生方块形状 |
| WP-14.02 | 落叶林、针叶林、沼泽和丛林生成及强度 | A | 复刻 |
| WP-14.03 | Caves、Caverns、Chasms 三类内置地下生成 | A | 复刻 |
| WP-14.04 | 三类洞穴的最小/最大高度、打破地表、积水/岩浆、去除地表流体 | A | 复刻 |
| WP-14.05 | 蘑菇、发光地衣、繁茂/滴水石等洞穴装饰开关 | C | 复刻：目标版本可用性校验 |
| WP-14.06 | Resources 资源层的各资源丰度、高度范围及默认值重置 | C | 复刻 |
| WP-14.07 | 煤、铁、金、红石、青金石、钻石、绿宝石、铜、泥土、砂砾、水、岩浆、下界石英、远古残骸等资源项 | C | 复刻：版本化预设和可扩展目录 |
| WP-14.08 | 图层 everywhere 设置：资源、洞穴、森林、丛林、沼泽、霜雪等默认覆盖 | A | 复刻：区分默认值和显式笔刷值 |
| WP-14.09 | Void 生成虚空区域 | A | 复刻：不等同于只隐藏视图 |
| WP-14.10 | Read Only 标记合并时不得修改的区块 | A | 复刻：不是编辑锁的同义词 |
| WP-14.11 | Not Present 保存导入时不存在的区块信息 | I | 适配：缺失区块与真实空气分开表示 |
| WP-14.12 | Populate 层及全维度 Populate，由 Minecraft 后续补充生成 | C | 复刻：平台能力与生成器限制需提示，不保证全版本同效果 |
| WP-14.13 | 地下材料、表土最小/最大深度、陡坡覆盖、相对高度锚定 | A | 复刻 |
| WP-14.14 | 无底、基岩/屏障墙与顶盖设置 | C | 复刻：和天花板维度分别验收 |
| WP-14.15 | 水/岩浆/虚空/屏障边界、有限宽度或无限边界、边界高度 | C | 复刻 |

#### WP-15 自定义地表覆盖层

源码：[GroundCoverLayerEditor.java][wp-src-groundcoverlayereditor-java]、[GroundCoverLayerExporter.java][wp-src-groundcoverlayerexporter-java]。

| 子功能 ID | 功能与源码行为 | 状态 | Worldbench 处理 |
| --- | --- | --- | --- |
| WP-15.01 | 自定义覆盖材料，包括混合材料 | A | 复刻 |
| WP-15.02 | 正厚度向上覆盖，负厚度向地表内部替换 | A | 复刻 |
| WP-15.03 | 覆盖厚度及噪声变化 | A | 复刻 |
| WP-15.04 | 边缘形状：垂直、线性、平滑、圆角，边缘宽度 | A | 复刻：这是生成轮廓算法，输出仍为方块 |
| WP-15.05 | 支持材料的平滑厚度/薄层处理 | C | 复刻：不引入平滑曲面预览 |
| WP-15.06 | 分层材料相对锚点与地表跟随关系 | A | 复刻 |
| WP-15.07 | 图层配置中的局部样板预览 | A | 优化：预览覆盖边缘和越界情况 |

#### WP-16 自定义植物与地下矿袋

源码：[PlantLayerEditor.java][wp-src-plantlayereditor-java]、[Plants.java][wp-src-plants-java]、[UndergroundPocketsDialog.java][wp-src-undergroundpocketsdialog-java]、[UndergroundPocketsLayerExporter.java][wp-src-undergroundpocketslayerexporter-java]。

| 子功能 ID | 功能与源码行为 | 状态 | Worldbench 处理 |
| --- | --- | --- | --- |
| WP-16.01 | 植物/花卉、树苗、农作物、蘑菇、仙人掌、甘蔗等目录 | C | 复刻：按版本维护植物目录 |
| WP-16.02 | 水生/漂浮植物、垂滴叶、下界和末地植物 | C | 复刻 |
| WP-16.03 | 干燥/水中悬挂植物，用于天花板和洞顶 | C | 复刻：验证方向与水状态 |
| WP-16.04 | 各植物出现权重、归一化比例与生长阶段/尺寸范围 | A | 复刻 |
| WP-16.05 | 只在合法基底种植、为农作物翻耕土壤 | C | 复刻：生成规则与方块有效性检查 |
| WP-16.06 | 不兼容当前地图格式的植物禁用/提示、重置选择 | C | 复刻 |
| WP-16.07 | Underground Pockets 选择地形或混合材料生成地下团簇 | A | 复刻 |
| WP-16.08 | 地下团簇出现频率、尺度、最低与最高高度 | A | 复刻 |

#### WP-17 组合图层、标注与画料显示

源码：[CombinedLayer.java][wp-src-combinedlayer-java]、[CombinedLayerEditor.java][wp-src-combinedlayereditor-java]、[CustomAnnotationLayerDialog.java][wp-src-customannotationlayerdialog-java]、[EditPaintDialog.java][wp-src-editpaintdialog-java]、[PaintPicker.java][wp-src-paintpicker-java]。

| 子功能 ID | 功能与源码行为 | 状态 | Worldbench 处理 |
| --- | --- | --- | --- |
| WP-17.01 | 组合多个组件图层，设置各层强度因子 | A | 优化：明确层依赖和生成顺序 |
| WP-17.02 | 组合层同时指定地形和生态群系 | A | 复刻 |
| WP-17.03 | 地形/生态群系在绘制时或导出时应用的选项 | A | 复刻：明确两种时机的结果差异 |
| WP-17.04 | 组件层隐藏、组件增删及关联资源导入 | A | 复刻 |
| WP-17.05 | 组合层上下文 Apply，将组合结果应用到维度 | A | 优化：保留显式展开与可回退工作流 |
| WP-17.06 | 内置彩色标注绘制、擦除、显示和颜色过滤 | A | 复刻 |
| WP-17.07 | 内置标注可选择导出为对应颜色羊毛 | A | 复刻：默认创作注释与实体生成明确区分 |
| WP-17.08 | 自定义标注层：名称、显示 paint，不参与正常地形导出 | A | 复刻 |
| WP-17.09 | 图层 paint 的纯色、图案、透明度与图案绘制/擦除 | A | 适配：图层图标和体素叠加显示 |

#### WP-18 对象、建筑与 schematic 工作流

源码：[DefaultCustomObjectProvider.java][wp-src-defaultcustomobjectprovider-java]、[Bo2LayerEditor.java][wp-src-bo2layereditor-java]、[EditObjectAttributes.java][wp-src-editobjectattributes-java]、[OffsetEditor.java][wp-src-offseteditor-java]、[Schem.java][wp-src-schem-java]、[Bo2LayerExporter.java][wp-src-bo2layerexporter-java]。

| 子功能 ID | 功能与源码行为 | 状态 | Worldbench 处理 |
| --- | --- | --- | --- |
| WP-18.01 | 导入 MCEdit `.schematic`、Sponge `.schem`、Minecraft 结构 `.nbt`、`.bo2`、`.bo3` | C | 复刻：每种格式单独建立兼容样本 |
| WP-18.02 | `.schem` 版本 1/2/3 解析分支，方块状态及适用实体数据 | C | 适配：不能把文件可读当成所有实体完全无损 |
| WP-18.03 | BO2/BO3 的受限属性支持；不是完整 TerrainControl 分支/对象组生成器 | C | 复刻：支持与忽略字段需明示 |
| WP-18.04 | 对象层包含多个对象，新增、移除、重载、属性编辑 | A | 优化：资源库和变更检测 |
| WP-18.05 | 对象相对出现频率、图层密度、网格布局及位置扰动 | A | 复刻：可重复的实例分布 |
| WP-18.06 | X/Y/Z 偏移、自动基底/归零基点、预览对象边界 | A | 优化：3D Gizmo 与数值编辑 |
| WP-18.07 | 随机旋转、镜像 | A | 复刻：方块朝向与方块实体同步变换 |
| WP-18.08 | 固定/相对地形高度、垂直随机偏移 | A | 复刻 |
| WP-18.09 | 在陆地、空中、水、岩浆、流体下生成的条件 | A | 复刻 |
| WP-18.10 | 碰撞/覆盖策略、地下替换与指定方块转空气 | A | 优化：覆盖差异可预览 |
| WP-18.11 | 地基延伸到地面、与地表碰撞/依附规则 | A | 复刻 |
| WP-18.12 | 树叶腐烂属性逐对象及批量设置 | C | 复刻 |
| WP-18.13 | 可含水方块与 waterlogged 放置处理 | C | 复刻：随版本能力校验 |
| WP-18.14 | 跨 Tile/Chunk/Region 的对象放置与后续修正 | I | 优化：邻区依赖、确定性与重复放置检查 |
| WP-18.15 | 对象可包含空气/流体并位于地下，用于人工洞穴等用途 | C | 复刻：不等于自动洞穴语义识别 |

WorldPainter 内置格式提供者未列出 `.litematic`；如以后加入，应登记为增量。它的对象层主要是按绘制区域生成/散布；Worldbench 的逐实例直接选取、移动、旋转和持久化编辑归 WB-04，不能冒称旧项目已经有相同交互。

#### WP-19 自定义洞穴/隧道与可编辑洞底洞顶

源码：[TunnelLayerDialog.java][wp-src-tunnellayerdialog-java]、[TunnelLayer.java][wp-src-tunnellayer-java]、[TunnelLayerExporter.java][wp-src-tunnellayerexporter-java]、[FloorDimensionSettingsDialog.java][wp-src-floordimensionsettingsdialog-java]。

| 子功能 ID | 功能与源码行为 | 状态 | Worldbench 处理 |
| --- | --- | --- | --- |
| WP-19.01 | 绘制自定义洞穴层控制挖掘范围 | A | 优化：地下体素直接预览/编辑 |
| WP-19.02 | 洞底和洞顶分别采用固定高度、距地表固定深度、地表反相模式 | A | 复刻 |
| WP-19.03 | 洞顶可采用距洞底固定高度 | A | 复刻 |
| WP-19.04 | 相对模式的绝对最低/最高高度限制 | A | 复刻 |
| WP-19.05 | 洞底/洞顶的独立噪声变化 | A | 复刻 |
| WP-19.06 | 洞底、洞壁、洞顶独立材料 | A | 复刻 |
| WP-19.07 | 上下边缘宽度和洞穴截面控制、截面预览 | A | 优化：三维截面诊断 |
| WP-19.08 | 洞内注水/岩浆、液面与移除原流体 | A | 复刻 |
| WP-19.09 | 洞穴生态群系 | C | 复刻 |
| WP-19.10 | 挖空填充 air、cave air、光源方块及强度、自定义材料 | C | 复刻：受目标方块能力限制 |
| WP-19.11 | 以 custom dimension 独立编辑洞底地形、材料、流体和生物群系 | A | 优化：直接地下观察，保留关联编辑能力 |
| WP-19.12 | 创建洞底维度时设置默认高度、变化、地形、生态群系和水位 | A | 复刻 |
| WP-19.13 | `roofDimensionId` 字段存在，但未发现自定义洞顶维度的正常创建/编辑入口 | D | 参考：不能与已接入的 custom floor dimension 混淆；任意洞顶 3D 编辑由 WB-01/05 扩展 |
| WP-19.14 | 洞底附加图层：创建、复制已有、编辑、移除 | A | 复刻 |
| WP-19.15 | 洞顶附加图层及倒置对象/植物 | C | 优化：保留方向语义；源码提示顶层对象会倒置 |
| WP-19.16 | 洞穴与地表相交形成开口、地下生成及跨区处理 | C | 优化：显式显示出口，洞口连接正确性单独验收 |

#### WP-20 浮空维度与多层地形

源码：[CustomLayerController.java:492][wp-src-customlayercontroller-java-l492]、[FloatingLayerDialog.java][wp-src-floatinglayerdialog-java]、[FloatingLayerExporter.java][wp-src-floatinglayerexporter-java]。

| 子功能 ID | 功能与源码行为 | 状态 | Worldbench 处理 |
| --- | --- | --- | --- |
| WP-20.01 | `[ALPHA]` 创建浮空维度并用图层确定作用范围 | E | 优化：纳入长期功能，不将 Alpha 当成熟实现 |
| WP-20.02 | 独立编辑浮空上表面维度及其属性 | E | 优化：三维多层选取与遮挡处理 |
| WP-20.03 | 创建时配置地形高度/丘陵/尺度、材料、相对水位、岩浆和海滩 | E | 复刻：区分创建默认与已建维度编辑 |
| WP-20.04 | 底面固定高度或距上表面固定深度、最小最大高度与变化 | E | 复刻 |
| WP-20.05 | 底面材料、边缘垂直/线性/平滑/圆角与边缘宽度 | E | 复刻：最终方块化 |
| WP-20.06 | 地表/地下生态群系分别应用 | E | 复刻 |
| WP-20.07 | 底部附加自定义图层及倒置对象 | E | 优化：明确朝向及与下方世界的冲突 |
| WP-20.08 | 浮空层与普通维度、洞穴和其他层的导出组合 | E | 优化：重叠、遮挡、穿插与多层空间验收 |

#### WP-21 高度图和蒙版导入

源码：[ImportHeightMapDialog.java][wp-src-importheightmapdialog-java]、[HeightMapImporter.java][wp-src-heightmapimporter-java]、[ImportMaskDialog.java][wp-src-importmaskdialog-java]、[MaskImporter.java][wp-src-maskimporter-java]。

| 子功能 ID | 功能与源码行为 | 状态 | Worldbench 处理 |
| --- | --- | --- | --- |
| WP-21.01 | 从高度图创建新世界或导入当前维度 | A | 优化：保留直接写入，同时新增参数化实例 |
| WP-21.02 | 读取尺寸、位深、图像最小/最大值及预览 | C | 优化：超大图分块解码/缓存；具体图片解码器能力另验 |
| WP-21.03 | 反转高度、水平缩放、X/Z 偏移 | A | 复刻 |
| WP-21.04 | 图像高低值映射到世界高度、映射预设、水位 | A | 复刻：显示裁剪与精度损失 |
| WP-21.05 | 设置 Void 阈值及对应空白处理 | A | 复刻 |
| WP-21.06 | 保持原地形、统一地形材料或根据主题生成 | A | 复刻 |
| WP-21.07 | 在缺失位置创建 Tile、仅在导入高度更高处生效 | A | 复刻 |
| WP-21.08 | 导入时平台、高度边界、Master 模式及默认设置 | C | 适配：版本约束与坐标转换 |
| WP-21.09 | 图片蒙版导入为地形或图层，设置目标和映射 | A | 复刻 |
| WP-21.10 | 蒙版缩放/偏移、阈值/值映射、清除已有目标层数据 | C | 复刻：离散层和连续层分别说明 |
| WP-21.11 | 导入范围预览、超界/数据不适用情况提示 | A | 优化：估计受影响区及可撤销批处理 |

#### WP-22 Minecraft 存档导入

源码：[MapImportDialog.java][wp-src-mapimportdialog-java]、[JavaMapImporter.java][wp-src-javamapimporter-java]、[ImportWarningsDialog.java][wp-src-importwarningsdialog-java]、[MapImporterProvider.java][wp-src-mapimporterprovider-java]。

| 子功能 ID | 功能与源码行为 | 状态 | Worldbench 处理 |
| --- | --- | --- | --- |
| WP-22.01 | 选择现有地图/level.dat 并识别地图格式 | C | 复刻 |
| WP-22.02 | 选择主世界、下界、末地导入 | C | 复刻 |
| WP-22.03 | 从区块提取可编辑地形高度、材料、流体和生态群系信息 | C | 适配：WorldPainter 导入不是任意完整体素存档无损建模 |
| WP-22.04 | 标记所有/不标记/含人工结构/地表人工结构区块为 Read Only | A | 优化：分类规则与误判可检查 |
| WP-22.05 | 保存原存档关联、缺失区块信息，为后续 Merge 服务 | A | 适配：原始数据保护与来源追踪 |
| WP-22.06 | 导入进度、统计及不支持/跳过内容警告 | A | 优化：区域可定位的报告 |
| WP-22.07 | 平台提供者控制导入能力，不能按“可导出”推断“可导入” | C | 适配：读写能力分别声明 |
| WP-22.08 | 导入后的再导出/合并差异告知；原建筑等不能由高度场自动重建 | C | 优化：新体素数据模型的保真范围单独定义 |

#### WP-23 版本适配、世界元数据与导出设置

源码：[DefaultPlugin.java][wp-src-defaultplugin-java]、[Platform.java][wp-src-platform-java]、[ExportWorldDialog.java][wp-src-exportworlddialog-java]、[DimensionPropertiesEditor.java][wp-src-dimensionpropertieseditor-java]、[EditSuperflatPresetDialog.java][wp-src-editsuperflatpresetdialog-java]。

| 子功能 ID | 功能与源码行为 | 状态 | Worldbench 处理 |
| --- | --- | --- | --- |
| WP-23.01 | 可选择目标平台/地图版本族；目标版本能力驱动设置 | C | 适配：1.20.1 首测，后续按版本适配器扩展 |
| WP-23.02 | 注册族：1.1 MCRegion、1.2–1.12 Anvil、1.15–1.16、1.17、1.18 | C | 适配：长期兼容目录，先后待排期 |
| WP-23.03 | 注册族：1.19–1.20.4、1.20.5–1.21.10、1.21.11、26.1 or later | C | 适配：这是源码标签，不能据此保证所有未来版本兼容 |
| WP-23.04 | 数字/命名方块、2D/3D/命名群系、人口生成等平台能力门控 | C | 复刻：统一能力模型 |
| WP-23.05 | 最低/最高建造范围、维度高度与目标平台校验 | C | 优化：原版/数据包/模组配置分档 |
| WP-23.06 | 地图名、导出目录、游戏模式、难度、作弊 | C | 复刻 |
| WP-23.07 | 游戏结构生成、奖励箱/goodies chest 等选项 | C | 复刻 |
| WP-23.08 | Minecraft 种子与维度生成器/世界类型设置 | C | 复刻 |
| WP-23.09 | Superflat 有序材料层/厚度、群系、结构、湖泊等预设编辑 | C | 复刻 |
| WP-23.10 | 自定义生成器名称/配置与格式差异 | C | 适配：不得把文本填入当成有效运行验证 |
| WP-23.11 | 数据包添加/移除、随存档导出、缺失/不可读警告 | C | 复刻 |
| WP-23.12 | 游戏世界边界中心和尺寸 | C | 复刻：和生成地形的边界/墙区别 |
| WP-23.13 | 图层导出启停、导出 pass 和顺序；维度属性汇总 | C | 复刻 |
| WP-23.14 | 不支持 Populate/生成器、出生点在选区外、禁用层等导出前告警 | C | 优化：尽量提供可定位、可修复诊断 |

以上 9 个平台注册项来自固定源码，不表示 Worldbench 已决定首发全部支持，也不填补源码没有明确列出的版本空档。兼容目标的先后属于路线图；不能把只支持 1.20.1 的阶段成果称为完整多版本对齐。

#### WP-24 烘焙、测试导出、后处理及图片输出

源码：[AbstractWorldExporter.java][wp-src-abstractworldexporter-java]、[JavaWorldExporter.java][wp-src-javaworldexporter-java]、[JavaExportSettingsEditor.java][wp-src-javaexportsettingseditor-java]、[ExportProgressDialog.java][wp-src-exportprogressdialog-java]、[TestExportDialog.java][wp-src-testexportdialog-java]、[ExportTileSelectionDialog.java][wp-src-exporttileselectiondialog-java]、[HeightMapExporter.java][wp-src-heightmapexporter-java]。

| 子功能 ID | 功能与源码行为 | 状态 | Worldbench 处理 |
| --- | --- | --- | --- |
| WP-24.01 | 多维度、Master/Detail、ceiling、图层的完整生成与组合 | C | 优化：对固定工程快照烘焙 |
| WP-24.02 | Region 并发生成、分阶段图层导出和跨区域 fixup | I | 优化：预算约束、确定性、边界一致性 |
| WP-24.03 | Chunk、Region、level.dat 及目标格式要求的数据写出 | C | 适配：实体、方块实体、光照等按版本验证 |
| WP-24.04 | 浮空水/岩浆保持或下落、无边界流体开始流动 | C | 复刻 |
| WP-24.05 | 砂砾/混凝土粉末保持、石头支撑或下落 | C | 复刻 |
| WP-24.06 | 沙保持、砂岩支撑或下落 | C | 复刻 |
| WP-24.07 | 叶距计算、移除悬空树叶、全部持久树叶选项 | C | 复刻 |
| WP-24.08 | 天光与方块光照计算 | C | 复刻：预览与目标格式光照分别验证 |
| WP-24.09 | 清理非法/不完整植物 | C | 复刻 |
| WP-24.10 | Test Export 选择维度、Tile 子集，调整选区出生点 | A | 复刻：代表性小区块测试流程 |
| WP-24.11 | 测试导出跳过地下、洞穴、资源、光照、树叶腐烂计算 | C | 复刻：清晰标记非最终质量 |
| WP-24.12 | 进度、取消、耗时、面积、输出大小/速度与阶段统计 | A | 优化：可恢复任务；源码取消后提示部分存档可能损坏 |
| WP-24.13 | 覆盖既有地图前备份，磁盘空间检查与备份清理 | A | 优化：临时目录、校验后发布、失败可回退 |
| WP-24.14 | 导出当前二维地图图像 | A | 优化：分块/分区大图输出 |
| WP-24.15 | 导出整数高度图：低精度 1:1 与高精度 1:256 | C | 复刻：位深/图片格式随范围和编码器确定 |
| WP-24.16 | 预览图片输出与高度图输出分开，记录原点、尺度和高度映射 | A | 优化：后半项元数据为 Worldbench 改进，不假定 WP 已完整保存 |

#### WP-25 合并到已有存档

源码：[MergeWorldDialog.java][wp-src-mergeworlddialog-java]、[JavaWorldMerger.java][wp-src-javaworldmerger-java]、[ReadOnly.java][wp-src-readonly-java]。

| 子功能 ID | 功能与源码行为 | 状态 | Worldbench 处理 |
| --- | --- | --- | --- |
| WP-25.01 | 选择目标现有地图和参与维度/Tile 范围 | C | 复刻 |
| WP-25.02 | 合并生成的地形与原区块 | C | 优化：合并预览与差异报告 |
| WP-25.03 | 完整替换区块 | C | 复刻：破坏范围与备份明确 |
| WP-25.04 | 仅替换生态群系 | C | 复刻 |
| WP-25.05 | 地表替换厚度，地表/地下合并策略 | C | 复刻 |
| WP-25.06 | 清理树木、巨型蘑菇、植被、农作物 | C | 复刻：保留分类误判告警 |
| WP-25.07 | 清理地表人工结构与地下人工结构 | C | 优化：木材等启发式分类需可检查 |
| WP-25.08 | 清理原地下资源和洞穴等内容 | C | 复刻 |
| WP-25.09 | Read Only 区块保护，未导入/未选中区域处理 | C | 复刻：只读保护不能被后台写入绕过 |
| WP-25.10 | 合并前兼容性检查、备份、进度、警告与失败处理 | C | 优化：恢复与重跑不污染目标 |
| WP-25.11 | 处理关联实体/方块实体与区块边界 | C | 适配：逐版本验证，不能承诺任意模组无损 |

GUI 合并功能实际存在；同名脚本 `mergeWorld()` 不代表这一能力已在脚本实现，见 WP-30.03。

#### WP-26 全图变换与高度迁移

源码：[ChangeHeightDialog.java][wp-src-changeheightdialog-java]、[RotateWorldDialog.java][wp-src-rotateworlddialog-java]、[ShiftWorldDialog.java][wp-src-shiftworlddialog-java]、[ScaleWorldDialog.java][wp-src-scaleworlddialog-java]。

| 子功能 ID | 功能与源码行为 | 状态 | Worldbench 处理 |
| --- | --- | --- | --- |
| WP-26.01 | 改地图格式、最低/最高高度边界 | C | 适配：目标能力变化与内容损失报告 |
| WP-26.02 | 垂直缩放后垂直平移 | A | 优化：操作预览、非破坏变换或可恢复事务 |
| WP-26.03 | 同步主题/图层高度相关参数，裁剪提示 | C | 复刻 |
| WP-26.04 | 全图旋转 90°/180°/270° | A | 复刻 |
| WP-26.05 | 地图水平平移，源码按 Tile 步长移动 | A | 优化：新数据模型下精确方块偏移另设计 |
| WP-26.06 | 水平整体缩放，高度/连续层插值与离散数据重采样 | A | 复刻：高度/连续值与分类值采用不同规则 |
| WP-26.07 | 相关 ceiling、洞穴等辅助维度一同变换 | C | 复刻：所有坐标引用保持一致 |
| WP-26.08 | 变换耗时、确认、进度及无法撤销/丢失内容警告 | A | 优化：大地图可取消、可恢复，不照搬不可撤销缺点 |

#### WP-27 脚本、批处理与命令行

源码：[ScriptingContext.java][wp-src-scriptingcontext-java]、[ScriptRunner.java][wp-src-scriptrunner-java]、[MappingOp.java][wp-src-mappingop-java]、[CreateFilterOp.java][wp-src-createfilterop-java]、[ImportHeightMapOp.java][wp-src-importheightmapop-java]、[ExportWorldOp.java][wp-src-exportworldop-java]。

| 子功能 ID | 功能与源码行为 | 状态 | Worldbench 处理 |
| --- | --- | --- | --- |
| WP-27.01 | GUI 运行脚本，脚本文件、参数、输出与中断 | C | 适配：语言/执行环境另选，不强制 JSR223 |
| WP-27.02 | `wpscript` 命令行运行与无交互批量工作流 | C | 适配：可重现参数、退出码、日志和取消 |
| WP-27.03 | `wp` builder API，参数链与 `go()` 执行 | I | 适配：保留脚本能力，不强制同名语法 |
| WP-27.04 | 加载/保存工程、读取图层/材料文件或世界内资源 | C | 复刻 |
| WP-27.05 | 读取高度图及 RGB 通道 | C | 复刻 |
| WP-27.06 | 高度图建图，缩放/偏移、值域映射、水位、平台和建造范围 | C | 复刻 |
| WP-27.07 | 把高度图、图层或地形映射到世界/指定维度或 ceiling | C | 复刻 |
| WP-27.08 | 映射输入数值/颜色/Alpha 到层值或地形；总是覆盖/仅更高/仅更低 | C | 复刻 |
| WP-27.09 | 脚本过滤器：高度/羽化、图层值比较、地形、群系、自动群系、水陆、坡度、选区 | C | 复刻：源码部分条件只允许一个选择，不能套用 GUI 多选能力 |
| WP-27.10 | 安装自定义地形、查询地图格式和群系 ID | C | 适配：不继承固定槽位限制 |
| WP-27.11 | 脚本导出世界、指定目录、使用已有导出配置和备份 | C | 复刻：无头导出与 GUI 导出结果比对 |
| WP-27.12 | 查询版本、检测中断并合作式停止 | I | 优化：任务取消与失败隔离 |
| WP-27.13 | 通过公开 Java 对象/API 深度操作世界模型 | I | 适配：稳定扩展 API、版本化兼容策略 |

#### WP-28 插件与维护工具

源码：[WPPluginManager.java][wp-src-wppluginmanager-java]、[LayerProvider.java][wp-src-layerprovider-java]、[CustomLayerProvider.java][wp-src-customlayerprovider-java]、[OperationProvider.java][wp-src-operationprovider-java]、[PlatformProvider.java][wp-src-platformprovider-java]、[CustomObjectProvider.java][wp-src-customobjectprovider-java]、[RespawnPlayerDialog.java][wp-src-respawnplayerdialog-java]。

| 子功能 ID | 功能与源码行为 | 状态 | Worldbench 处理 |
| --- | --- | --- | --- |
| WP-28.01 | 插件发现/加载、签名/版本参数、安装授权 UUID 与重名/初始化错误检查 | C | 适配：新插件协议与信任机制另设计，不强制复制授权 UUID 限制 |
| WP-28.02 | 插件添加工具、图层及其编辑器 | I | 复刻：扩展能力不要求兼容旧 Java 二进制插件 |
| WP-28.03 | 插件提供自定义对象格式 | I | 复刻 |
| WP-28.04 | 插件提供平台/格式、地图导入与导出 | I | 复刻：非 Minecraft 格式插件不是内置支持承诺 |
| WP-28.05 | 插件自定义图层出现在创建菜单，角色能力过滤 | C | 复刻 |
| WP-28.06 | 打开插件、自定义笔刷、自定义方块材料目录 | A | 适配：资源管理与诊断入口 |
| WP-28.07 | Respawn Player 修复存档中卡住/位置异常的玩家 | C | 适配：版本化玩家数据与修改前备份 |
| WP-28.08 | 独立 Import/Export、RecoverWorld、IntegrityChecker、DamagedWorldFinder 等维护入口 | I | 适配：归并为受支持 CLI/诊断能力；每项成熟度另验证 |
| WP-28.09 | NBT/区块/光照/实体/Region/对象转储、比较、统计和分析工具 | I | 参考：开发期提供等效诊断，不将所有脚本变成产品菜单 |
| WP-28.10 | 方块/群系数据采集、旧数据转换、修复和性能研究脚本 | I | 参考：作为格式维护成本与测试资产来源 |

#### WP-29 偏好设置、恢复、帮助与发行体验

源码：[PreferencesDialog.java][wp-src-preferencesdialog-java]、[Main.java][wp-src-main-java]、[ErrorDialog.java][wp-src-errordialog-java]、[AboutDialog.java][wp-src-aboutdialog-java]、[App.java:4393][wp-src-app-java-l4393]。

| 子功能 ID | 功能与源码行为 | 状态 | Worldbench 处理 |
| --- | --- | --- | --- |
| WP-29.01 | 默认新世界尺寸/地形/主题/地图格式/导出设置 | A | 复刻 |
| WP-29.02 | 默认网格、等高线、视图与笔刷参数 | A | 复刻 |
| WP-29.03 | UI 主题、缩放、显示布局和平台差异 | C | 适配：新 UI 的高 DPI/主题支持 |
| WP-29.04 | 自动保存间隔/空闲保护、备份数量、撤销深度 | A | 优化：统一可靠性设置 |
| WP-29.05 | 多实例文件锁与自动保存冲突保护 | C | 优化：工程级写入锁与恢复流程 |
| WP-29.06 | 最低剩余磁盘空间、旧导出备份清理 | A | 优化：大规模烘焙容量估计 |
| WP-29.07 | 导出线程限制、图像覆盖缓存策略、绘图加速后端 | C | 适配：改为 CPU/GPU/内存/缓存预算，不复制过时 Java 后端选择 |
| WP-29.08 | 安全模式：禁插件/自定义笔刷/部分背景及显示设置回退 | C | 复刻：面向诊断，不是完整工作模式 |
| WP-29.09 | 错误报告、详细信息复制、启动警告与恢复通知 | A | 优化：日志可定位到任务/资源/区块 |
| WP-29.10 | 更新检查、使用数据选择设置 | A | 适配：Worldbench 自己的发布与隐私策略待决策 |
| WP-29.11 | 文档、FAQ、故障排查、上下文 F1 帮助、关于/版本/致谢 | A | 适配：Worldbench 自己的文档与依赖声明 |
| WP-29.12 | 捐赠和周边商品入口/提醒 | A | 参考：记录源功能，不复制作者品牌、收款或推广；Worldbench 是否需要另定 |
| WP-29.13 | 简化界面模式及高级入口隐藏规则 | C | 适配：入门/高级呈现可重设计，但不隐藏实际缺失能力 |

#### WP-30 未接入、占位与实验代码边界

源码：[App.java:3119][wp-src-app-java-l3119]、[DefaultPlugin.java:38][wp-src-defaultplugin-java-l38]、[MergeWorldOp.java][wp-src-mergeworldop-java]、[ExportPartDialog.java][wp-src-exportpartdialog-java]、[HeightMapEditor.java][wp-src-heightmapeditor-java]、[HeightMapExporter.java][wp-src-heightmapexporter-java]、[CliffCarverExporter.java][wp-src-cliffcarverexporter-java]。

| 子功能 ID | 观察与边界 | 状态 | Worldbench 处理 |
| --- | --- | --- | --- |
| WP-30.01 | `Erode` 类存在，工具注册被注释 | D | 参考：侵蚀已由用户独立要求，纳入 WB-02；不能宣称在复刻正式 WP 侵蚀按钮 |
| WP-30.02 | `RiverPaint`、`CreateMountain` 有实现类但未发现正常工具注册；`River` 内置层注册被注释 | D | 参考：不把它们当成熟河流/河网功能；正式 Mountain 已在 WP-07 |
| WP-30.03 | `mergeWorld()` 返回操作对象，但 `MergeWorldOp.go()` 仍有 TODO，没有执行地图合并 | D | 参考：Worldbench 若实现脚本合并，属于补全接口 |
| WP-30.04 | `ExportPartDialog` 有表单/类，未发现正常实例化入口 | D | 参考：局部导出以已接入的 Test Export/Tile 选择为准 |
| WP-30.05 | `HeightMapEditor` 是独立 main 工具，非主编辑器内正式程序化图层 UI | I | 参考：不能据此认为 WP 已有 WB-02 的完整实例工作流 |
| WP-30.06 | 高度图组合 API：加/减/乘、最小/最大、位移、变换、噪声、坡度、阶梯及双三次采样等 | I | 参考：可评估算法复用，需独立设计依赖图/缓存/交互 |
| WP-30.07 | `CliffCarver`/exporter 中为简单演示算法，未发现默认自定义层菜单注册 | D | 参考：不是正式悬崖工具保证 |
| WP-30.08 | GardenOfEden/种子等程序化实验类及历史挂点 | D | 参考：不能推定有可用道路、城市或生态模拟工具 |
| WP-30.09 | `FancyTheme` 及 fancy 主题辅助表单未形成当前默认主题编辑工作流 | D | 参考：正式对齐按 SimpleTheme 与其高级配置执行 |
| WP-30.10 | HeightMapExporter 支持浮点原值/归一化枚举，但主菜单只接入整数两种导出 | I | 参考：如加入浮点菜单要作为明确扩展验收 |
| WP-30.11 | “把已有 Minecraft 地图导入当前工程”的菜单代码被注释 | D | 参考：已接入的是导入为新工程；多存档拼接需另设计 |
| WP-30.12 | 世界边界伤害/缓冲/警告参数 UI 代码被注释，当前入口主要为中心/尺寸 | D | 参考：不能从数据字段推断所有设置可编辑 |
| WP-30.13 | 帮助选择器菜单等注释掉的入口，不等于已发布 UI | D | 参考：保留已接入的 F1/在线帮助能力 |

### 5.8 Worldbench 的增量与优化目标

下表区分用户确认的方向和建议的实现细节；不将其混入 WorldPainter 既有功能计数。

| ID（父节点 WB） | 功能 | 要求/候选子项 | 与 WP 的关系 |
| --- | --- | --- | --- |
| WB-01 | 始终体素的 3D 主编辑器【已确认】 | 近景方块精度；远景体素 LOD；自由观察和直接编辑；可参考 Distant Horizons。拾取、剖切、地下隔离、透明方块及非整立方方块显示规则待设计 | 重建 WP-04/05/06 的交互及渲染基础 |
| WB-02 | 非破坏高度图地貌实例【已确认】 | 可重调位置、混合、侵蚀、噪波；候选旋转/缩放/蒙版/修改器顺序/种子/缓存；侵蚀可后台精算 | 超出一次性高度图导入、笔刷和内部 HeightMap 类 |
| WB-03 | 节点路网【已确认】 | 地表节点、指定连接自动生成；道路截面、坡度约束、交叉口、桥隧范围待规划 | Pencil 直线或未接入 River/Garden 代码都不是此功能 |
| WB-04 | 可直接操纵的建筑/结构实例【已确认方向】 | schematic 导入与快捷放置；候选实例选择、Gizmo、吸附、覆盖预览、跨块变换和共享资产 | 保留 WP 对象分布并新增逐实例编辑 |
| WB-05 | 洞穴资产语义识别【已确认方向】 | 自动区分内部/外部/出口并填入地下；人工校正、入口标记、体素密封与连接验证为建议 | WP 含空气对象和 Tunnel 层不足以完成自动识别 |
| WB-06 | 极大地图与完整烘焙【已确认】 | 8K/16K 常见规模；128K 极端；预算内流式显示/编辑；最终全质量求值；断点、确定性和恢复机制为建议 | 需要超越整世界驻内存式假设，不只是增加显示距离 |
| WB-07 | 多版本与超高实验配置【已确认】 | 首测 1.20.1；后续版本选择；几千格高；原版/数据包/模组边界分别验证 | 对齐 WP 平台选择并扩展独立工程高度能力 |
| WB-08 | 全功能 parity 与更可靠操作【已确认范围，方案建议】 | A/C/E 全量纳入长期计划；大操作、导出/合并、迁移、资产依赖均可诊断；每项绑定样本与验收 | “先做核心”不得变成永久删除高级功能 |

“体素预览”不意味着只渲染完整立方体：台阶、楼梯、植物、流体和透明方块需要明确精确预览规则；可分阶段实现，但不能宣称全部方块保真。高度场、SDF、距离场或平滑数学函数仍可作为内部数据/计算表示，最后离散为目标方块；不提供平滑曲面地形预览模式。

### 5.9 覆盖核对与遗留验证

本轮目录包含 **30 个一级功能域、334 个子条目**，其中 A=174、C=126、E=8、D=11、I=15；另列 8 项 Worldbench 增量/优化方向。计数是本轮条目划分结果，不是 WorldPainter 官方功能数量。

已从下列入口向实现回溯，表单仅用于检查参数覆盖，不能单凭表单存在判定可用。表单覆盖统计为 WPGUI 69 个、WPCore 1 个，总计 70 个；重复类名按目录区分。

| 审计面 | 对应功能域 | 证据范围与注意事项 |
| --- | --- | --- |
| File / Edit / View / Tools / Help | WP-01–05、21–29 | App 菜单及 action；包含高级/简化模式、条件禁用和被注释入口 |
| 工具注册与动态 options | WP-06–09 | painting/terrain/selection 注册、反向操作、数位笔和工具内选项 |
| 默认图层/平台注册 | WP-12/14/23 | DefaultPlugin；额外核对 App 中 Populate、Read Only、Biome、Annotations、流体入口 |
| 自定义层创建/上下文菜单 | WP-13、15–20 | CustomLayerController；含插件菜单、Find、Apply、copy、export、编辑子维度 |
| 对象格式提供者与解析器 | WP-18 | 扩展名、Schem 版本分支、BO2/BO3 限制、对象导出属性 |
| 导入/合并/导出实现 | WP-21–26 | JavaMapImporter、JavaWorldMerger、平台设置、生成阶段与后处理 |
| 预览与内部 API | WP-05/27/28/30 | ThreeDee、Dynmap、脚本、插件、独立工具及占位检查 |

70 个表单的归属账如下。名称省略 `.form`；带 `/` 的短目录用于区分同名表单，不是新建文档或新模块。

| 表单组 | 表单名称 | 功能归属 |
| --- | --- | --- |
| 创建与范围（4） | NewWorldDialog、TileEditor、TileSelector、ExportTileSelectionDialog | WP-02/24 |
| 维度与全图变换（6） | DimensionPropertiesDialog、DimensionPropertiesEditor、ChangeHeightDialog、RotateWorldDialog、ShiftWorldDialog、ScaleWorldDialog | WP-03/14/23/26 |
| 视图与信息（5） | ConfigureViewDialog、ConfigureOverlayDialog、GlassPane、threedeeview/GlassPane、panels/InfoPanel | WP-04/05 |
| 画料与噪波（6） | CustomMaterialDialog、MaterialSelector、MixedMaterialChooser、ColourEditor、NoiseSettingsDialog、NoiseSettingsEditor | WP-10/11/17 |
| 导入/导出/合并（8） | ImportHeightMapDialog、importing/ImportMaskDialog、importing/MapImportDialog、ImportWarningsDialog、ExportWorldDialog、TestExportDialog、MergeWorldDialog、platforms/JavaExportSettingsEditor（WPCore） | WP-21–25 |
| 选区与工具（4） | FillDialog、panels/BrushOptions、operations/TextDialog、selection/CopySelectionOperationOptionsPanel | WP-06–09 |
| 图层管理/群系/历史（6） | DeleteLayersDialog、layers/EditLayerDialog、palettes/EditPaletteDialog、importing/ImportCustomItemsDialog、biomeschemes/CustomBiomeDialog、history/WorldHistoryDialog | WP-01/05/12/13 |
| 自定义图层（14） | annotation/CustomAnnotationLayerDialog、bo2/Bo2LayerEditor、bo2/EditObjectAttributes、bo2/OffsetEditor、combined/AddLayerDialog、combined/CombinedLayerEditor、groundcover/GroundCoverLayerEditor、plants/PlantLayerEditor、pockets/UndergroundPocketsDialog、renderers/EditPaintDialog、renderers/PaintPicker、tunnel/FloatingLayerDialog、tunnel/FloorDimensionSettingsDialog、tunnel/TunnelLayerDialog | WP-15–20 |
| 主题与生成器（6） | themes/impl/simple/AddLayerDialog、themes/impl/simple/AddTerrainRangeDialog、themes/impl/simple/EditSimpleThemeDialog、themes/impl/simple/SimpleThemeEditor、themes/impl/fancy/AddLayerDialog、superflat/EditSuperflatPresetDialog | WP-11/23/30 |
| 脚本与修复（2） | tools/scripts/ScriptRunner、tools/RespawnPlayerDialog | WP-27/28 |
| 设置、帮助及进度（7） | PreferencesDialog、ErrorDialog、AboutDialog、DonationDialog、MerchDialog、MultiProgressDialog、browser/SimpleBrowser | WP-24/29；浏览器为帮助支撑 |
| 未接入/独立工具（2） | ExportPartDialog、tools/HeightMapEditor | WP-30 |

完整对齐的后续验收规则：

1. 每个 A/C/E 子功能至少有一个可重现样本，记录目标版本、输入、关键参数、预期结果和已知差异；不以按钮存在或类名相同验收。
2. 对编辑功能检验撤销/重做、保存/重开、跨块边界和实际烘焙结果；对仅影响显示的功能确认不污染世界数据。
3. 对导入/导出/合并分别检查方块状态、实体/方块实体、群系、光照、元数据及旧存档保护；实际进入对应 Minecraft 版本验证。
4. 对生成类功能比较分布/参数语义和结构正确性；是否要求与 WP 随机结果逐方块一致尚未确定，不能默认承诺相同 seed 必然生成相同世界。
5. 对浮空、洞穴子维度、Master/Detail、ceiling、多版本和实验高度建立专项样本；覆盖 A/C 不代表已验证 E。
6. 对 8K/16K 和 128K 分别报告工作集/耗时/缓存；视口实时与全图最终烘焙分开计量。远景 LOD 不作为导出数据。
7. 后续漏项、插件边界或源码与实际运行不符时，在本文对应父节点追加/更正条目，保留原 ID 和 Git 历史。

官方交叉资料（访问于 2026-11-19，页面可能继续更新）：[Manual](https://www.worldpainter.net/trac/wiki/Manual)、[Controls](https://www.worldpainter.net/trac/wiki/Controls)、[KeyboardShortcuts](https://www.worldpainter.net/trac/wiki/KeyboardShortcuts)、[CustomBrushes](https://www.worldpainter.net/trac/wiki/CustomBrushes)、[CustomObjects](https://www.worldpainter.net/trac/wiki/CustomObjects)、[CavesAndTunnels](https://www.worldpainter.net/trac/wiki/CavesAndTunnels)、[Populate](https://www.worldpainter.net/trac/wiki/Populate)、[Scripting/API](https://www.worldpainter.net/trac/wiki/Scripting/API)、[Plugins](https://www.worldpainter.net/trac/wiki/Plugins)、[SafeMode](https://www.worldpainter.net/trac/wiki/SafeMode)。功能证据优先使用上方固定提交链接，Wiki 的过时示例不是新实现的验收规范。



<!-- 固定提交源码链接定义；属于本文，不是独立文档。 -->

[wp-src-app-java-l928]: https://github.com/Captain-Chaos/WorldPainter/blob/8e3329aa7e7014dcda08f7ad25b8cb8fa97b4d2b/WorldPainter/WPGUI/src/main/java/org/pepsoft/worldpainter/App.java#L928
[wp-src-worldio-java]: https://github.com/Captain-Chaos/WorldPainter/blob/8e3329aa7e7014dcda08f7ad25b8cb8fa97b4d2b/WorldPainter/WPCore/src/main/java/org/pepsoft/worldpainter/WorldIO.java
[wp-src-worldhistorydialog-java]: https://github.com/Captain-Chaos/WorldPainter/blob/8e3329aa7e7014dcda08f7ad25b8cb8fa97b4d2b/WorldPainter/WPGUI/src/main/java/org/pepsoft/worldpainter/history/WorldHistoryDialog.java
[wp-src-newworlddialog-java]: https://github.com/Captain-Chaos/WorldPainter/blob/8e3329aa7e7014dcda08f7ad25b8cb8fa97b4d2b/WorldPainter/WPGUI/src/main/java/org/pepsoft/worldpainter/NewWorldDialog.java
[wp-src-tileeditor-java]: https://github.com/Captain-Chaos/WorldPainter/blob/8e3329aa7e7014dcda08f7ad25b8cb8fa97b4d2b/WorldPainter/WPGUI/src/main/java/org/pepsoft/worldpainter/TileEditor.java
[wp-src-tileselector-java]: https://github.com/Captain-Chaos/WorldPainter/blob/8e3329aa7e7014dcda08f7ad25b8cb8fa97b4d2b/WorldPainter/WPGUI/src/main/java/org/pepsoft/worldpainter/TileSelector.java
[wp-src-biomesviewerframe-java]: https://github.com/Captain-Chaos/WorldPainter/blob/8e3329aa7e7014dcda08f7ad25b8cb8fa97b4d2b/WorldPainter/WPGUI/src/main/java/org/pepsoft/worldpainter/tools/BiomesViewerFrame.java
[wp-src-app-java-l3897]: https://github.com/Captain-Chaos/WorldPainter/blob/8e3329aa7e7014dcda08f7ad25b8cb8fa97b4d2b/WorldPainter/WPGUI/src/main/java/org/pepsoft/worldpainter/App.java#L3897
[wp-src-dimension-java]: https://github.com/Captain-Chaos/WorldPainter/blob/8e3329aa7e7014dcda08f7ad25b8cb8fa97b4d2b/WorldPainter/WPCore/src/main/java/org/pepsoft/worldpainter/Dimension.java
[wp-src-dimensionpropertieseditor-java]: https://github.com/Captain-Chaos/WorldPainter/blob/8e3329aa7e7014dcda08f7ad25b8cb8fa97b4d2b/WorldPainter/WPGUI/src/main/java/org/pepsoft/worldpainter/DimensionPropertiesEditor.java
[wp-src-world2-java]: https://github.com/Captain-Chaos/WorldPainter/blob/8e3329aa7e7014dcda08f7ad25b8cb8fa97b4d2b/WorldPainter/WPCore/src/main/java/org/pepsoft/worldpainter/World2.java
[wp-src-app-java-l4115]: https://github.com/Captain-Chaos/WorldPainter/blob/8e3329aa7e7014dcda08f7ad25b8cb8fa97b4d2b/WorldPainter/WPGUI/src/main/java/org/pepsoft/worldpainter/App.java#L4115
[wp-src-configureviewdialog-java]: https://github.com/Captain-Chaos/WorldPainter/blob/8e3329aa7e7014dcda08f7ad25b8cb8fa97b4d2b/WorldPainter/WPGUI/src/main/java/org/pepsoft/worldpainter/ConfigureViewDialog.java
[wp-src-configureoverlaydialog-java]: https://github.com/Captain-Chaos/WorldPainter/blob/8e3329aa7e7014dcda08f7ad25b8cb8fa97b4d2b/WorldPainter/WPGUI/src/main/java/org/pepsoft/worldpainter/ConfigureOverlayDialog.java
[wp-src-infopanel-java]: https://github.com/Captain-Chaos/WorldPainter/blob/8e3329aa7e7014dcda08f7ad25b8cb8fa97b4d2b/WorldPainter/WPGUI/src/main/java/org/pepsoft/worldpainter/panels/InfoPanel.java
[wp-src-threedeeframe-java]: https://github.com/Captain-Chaos/WorldPainter/blob/8e3329aa7e7014dcda08f7ad25b8cb8fa97b4d2b/WorldPainter/WPGUI/src/main/java/org/pepsoft/worldpainter/threedeeview/ThreeDeeFrame.java
[wp-src-threedeeview-java]: https://github.com/Captain-Chaos/WorldPainter/blob/8e3329aa7e7014dcda08f7ad25b8cb8fa97b4d2b/WorldPainter/WPGUI/src/main/java/org/pepsoft/worldpainter/threedeeview/ThreeDeeView.java
[wp-src-tile3drenderer-java]: https://github.com/Captain-Chaos/WorldPainter/blob/8e3329aa7e7014dcda08f7ad25b8cb8fa97b4d2b/WorldPainter/WPGUI/src/main/java/org/pepsoft/worldpainter/threedeeview/Tile3DRenderer.java
[wp-src-editlayerdialog-java]: https://github.com/Captain-Chaos/WorldPainter/blob/8e3329aa7e7014dcda08f7ad25b8cb8fa97b4d2b/WorldPainter/WPGUI/src/main/java/org/pepsoft/worldpainter/layers/EditLayerDialog.java
[wp-src-dynmappreviewer-java]: https://github.com/Captain-Chaos/WorldPainter/blob/8e3329aa7e7014dcda08f7ad25b8cb8fa97b4d2b/WorldPainter/WPDynmapPreviewer/src/main/java/org/pepsoft/worldpainter/dynmap/DynmapPreviewer.java
[wp-src-app-java-l2897]: https://github.com/Captain-Chaos/WorldPainter/blob/8e3329aa7e7014dcda08f7ad25b8cb8fa97b4d2b/WorldPainter/WPGUI/src/main/java/org/pepsoft/worldpainter/App.java#L2897
[wp-src-app-java-l3593]: https://github.com/Captain-Chaos/WorldPainter/blob/8e3329aa7e7014dcda08f7ad25b8cb8fa97b4d2b/WorldPainter/WPGUI/src/main/java/org/pepsoft/worldpainter/App.java#L3593
[wp-src-mouseortabletoperation-java]: https://github.com/Captain-Chaos/WorldPainter/blob/8e3329aa7e7014dcda08f7ad25b8cb8fa97b4d2b/WorldPainter/WPGUI/src/main/java/org/pepsoft/worldpainter/operations/MouseOrTabletOperation.java
[wp-src-bitmapbrush-java]: https://github.com/Captain-Chaos/WorldPainter/blob/8e3329aa7e7014dcda08f7ad25b8cb8fa97b4d2b/WorldPainter/WPGUI/src/main/java/org/pepsoft/worldpainter/brushes/BitmapBrush.java
[wp-src-app-java-l3067]: https://github.com/Captain-Chaos/WorldPainter/blob/8e3329aa7e7014dcda08f7ad25b8cb8fa97b4d2b/WorldPainter/WPGUI/src/main/java/org/pepsoft/worldpainter/App.java#L3067
[wp-src-app-java-l3114]: https://github.com/Captain-Chaos/WorldPainter/blob/8e3329aa7e7014dcda08f7ad25b8cb8fa97b4d2b/WorldPainter/WPGUI/src/main/java/org/pepsoft/worldpainter/App.java#L3114
[wp-src-height-java]: https://github.com/Captain-Chaos/WorldPainter/blob/8e3329aa7e7014dcda08f7ad25b8cb8fa97b4d2b/WorldPainter/WPGUI/src/main/java/org/pepsoft/worldpainter/operations/Height.java
[wp-src-flatten-java]: https://github.com/Captain-Chaos/WorldPainter/blob/8e3329aa7e7014dcda08f7ad25b8cb8fa97b4d2b/WorldPainter/WPGUI/src/main/java/org/pepsoft/worldpainter/operations/Flatten.java
[wp-src-fill-java]: https://github.com/Captain-Chaos/WorldPainter/blob/8e3329aa7e7014dcda08f7ad25b8cb8fa97b4d2b/WorldPainter/WPGUI/src/main/java/org/pepsoft/worldpainter/operations/Fill.java
[wp-src-flood-java]: https://github.com/Captain-Chaos/WorldPainter/blob/8e3329aa7e7014dcda08f7ad25b8cb8fa97b4d2b/WorldPainter/WPGUI/src/main/java/org/pepsoft/worldpainter/operations/Flood.java
[wp-src-sponge-java]: https://github.com/Captain-Chaos/WorldPainter/blob/8e3329aa7e7014dcda08f7ad25b8cb8fa97b4d2b/WorldPainter/WPGUI/src/main/java/org/pepsoft/worldpainter/operations/Sponge.java
[wp-src-spraypaint-java]: https://github.com/Captain-Chaos/WorldPainter/blob/8e3329aa7e7014dcda08f7ad25b8cb8fa97b4d2b/WorldPainter/WPGUI/src/main/java/org/pepsoft/worldpainter/operations/SprayPaint.java
[wp-src-pencil-java]: https://github.com/Captain-Chaos/WorldPainter/blob/8e3329aa7e7014dcda08f7ad25b8cb8fa97b4d2b/WorldPainter/WPGUI/src/main/java/org/pepsoft/worldpainter/operations/Pencil.java
[wp-src-text-java]: https://github.com/Captain-Chaos/WorldPainter/blob/8e3329aa7e7014dcda08f7ad25b8cb8fa97b4d2b/WorldPainter/WPGUI/src/main/java/org/pepsoft/worldpainter/operations/Text.java
[wp-src-brushoptions-java]: https://github.com/Captain-Chaos/WorldPainter/blob/8e3329aa7e7014dcda08f7ad25b8cb8fa97b4d2b/WorldPainter/WPGUI/src/main/java/org/pepsoft/worldpainter/panels/BrushOptions.java
[wp-src-combinedfilter-java]: https://github.com/Captain-Chaos/WorldPainter/blob/8e3329aa7e7014dcda08f7ad25b8cb8fa97b4d2b/WorldPainter/WPCore/src/main/java/org/pepsoft/worldpainter/panels/CombinedFilter.java
[wp-src-eyedropper-java]: https://github.com/Captain-Chaos/WorldPainter/blob/8e3329aa7e7014dcda08f7ad25b8cb8fa97b4d2b/WorldPainter/WPGUI/src/main/java/org/pepsoft/worldpainter/tools/Eyedropper.java
[wp-src-editselectionoperation-java]: https://github.com/Captain-Chaos/WorldPainter/blob/8e3329aa7e7014dcda08f7ad25b8cb8fa97b4d2b/WorldPainter/WPGUI/src/main/java/org/pepsoft/worldpainter/selection/EditSelectionOperation.java
[wp-src-copyselectionoperation-java]: https://github.com/Captain-Chaos/WorldPainter/blob/8e3329aa7e7014dcda08f7ad25b8cb8fa97b4d2b/WorldPainter/WPGUI/src/main/java/org/pepsoft/worldpainter/selection/CopySelectionOperation.java
[wp-src-copyselectionoperationoptionspanel-java]: https://github.com/Captain-Chaos/WorldPainter/blob/8e3329aa7e7014dcda08f7ad25b8cb8fa97b4d2b/WorldPainter/WPGUI/src/main/java/org/pepsoft/worldpainter/selection/CopySelectionOperationOptionsPanel.java
[wp-src-filldialog-java]: https://github.com/Captain-Chaos/WorldPainter/blob/8e3329aa7e7014dcda08f7ad25b8cb8fa97b4d2b/WorldPainter/WPGUI/src/main/java/org/pepsoft/worldpainter/FillDialog.java
[wp-src-terrain-java]: https://github.com/Captain-Chaos/WorldPainter/blob/8e3329aa7e7014dcda08f7ad25b8cb8fa97b4d2b/WorldPainter/WPCore/src/main/java/org/pepsoft/worldpainter/Terrain.java
[wp-src-custommaterialdialog-java]: https://github.com/Captain-Chaos/WorldPainter/blob/8e3329aa7e7014dcda08f7ad25b8cb8fa97b4d2b/WorldPainter/WPGUI/src/main/java/org/pepsoft/worldpainter/CustomMaterialDialog.java
[wp-src-materialselector-java]: https://github.com/Captain-Chaos/WorldPainter/blob/8e3329aa7e7014dcda08f7ad25b8cb8fa97b4d2b/WorldPainter/WPGUI/src/main/java/org/pepsoft/worldpainter/MaterialSelector.java
[wp-src-mixedmaterial-java]: https://github.com/Captain-Chaos/WorldPainter/blob/8e3329aa7e7014dcda08f7ad25b8cb8fa97b4d2b/WorldPainter/WPCore/src/main/java/org/pepsoft/worldpainter/MixedMaterial.java
[wp-src-app-java-l1617]: https://github.com/Captain-Chaos/WorldPainter/blob/8e3329aa7e7014dcda08f7ad25b8cb8fa97b4d2b/WorldPainter/WPGUI/src/main/java/org/pepsoft/worldpainter/App.java#L1617
[wp-src-simplethemeeditor-java]: https://github.com/Captain-Chaos/WorldPainter/blob/8e3329aa7e7014dcda08f7ad25b8cb8fa97b4d2b/WorldPainter/WPGUI/src/main/java/org/pepsoft/worldpainter/themes/impl/simple/SimpleThemeEditor.java
[wp-src-simpletheme-java]: https://github.com/Captain-Chaos/WorldPainter/blob/8e3329aa7e7014dcda08f7ad25b8cb8fa97b4d2b/WorldPainter/WPCore/src/main/java/org/pepsoft/worldpainter/themes/SimpleTheme.java
[wp-src-noisesettingseditor-java]: https://github.com/Captain-Chaos/WorldPainter/blob/8e3329aa7e7014dcda08f7ad25b8cb8fa97b4d2b/WorldPainter/WPGUI/src/main/java/org/pepsoft/worldpainter/NoiseSettingsEditor.java
[wp-src-noisesettingsdialog-java]: https://github.com/Captain-Chaos/WorldPainter/blob/8e3329aa7e7014dcda08f7ad25b8cb8fa97b4d2b/WorldPainter/WPGUI/src/main/java/org/pepsoft/worldpainter/NoiseSettingsDialog.java
[wp-src-biomespanel-java]: https://github.com/Captain-Chaos/WorldPainter/blob/8e3329aa7e7014dcda08f7ad25b8cb8fa97b4d2b/WorldPainter/WPGUI/src/main/java/org/pepsoft/worldpainter/layers/BiomesPanel.java
[wp-src-custombiomedialog-java]: https://github.com/Captain-Chaos/WorldPainter/blob/8e3329aa7e7014dcda08f7ad25b8cb8fa97b4d2b/WorldPainter/WPGUI/src/main/java/org/pepsoft/worldpainter/biomeschemes/CustomBiomeDialog.java
[wp-src-defaultplugin-java]: https://github.com/Captain-Chaos/WorldPainter/blob/8e3329aa7e7014dcda08f7ad25b8cb8fa97b4d2b/WorldPainter/WPCore/src/main/java/org/pepsoft/worldpainter/DefaultPlugin.java
[wp-src-customlayercontroller-java]: https://github.com/Captain-Chaos/WorldPainter/blob/8e3329aa7e7014dcda08f7ad25b8cb8fa97b4d2b/WorldPainter/WPGUI/src/main/java/org/pepsoft/worldpainter/CustomLayerController.java
[wp-src-palette-java]: https://github.com/Captain-Chaos/WorldPainter/blob/8e3329aa7e7014dcda08f7ad25b8cb8fa97b4d2b/WorldPainter/WPGUI/src/main/java/org/pepsoft/worldpainter/palettes/Palette.java
[wp-src-editpalettedialog-java]: https://github.com/Captain-Chaos/WorldPainter/blob/8e3329aa7e7014dcda08f7ad25b8cb8fa97b4d2b/WorldPainter/WPGUI/src/main/java/org/pepsoft/worldpainter/palettes/EditPaletteDialog.java
[wp-src-importcustomitemsdialog-java]: https://github.com/Captain-Chaos/WorldPainter/blob/8e3329aa7e7014dcda08f7ad25b8cb8fa97b4d2b/WorldPainter/WPGUI/src/main/java/org/pepsoft/worldpainter/importing/ImportCustomItemsDialog.java
[wp-src-deletelayersdialog-java]: https://github.com/Captain-Chaos/WorldPainter/blob/8e3329aa7e7014dcda08f7ad25b8cb8fa97b4d2b/WorldPainter/WPGUI/src/main/java/org/pepsoft/worldpainter/DeleteLayersDialog.java
[wp-src-defaultplugin-java-l38]: https://github.com/Captain-Chaos/WorldPainter/blob/8e3329aa7e7014dcda08f7ad25b8cb8fa97b4d2b/WorldPainter/WPCore/src/main/java/org/pepsoft/worldpainter/DefaultPlugin.java#L38
[wp-src-readonly-java]: https://github.com/Captain-Chaos/WorldPainter/blob/8e3329aa7e7014dcda08f7ad25b8cb8fa97b4d2b/WorldPainter/WPCore/src/main/java/org/pepsoft/worldpainter/layers/ReadOnly.java
[wp-src-notpresent-java]: https://github.com/Captain-Chaos/WorldPainter/blob/8e3329aa7e7014dcda08f7ad25b8cb8fa97b4d2b/WorldPainter/WPCore/src/main/java/org/pepsoft/worldpainter/layers/NotPresent.java
[wp-src-resourcesexporter-java]: https://github.com/Captain-Chaos/WorldPainter/blob/8e3329aa7e7014dcda08f7ad25b8cb8fa97b4d2b/WorldPainter/WPCore/src/main/java/org/pepsoft/worldpainter/layers/exporters/ResourcesExporter.java
[wp-src-groundcoverlayereditor-java]: https://github.com/Captain-Chaos/WorldPainter/blob/8e3329aa7e7014dcda08f7ad25b8cb8fa97b4d2b/WorldPainter/WPGUI/src/main/java/org/pepsoft/worldpainter/layers/groundcover/GroundCoverLayerEditor.java
[wp-src-groundcoverlayerexporter-java]: https://github.com/Captain-Chaos/WorldPainter/blob/8e3329aa7e7014dcda08f7ad25b8cb8fa97b4d2b/WorldPainter/WPCore/src/main/java/org/pepsoft/worldpainter/layers/groundcover/GroundCoverLayerExporter.java
[wp-src-plantlayereditor-java]: https://github.com/Captain-Chaos/WorldPainter/blob/8e3329aa7e7014dcda08f7ad25b8cb8fa97b4d2b/WorldPainter/WPGUI/src/main/java/org/pepsoft/worldpainter/layers/plants/PlantLayerEditor.java
[wp-src-plants-java]: https://github.com/Captain-Chaos/WorldPainter/blob/8e3329aa7e7014dcda08f7ad25b8cb8fa97b4d2b/WorldPainter/WPCore/src/main/java/org/pepsoft/worldpainter/layers/plants/Plants.java
[wp-src-undergroundpocketsdialog-java]: https://github.com/Captain-Chaos/WorldPainter/blob/8e3329aa7e7014dcda08f7ad25b8cb8fa97b4d2b/WorldPainter/WPGUI/src/main/java/org/pepsoft/worldpainter/layers/pockets/UndergroundPocketsDialog.java
[wp-src-undergroundpocketslayerexporter-java]: https://github.com/Captain-Chaos/WorldPainter/blob/8e3329aa7e7014dcda08f7ad25b8cb8fa97b4d2b/WorldPainter/WPCore/src/main/java/org/pepsoft/worldpainter/layers/pockets/UndergroundPocketsLayerExporter.java
[wp-src-combinedlayer-java]: https://github.com/Captain-Chaos/WorldPainter/blob/8e3329aa7e7014dcda08f7ad25b8cb8fa97b4d2b/WorldPainter/WPCore/src/main/java/org/pepsoft/worldpainter/layers/CombinedLayer.java
[wp-src-combinedlayereditor-java]: https://github.com/Captain-Chaos/WorldPainter/blob/8e3329aa7e7014dcda08f7ad25b8cb8fa97b4d2b/WorldPainter/WPGUI/src/main/java/org/pepsoft/worldpainter/layers/combined/CombinedLayerEditor.java
[wp-src-customannotationlayerdialog-java]: https://github.com/Captain-Chaos/WorldPainter/blob/8e3329aa7e7014dcda08f7ad25b8cb8fa97b4d2b/WorldPainter/WPGUI/src/main/java/org/pepsoft/worldpainter/layers/annotation/CustomAnnotationLayerDialog.java
[wp-src-editpaintdialog-java]: https://github.com/Captain-Chaos/WorldPainter/blob/8e3329aa7e7014dcda08f7ad25b8cb8fa97b4d2b/WorldPainter/WPGUI/src/main/java/org/pepsoft/worldpainter/layers/renderers/EditPaintDialog.java
[wp-src-paintpicker-java]: https://github.com/Captain-Chaos/WorldPainter/blob/8e3329aa7e7014dcda08f7ad25b8cb8fa97b4d2b/WorldPainter/WPGUI/src/main/java/org/pepsoft/worldpainter/layers/renderers/PaintPicker.java
[wp-src-defaultcustomobjectprovider-java]: https://github.com/Captain-Chaos/WorldPainter/blob/8e3329aa7e7014dcda08f7ad25b8cb8fa97b4d2b/WorldPainter/WPCore/src/main/java/org/pepsoft/worldpainter/DefaultCustomObjectProvider.java
[wp-src-bo2layereditor-java]: https://github.com/Captain-Chaos/WorldPainter/blob/8e3329aa7e7014dcda08f7ad25b8cb8fa97b4d2b/WorldPainter/WPGUI/src/main/java/org/pepsoft/worldpainter/layers/bo2/Bo2LayerEditor.java
[wp-src-editobjectattributes-java]: https://github.com/Captain-Chaos/WorldPainter/blob/8e3329aa7e7014dcda08f7ad25b8cb8fa97b4d2b/WorldPainter/WPGUI/src/main/java/org/pepsoft/worldpainter/layers/bo2/EditObjectAttributes.java
[wp-src-offseteditor-java]: https://github.com/Captain-Chaos/WorldPainter/blob/8e3329aa7e7014dcda08f7ad25b8cb8fa97b4d2b/WorldPainter/WPGUI/src/main/java/org/pepsoft/worldpainter/layers/bo2/OffsetEditor.java
[wp-src-schem-java]: https://github.com/Captain-Chaos/WorldPainter/blob/8e3329aa7e7014dcda08f7ad25b8cb8fa97b4d2b/WorldPainter/WPCore/src/main/java/org/pepsoft/worldpainter/layers/bo2/Schem.java
[wp-src-bo2layerexporter-java]: https://github.com/Captain-Chaos/WorldPainter/blob/8e3329aa7e7014dcda08f7ad25b8cb8fa97b4d2b/WorldPainter/WPCore/src/main/java/org/pepsoft/worldpainter/layers/bo2/Bo2LayerExporter.java
[wp-src-tunnellayerdialog-java]: https://github.com/Captain-Chaos/WorldPainter/blob/8e3329aa7e7014dcda08f7ad25b8cb8fa97b4d2b/WorldPainter/WPGUI/src/main/java/org/pepsoft/worldpainter/layers/tunnel/TunnelLayerDialog.java
[wp-src-tunnellayer-java]: https://github.com/Captain-Chaos/WorldPainter/blob/8e3329aa7e7014dcda08f7ad25b8cb8fa97b4d2b/WorldPainter/WPCore/src/main/java/org/pepsoft/worldpainter/layers/tunnel/TunnelLayer.java
[wp-src-tunnellayerexporter-java]: https://github.com/Captain-Chaos/WorldPainter/blob/8e3329aa7e7014dcda08f7ad25b8cb8fa97b4d2b/WorldPainter/WPCore/src/main/java/org/pepsoft/worldpainter/layers/tunnel/TunnelLayerExporter.java
[wp-src-floordimensionsettingsdialog-java]: https://github.com/Captain-Chaos/WorldPainter/blob/8e3329aa7e7014dcda08f7ad25b8cb8fa97b4d2b/WorldPainter/WPGUI/src/main/java/org/pepsoft/worldpainter/layers/tunnel/FloorDimensionSettingsDialog.java
[wp-src-customlayercontroller-java-l492]: https://github.com/Captain-Chaos/WorldPainter/blob/8e3329aa7e7014dcda08f7ad25b8cb8fa97b4d2b/WorldPainter/WPGUI/src/main/java/org/pepsoft/worldpainter/CustomLayerController.java#L492
[wp-src-floatinglayerdialog-java]: https://github.com/Captain-Chaos/WorldPainter/blob/8e3329aa7e7014dcda08f7ad25b8cb8fa97b4d2b/WorldPainter/WPGUI/src/main/java/org/pepsoft/worldpainter/layers/tunnel/FloatingLayerDialog.java
[wp-src-floatinglayerexporter-java]: https://github.com/Captain-Chaos/WorldPainter/blob/8e3329aa7e7014dcda08f7ad25b8cb8fa97b4d2b/WorldPainter/WPCore/src/main/java/org/pepsoft/worldpainter/layers/tunnel/FloatingLayerExporter.java
[wp-src-importheightmapdialog-java]: https://github.com/Captain-Chaos/WorldPainter/blob/8e3329aa7e7014dcda08f7ad25b8cb8fa97b4d2b/WorldPainter/WPGUI/src/main/java/org/pepsoft/worldpainter/ImportHeightMapDialog.java
[wp-src-heightmapimporter-java]: https://github.com/Captain-Chaos/WorldPainter/blob/8e3329aa7e7014dcda08f7ad25b8cb8fa97b4d2b/WorldPainter/WPCore/src/main/java/org/pepsoft/worldpainter/importing/HeightMapImporter.java
[wp-src-importmaskdialog-java]: https://github.com/Captain-Chaos/WorldPainter/blob/8e3329aa7e7014dcda08f7ad25b8cb8fa97b4d2b/WorldPainter/WPGUI/src/main/java/org/pepsoft/worldpainter/importing/ImportMaskDialog.java
[wp-src-maskimporter-java]: https://github.com/Captain-Chaos/WorldPainter/blob/8e3329aa7e7014dcda08f7ad25b8cb8fa97b4d2b/WorldPainter/WPGUI/src/main/java/org/pepsoft/worldpainter/importing/MaskImporter.java
[wp-src-mapimportdialog-java]: https://github.com/Captain-Chaos/WorldPainter/blob/8e3329aa7e7014dcda08f7ad25b8cb8fa97b4d2b/WorldPainter/WPGUI/src/main/java/org/pepsoft/worldpainter/importing/MapImportDialog.java
[wp-src-javamapimporter-java]: https://github.com/Captain-Chaos/WorldPainter/blob/8e3329aa7e7014dcda08f7ad25b8cb8fa97b4d2b/WorldPainter/WPCore/src/main/java/org/pepsoft/worldpainter/importing/JavaMapImporter.java
[wp-src-importwarningsdialog-java]: https://github.com/Captain-Chaos/WorldPainter/blob/8e3329aa7e7014dcda08f7ad25b8cb8fa97b4d2b/WorldPainter/WPGUI/src/main/java/org/pepsoft/worldpainter/ImportWarningsDialog.java
[wp-src-mapimporterprovider-java]: https://github.com/Captain-Chaos/WorldPainter/blob/8e3329aa7e7014dcda08f7ad25b8cb8fa97b4d2b/WorldPainter/WPCore/src/main/java/org/pepsoft/worldpainter/plugins/MapImporterProvider.java
[wp-src-platform-java]: https://github.com/Captain-Chaos/WorldPainter/blob/8e3329aa7e7014dcda08f7ad25b8cb8fa97b4d2b/WorldPainter/WPCore/src/main/java/org/pepsoft/worldpainter/Platform.java
[wp-src-exportworlddialog-java]: https://github.com/Captain-Chaos/WorldPainter/blob/8e3329aa7e7014dcda08f7ad25b8cb8fa97b4d2b/WorldPainter/WPGUI/src/main/java/org/pepsoft/worldpainter/ExportWorldDialog.java
[wp-src-editsuperflatpresetdialog-java]: https://github.com/Captain-Chaos/WorldPainter/blob/8e3329aa7e7014dcda08f7ad25b8cb8fa97b4d2b/WorldPainter/WPGUI/src/main/java/org/pepsoft/worldpainter/superflat/EditSuperflatPresetDialog.java
[wp-src-abstractworldexporter-java]: https://github.com/Captain-Chaos/WorldPainter/blob/8e3329aa7e7014dcda08f7ad25b8cb8fa97b4d2b/WorldPainter/WPCore/src/main/java/org/pepsoft/worldpainter/exporting/AbstractWorldExporter.java
[wp-src-javaworldexporter-java]: https://github.com/Captain-Chaos/WorldPainter/blob/8e3329aa7e7014dcda08f7ad25b8cb8fa97b4d2b/WorldPainter/WPCore/src/main/java/org/pepsoft/worldpainter/exporting/JavaWorldExporter.java
[wp-src-javaexportsettingseditor-java]: https://github.com/Captain-Chaos/WorldPainter/blob/8e3329aa7e7014dcda08f7ad25b8cb8fa97b4d2b/WorldPainter/WPCore/src/main/java/org/pepsoft/worldpainter/platforms/JavaExportSettingsEditor.java
[wp-src-exportprogressdialog-java]: https://github.com/Captain-Chaos/WorldPainter/blob/8e3329aa7e7014dcda08f7ad25b8cb8fa97b4d2b/WorldPainter/WPGUI/src/main/java/org/pepsoft/worldpainter/ExportProgressDialog.java
[wp-src-testexportdialog-java]: https://github.com/Captain-Chaos/WorldPainter/blob/8e3329aa7e7014dcda08f7ad25b8cb8fa97b4d2b/WorldPainter/WPGUI/src/main/java/org/pepsoft/worldpainter/TestExportDialog.java
[wp-src-exporttileselectiondialog-java]: https://github.com/Captain-Chaos/WorldPainter/blob/8e3329aa7e7014dcda08f7ad25b8cb8fa97b4d2b/WorldPainter/WPGUI/src/main/java/org/pepsoft/worldpainter/ExportTileSelectionDialog.java
[wp-src-heightmapexporter-java]: https://github.com/Captain-Chaos/WorldPainter/blob/8e3329aa7e7014dcda08f7ad25b8cb8fa97b4d2b/WorldPainter/WPCore/src/main/java/org/pepsoft/worldpainter/exporting/HeightMapExporter.java
[wp-src-mergeworlddialog-java]: https://github.com/Captain-Chaos/WorldPainter/blob/8e3329aa7e7014dcda08f7ad25b8cb8fa97b4d2b/WorldPainter/WPGUI/src/main/java/org/pepsoft/worldpainter/MergeWorldDialog.java
[wp-src-javaworldmerger-java]: https://github.com/Captain-Chaos/WorldPainter/blob/8e3329aa7e7014dcda08f7ad25b8cb8fa97b4d2b/WorldPainter/WPCore/src/main/java/org/pepsoft/worldpainter/merging/JavaWorldMerger.java
[wp-src-changeheightdialog-java]: https://github.com/Captain-Chaos/WorldPainter/blob/8e3329aa7e7014dcda08f7ad25b8cb8fa97b4d2b/WorldPainter/WPGUI/src/main/java/org/pepsoft/worldpainter/ChangeHeightDialog.java
[wp-src-rotateworlddialog-java]: https://github.com/Captain-Chaos/WorldPainter/blob/8e3329aa7e7014dcda08f7ad25b8cb8fa97b4d2b/WorldPainter/WPGUI/src/main/java/org/pepsoft/worldpainter/RotateWorldDialog.java
[wp-src-shiftworlddialog-java]: https://github.com/Captain-Chaos/WorldPainter/blob/8e3329aa7e7014dcda08f7ad25b8cb8fa97b4d2b/WorldPainter/WPGUI/src/main/java/org/pepsoft/worldpainter/ShiftWorldDialog.java
[wp-src-scaleworlddialog-java]: https://github.com/Captain-Chaos/WorldPainter/blob/8e3329aa7e7014dcda08f7ad25b8cb8fa97b4d2b/WorldPainter/WPGUI/src/main/java/org/pepsoft/worldpainter/ScaleWorldDialog.java
[wp-src-scriptingcontext-java]: https://github.com/Captain-Chaos/WorldPainter/blob/8e3329aa7e7014dcda08f7ad25b8cb8fa97b4d2b/WorldPainter/WPCore/src/main/java/org/pepsoft/worldpainter/tools/scripts/ScriptingContext.java
[wp-src-scriptrunner-java]: https://github.com/Captain-Chaos/WorldPainter/blob/8e3329aa7e7014dcda08f7ad25b8cb8fa97b4d2b/WorldPainter/WPGUI/src/main/java/org/pepsoft/worldpainter/tools/scripts/ScriptRunner.java
[wp-src-mappingop-java]: https://github.com/Captain-Chaos/WorldPainter/blob/8e3329aa7e7014dcda08f7ad25b8cb8fa97b4d2b/WorldPainter/WPCore/src/main/java/org/pepsoft/worldpainter/tools/scripts/MappingOp.java
[wp-src-createfilterop-java]: https://github.com/Captain-Chaos/WorldPainter/blob/8e3329aa7e7014dcda08f7ad25b8cb8fa97b4d2b/WorldPainter/WPCore/src/main/java/org/pepsoft/worldpainter/tools/scripts/CreateFilterOp.java
[wp-src-importheightmapop-java]: https://github.com/Captain-Chaos/WorldPainter/blob/8e3329aa7e7014dcda08f7ad25b8cb8fa97b4d2b/WorldPainter/WPCore/src/main/java/org/pepsoft/worldpainter/tools/scripts/ImportHeightMapOp.java
[wp-src-exportworldop-java]: https://github.com/Captain-Chaos/WorldPainter/blob/8e3329aa7e7014dcda08f7ad25b8cb8fa97b4d2b/WorldPainter/WPCore/src/main/java/org/pepsoft/worldpainter/tools/scripts/ExportWorldOp.java
[wp-src-wppluginmanager-java]: https://github.com/Captain-Chaos/WorldPainter/blob/8e3329aa7e7014dcda08f7ad25b8cb8fa97b4d2b/WorldPainter/WPCore/src/main/java/org/pepsoft/worldpainter/plugins/WPPluginManager.java
[wp-src-layerprovider-java]: https://github.com/Captain-Chaos/WorldPainter/blob/8e3329aa7e7014dcda08f7ad25b8cb8fa97b4d2b/WorldPainter/WPCore/src/main/java/org/pepsoft/worldpainter/plugins/LayerProvider.java
[wp-src-customlayerprovider-java]: https://github.com/Captain-Chaos/WorldPainter/blob/8e3329aa7e7014dcda08f7ad25b8cb8fa97b4d2b/WorldPainter/WPCore/src/main/java/org/pepsoft/worldpainter/plugins/CustomLayerProvider.java
[wp-src-operationprovider-java]: https://github.com/Captain-Chaos/WorldPainter/blob/8e3329aa7e7014dcda08f7ad25b8cb8fa97b4d2b/WorldPainter/WPCore/src/main/java/org/pepsoft/worldpainter/plugins/OperationProvider.java
[wp-src-platformprovider-java]: https://github.com/Captain-Chaos/WorldPainter/blob/8e3329aa7e7014dcda08f7ad25b8cb8fa97b4d2b/WorldPainter/WPCore/src/main/java/org/pepsoft/worldpainter/plugins/PlatformProvider.java
[wp-src-customobjectprovider-java]: https://github.com/Captain-Chaos/WorldPainter/blob/8e3329aa7e7014dcda08f7ad25b8cb8fa97b4d2b/WorldPainter/WPCore/src/main/java/org/pepsoft/worldpainter/plugins/CustomObjectProvider.java
[wp-src-respawnplayerdialog-java]: https://github.com/Captain-Chaos/WorldPainter/blob/8e3329aa7e7014dcda08f7ad25b8cb8fa97b4d2b/WorldPainter/WPGUI/src/main/java/org/pepsoft/worldpainter/tools/RespawnPlayerDialog.java
[wp-src-preferencesdialog-java]: https://github.com/Captain-Chaos/WorldPainter/blob/8e3329aa7e7014dcda08f7ad25b8cb8fa97b4d2b/WorldPainter/WPGUI/src/main/java/org/pepsoft/worldpainter/PreferencesDialog.java
[wp-src-main-java]: https://github.com/Captain-Chaos/WorldPainter/blob/8e3329aa7e7014dcda08f7ad25b8cb8fa97b4d2b/WorldPainter/WPGUI/src/main/java/org/pepsoft/worldpainter/Main.java
[wp-src-errordialog-java]: https://github.com/Captain-Chaos/WorldPainter/blob/8e3329aa7e7014dcda08f7ad25b8cb8fa97b4d2b/WorldPainter/WPGUI/src/main/java/org/pepsoft/worldpainter/ErrorDialog.java
[wp-src-aboutdialog-java]: https://github.com/Captain-Chaos/WorldPainter/blob/8e3329aa7e7014dcda08f7ad25b8cb8fa97b4d2b/WorldPainter/WPGUI/src/main/java/org/pepsoft/worldpainter/AboutDialog.java
[wp-src-app-java-l4393]: https://github.com/Captain-Chaos/WorldPainter/blob/8e3329aa7e7014dcda08f7ad25b8cb8fa97b4d2b/WorldPainter/WPGUI/src/main/java/org/pepsoft/worldpainter/App.java#L4393
[wp-src-app-java-l3119]: https://github.com/Captain-Chaos/WorldPainter/blob/8e3329aa7e7014dcda08f7ad25b8cb8fa97b4d2b/WorldPainter/WPGUI/src/main/java/org/pepsoft/worldpainter/App.java#L3119
[wp-src-mergeworldop-java]: https://github.com/Captain-Chaos/WorldPainter/blob/8e3329aa7e7014dcda08f7ad25b8cb8fa97b4d2b/WorldPainter/WPCore/src/main/java/org/pepsoft/worldpainter/tools/scripts/MergeWorldOp.java
[wp-src-exportpartdialog-java]: https://github.com/Captain-Chaos/WorldPainter/blob/8e3329aa7e7014dcda08f7ad25b8cb8fa97b4d2b/WorldPainter/WPGUI/src/main/java/org/pepsoft/worldpainter/ExportPartDialog.java
[wp-src-heightmapeditor-java]: https://github.com/Captain-Chaos/WorldPainter/blob/8e3329aa7e7014dcda08f7ad25b8cb8fa97b4d2b/WorldPainter/WPGUI/src/main/java/org/pepsoft/worldpainter/tools/HeightMapEditor.java
[wp-src-cliffcarverexporter-java]: https://github.com/Captain-Chaos/WorldPainter/blob/8e3329aa7e7014dcda08f7ad25b8cb8fa97b4d2b/WorldPainter/WPCore/src/main/java/org/pepsoft/worldpainter/layers/exporters/CliffCarverExporter.java

## 6. 候选架构【建议，尚未定案】

### 6.1 世界表示

采用“分块高度场 + 局部三维体积/挖掘规则 + 参数化实例”的混合模型。

| 内容 | 候选表示 | 用途 |
| --- | --- | --- |
| 地表主体 | 分块高度场及材质、生态群系数据 | 大范围地形与地表绘制 |
| 地貌实例 | 资源引用、变换、蒙版、混合与修改器 | 可调整的山体等地形组合 |
| 洞穴与悬挑 | 局部体积、挖掘规则或稀疏方块差量 | 表达同一水平位置的多层空间 |
| 建筑结构 | 共享资源与实例变换 | 高效重复放置 |
| 道路 | 节点连接关系、曲线和横断面参数 | 可编辑路网与地形整形 |
| 手工精修 | 局部差量或可重放编辑记录 | 保留创作者的直接修改 |

具体分块尺寸、体积表示、磁盘格式、空间索引和数据精度均未选择。不能默认所有内容都展开成完整体素数组，也不能仅靠高度场表达所有地下和悬挑结构。

### 6.2 编辑工程与 Minecraft 存档分离

工程保存创作意图与必要的原始数据：资源、参数、连接关系、种子和局部修改。最终方块、预览网格和派生数据尽可能按需求值。

Minecraft 存档是导出产物，不应成为参数化编辑工程的唯一权威数据。参数调整后，应能重新生成受影响区域。

所有可重建缓存都必须关联工程版本、生成器版本和必要参数。无法确定性重建的结果应明确持久化，不能在重开工程后悄然变化。实时预览可以降低采样、细节和计算质量，但最终烘焙必须从权威工程数据完成求值，不能把远景 LOD 网格直接当作最终方块数据。

工程的垂直坐标与存储不要直接套用原版方块坐标压缩位数或区段索引限制；通过目标版本适配层施加导出边界。内部精度和表示仍需原型确定。

### 6.3 流式加载与计算

- 用空间索引确定视口、笔刷、实例和导出区域的影响范围。
- 为内存、显存、任务并发与每帧上传设置预算。
- 大型高度图导入时构建可分块读取的数据及多级预览，避免要求一次完整解码入内存。
- 参数修改只使相关结果失效；移动对象必须同时覆盖旧位置与新位置。
- 优先计算当前工作区，取消或丢弃过期计算结果。
- 跨块运算考虑边界数据；局部邻域算子可用重叠计算区，长距离水流等问题不能仅靠固定边界补丁保证正确。
- 全局操作允许先标记结果失效，再按需或后台重建，不承诺即时完成全分辨率重算。

### 6.4 预览与交互

- 视口始终使用体素/方块几何。近景编辑区提供方块级拾取与精确结果；后台生成尚未完成时明确标记，不切换成平滑曲面地形。
- 中远景采用简化体素/方块 LOD，可研究 Distant Horizons 的远景分层与聚合思路；LOD 表示、透明材料和多层垂直结构保留策略待验证。
- 不要求所有视口内容在每一帧完成最终烘焙；降低远景细节不等于更改权威地形。
- 明确近远景过渡、洞口、悬挑和大型建筑在简化后的可见性规则。
- 提供地表、洞穴、浮空层、ceiling 和 Master/Detail 等完整工作流的观察方式；剖切、隐藏地表和辅助顶视图的具体交互待规划。
- 世界坐标与渲染局部坐标分离，验证大坐标下的拾取、相机和边界精度。
- 渲染简化不能修改权威世界数据；精确预览与导出共用生成语义。快速预览与最终烘焙的质量差异应有明确标识和校验方式。
- 精确模拟 Minecraft 所有光照、着色器、资源包或模组效果不属于当前已确认承诺。

### 6.5 存储与撤销

建议使用增量保存、局部差量、操作记录和检查点，避免每次保存或撤销都复制整个世界。

需要验证崩溃恢复、缺失资源、生成器升级和缓存失效时的行为。工程资源是否打包、自包含或使用外部路径引用，尚待决策。

## 7. 各功能的候选实现路径

### 7.1 地貌实例

建议保留位置、旋转、水平/垂直缩放、高度偏移、羽化、蒙版和混合模式，并将噪波、侵蚀作为可调整的修改器。

应区分对单个高度图资源的侵蚀与对混合后地形的侵蚀。两者的输入、影响范围、缓存复用与计算成本不同。

优先验证基础变换与混合；复杂侵蚀使用交互预览和后台精算。多级精度间的结果差异必须明确，不能把低精度预览当作已完成的最终结果。

### 7.2 路网

建议先支持指定节点连接、可编辑曲线、道路宽度、地面整平与边坡；基于坡度、水体和障碍物的自动选线可以后续增加。

道路模型同时记录拓扑、平面曲线和纵向高度。交叉口、桥梁、隧道、施工范围和坡度限制属于待定范围。

避免道路从自己修改后的地形不断重新选线造成循环依赖：选线读取明确的基础地形阶段，再生成道路施工影响。

### 7.3 建筑实例

建议采用共享资源与独立实例，支持按范围查询、实例变换及局部覆盖规则。优先评估 Sponge `.schem`，再补传统 `.schematic`。

需要处理方块状态、方块实体、普通实体、数据版本、空气覆盖规则和跨块放置。首版可考虑仅支持 90° 倍数旋转；任意角度旋转需要方块重采样，可能损坏细节，不能默认无损支持。

### 7.4 洞穴资产与自动识别

普通 schematic 保存的方块布局不足以保证无歧义识别任意洞穴。存在出口时，内部空气与外部空气相通，单纯从边界进行空气连通搜索会把二者合并。

建议用 schematic 配合额外元数据形成洞穴资产：

| 语义 | 放置行为 |
| --- | --- |
| 洞壁与装饰 | 放置指定方块 |
| 内部空腔 | 挖空原地形 |
| 外部区域 | 保留原地形 |
| 出入口 | 连接地表或其他洞穴 |

导入流程可自动提出候选区域与出口，再允许用户标记内部种子、圈定出口或修补漏口。必要时对候选出口临时封口后分析内部，但这仍是需要验证的识别策略，不保证适用于任意输入。

最终确认的蒙版与出口信息应保存并复用。最小覆土厚度、与地表融合、洞穴连接、地下水及材质过渡待进一步规划。

## 8. Minecraft 导出与实验高度

### 8.1 可选版本与完整烘焙【需求已确认，实现方案建议】

首轮测试版本暂定 Minecraft Java Edition 1.20.1；后续支持选择导出版本。Bedrock 支持尚未决定。

建议采用统一工程模型和独立版本适配器，导出配置分别记录 Edition、游戏版本、DataVersion、方块/生态群系能力、NBT 格式以及高度/运行环境配置。新增版本通过适配器扩展，不把版本判断分散到各编辑工具。

向旧版本转换时，需要明确不可表示方块和属性的处理：拒绝、提示用户选择映射或显式替换。不得静默丢失内容，也不能仅修改 DataVersion 伪装完成版本转换。

需要检查的内容包括方块状态、方块实体、实体、生态群系、光照、世界元数据、目标版本字段及区块边界。具体字段以确定版本的实际格式为准。

导出期间使用固定工程快照和生成配置，防止正在编辑的内容让同一份存档前后不一致。

候选完整烘焙流程：求值地貌与修改器 → 生成道路、洞穴和结构 → 解决覆盖关系并生成方块 → 应用目标版本映射 → 校验高度与元数据 → 完成所需光照及后处理 → 写出并校验存档。具体操作顺序与跨区依赖需要在原型中确认。

### 8.2 大规模导出

建议支持选区导出、分批处理、进度和磁盘容量估算、取消、恢复、失败报告与输出验证。并发度需按 CPU、内存和磁盘吞吐量控制；存在跨区依赖时安排明确的后处理阶段。

以下仅为条件计算，不是压缩率或速度承诺：

| 范围 | 平均每区块落盘 32 KiB | 平均每区块落盘 64 KiB |
| --- | --- | --- |
| 8K × 8K | 8 GiB | 16 GiB |
| 16K × 16K | 32 GiB | 64 GiB |
| 128K × 128K | 2 TiB | 4 TiB |

| 假设 | 全量 128K 范围的计算结果 |
| --- | --- |
| 完整流水线实测为每秒 1,000 区块 | 约 18.6 小时 |
| 完整流水线实测为每秒 10,000 区块 | 约 1.86 小时 |

必须用包含代表性地形、洞穴、建筑和材质的样本测量。外推时说明硬件、内容密度、压缩设置及是否包含光照和磁盘写入。

选区导出适合测试，但未导出的区域不会自动继承编辑器内的自定义地貌。普通 Minecraft 全量加载自定义世界与依赖插件/模组运行时生成，是不同的产品路线，不能混为一谈。

### 8.3 超高世界的兼容边界【静态核验完成，运行验证未完成】

2026-11-19 从 Mojang 官方版本清单取得 1.20.1 服务端与官方映射，核验 SHA-1 后，检查内置主世界维度 JSON 及 `DimensionType`、`BlockPos` 的字段、初始化和校验字节码。未运行 Minecraft，也未完成超高世界导出/加载测试。

静态核验结果：

- 默认主世界为 `min_y = -64`、`height = 384`，实际方块 Y 范围为 -64 至 319。
- 维度类型允许的最大垂直跨度为 4,064；可用 Y 边界为 -2,032 至 2,031，最高边界的排他值为 2,032。
- `min_y` 与 `height` 需要按 16 对齐，`height` 至少为 16，且 `min_y + height <= 2032`；`logical_height` 不得大于 `height`。
- 4,064 是可配置的总跨度，不代表能从 Y=0 向上建到 4,064，也不代表把一个字段改大后所有生成、光照与游戏玩法都已验证可用。

建议区分三类导出配置：

| 配置 | 目的 | 边界 |
| --- | --- | --- |
| 标准 1.20.1 | 默认主世界范围的兼容导出 | 优先完成端到端正确性验证 |
| 自定义维度高度 | 在该版本校验范围内扩展默认高度 | 配套维度类型/数据包、必要生成设置与元数据；实验验证后开放 |
| 模组扩展高度 | 超出上述坐标或跨度范围，例如更高的正 Y 地图 | 需要明确兼容的客户端/服务端修改及存档协议；不能承诺原版直接加载 |

几千格高的编辑工程是产品目标；可导出的高度必须由目标版本和运行环境共同决定。首轮不假定已有适配 1.20.1 的现成超高模组，更不能因为其他 Minecraft 版本存在此类模组就推定本版本可用。

证据入口：[官方版本清单](https://piston-meta.mojang.com/mc/game/version_manifest_v2.json)、[1.20.1 服务端](https://piston-data.mojang.com/v1/objects/84194a2f286ef7c14ed7ce0090dba59902951553/server.jar)、[官方服务端映射](https://piston-data.mojang.com/v1/objects/0b4dba049482496c507b2387a73a913230ebbd76/server.txt)。服务端 SHA-1 为 `84194a2f286ef7c14ed7ce0090dba59902951553`，映射 SHA-1 为 `0b4dba049482496c507b2387a73a913230ebbd76`。二进制与调查缓存仅保存在仓库外，不属于 Worldbench 源码或交付内容。

## 9. 技术选型与开源复用调研

| 事项 | 当前状态 |
| --- | --- |
| 开发语言 | 未选择；Java、C++、Rust、C# 等仅是候选，不因参考仓库语言自动定案 |
| UI 框架与 3D 引擎 | 尚未选择；优先评估 Godot + Voxel Tools，另以 NAADF 作渲染实验对照 |
| 图形 API | 未选择 |
| WorldPainter 代码复用范围 | 未选择，受架构与许可证影响 |
| 世界工程格式 | 未选择 |
| 分块、缓存与空间索引方案 | 未选择 |
| 三维体积与洞穴表示 | 未选择 |
| 首发操作系统 | 未选择；不能从当前开发电脑推定产品只支持 Windows |
| Minecraft 版本与版本范围 | 首轮暂定 1.20.1；可选多版本导出已确认，具体列表未定 |
| 开源、商业与许可证路线 | 正式发布开源、不以盈利为目的、接受 GPL 复用已确认；具体许可证未定 |

选型应优先考虑目标硬件上的验证结果、开发维护成本、依赖与许可证、Minecraft 兼容性，以及团队能力。不能将语言或某个引擎名称本身视为 128K 性能保证。

### 9.1 现成项目与可复用范围【2026-11-19 调研，均未集成或跑分】

“候选复用”表示值得进行集成验证，不表示可零修改直接完成 Worldbench。核对的是指定提交的官方仓库、README、文档及根许可证；引入时还需固定依赖和检查所用子模块、资源及兼容版本。

| 项目 | 本次核对的许可 | 可借用的能力 | 集成判断与缺口 |
| --- | --- | --- | --- |
| Voxel Tools for Godot（Zylann/godot_voxel） | MIT | 体素编辑、分块网格、加载/卸载、生成器；另有平滑地形 Transvoxel LOD | 工程候选须先验证方块路径；平滑地形 LOD 不能用来满足本项目视口要求。官方 API 的方块 LOD 支持限制与 Minecraft 方块语义需重点检查 |
| Terrain3D（TokisanGames/Terrain3D） | MIT | 可编辑高度场、分区、地形 LOD、洞口与植被实例 | 仅保留分区/编辑算法参考，不作为直接显示的地表或远景组件；其平滑高度场渲染不符合体素视口基准。README 的规模也不能证明 1 方块采样的 128K |
| NAADF（cg-tuwien/NAADF） | MIT | 使用嵌套轴向距离场加速体素光线遍历，CPU 编辑后同步 GPU | 重要研究对照。作者展示 8,256² 场景、声明支持 16,384³ 的编辑；这是作者演示与规模声明，未在目标硬件复测，不能视为任意密度场景的性能保证 |
| Distant Horizons | LGPLv3，另有独立素材许可 | Minecraft 远景 LOD、远景数据与缓存架构 | 用户明确允许参考其体素远景 LOD 思路；不代表已选择/集成其实现。core、Java、渲染接口及游戏适配依赖仍须核对 |
| BlueMap | MIT | 从 Minecraft 存档生成三维表面地图并在浏览器查看 | 可参考存档渲染与预览流程；读取存档和生成展示地图的流程需要改造才能服务交互创作 |
| prismarine-viewer | MIT | Web/Three.js Minecraft 查看器，文档列出 1.20.1 支持及独立内存世界示例 | 适合小规模预览/交互概念验证；未证明适合 16K 高细节或 128K 主编辑视口 |
| vengi | 主体代码 MIT；资源及第三方组件另查 | 体素资产编辑、缩略图和格式转换，源码有 Minecraft/Schematic 格式模块 | 候选资产工具链或局部编辑参考；格式可读不等于无损保留所有 Minecraft 方块状态与实体 |
| OpenVDB / NanoVDB 所在仓库 | 当前主分支 Apache-2.0 | 稀疏体积与体积处理基础 | 适合评估洞穴/体积运算；不是完整地形编辑器或 Minecraft 渲染器，动态更新与 GPU 路径仍需验证 |
| Chunky | GPLv3 | Minecraft 路径追踪渲染 | 可参考最终静态效果预览；不作为流畅主编辑视口的首选 |
| WorldPainter | GPLv3，具体文件声明另查 | Minecraft 导出、对象导入、材料语义与生成流程 | 优先评估导出适配器或生成组件复用；仍需改造与新工程数据的连接 |

NAADF 使用带计算着色器支持的 MonoGame 分支，当前说明仅支持 Windows 10/11，采用自己的 `.cvox` 数据格式；其资源导入列出 `.vox`、`.vl32` 等。它能证明这类渲染方向有可运行研究实现，但尚未证明 Minecraft 存档、任意方块模型、完整资源包和跨平台能力可直接复用。

需要排除的“源码可见即可开源复用”误判：

- Amulet-Core 当前 `2.0` 分支与 Amulet-Map-Editor 当前 `0.10` 分支的根许可证均为保留所有权利，并要求购买授权。不能沿用旧资料把当前版本归类为 BSD 可自由复用。历史开源版本需要单独锁定提交、依赖和授权范围后再评估，目前不列为首选。
- Voxy 本次检查的 `dev` 分支许可证声明保留所有权利并禁止再分发，不列入可直接合入的开源候选。
- VoxelPluginFreeLegacy 可读取源码，但本次未从检查的根文件取得明确开源授权；UE 路线与 GPL 组件的组合许可也尚待复核。暂不将它标记为 MIT 或 GPL 兼容组件。

### 9.2 LOD、距离场与 UE 的适用边界

- LOD 减少远处显示细节；分块与流式加载控制工作集；可见面剔除、网格合并和实例化降低渲染成本。它们需要共同配合，不能相互替代。
- 距离场可以表示几何、支持体积运算或加速光线遍历；它自身不自动解决版本导出、编辑撤销和整个世界的数据管理。
- Epic 的 Mesh Distance Fields 文档描述的是静态网格的离线生成表示，主要用于光照、阴影、碰撞等；其生成不能直接在运行时完成。Global Distance Field 的局部合成更新与“任意新地形的距离场生成”不是同一操作。
- NAADF 的可更新体素/AADF 数据结构是另一条路径，不能把它与 UE 的 Mesh Distance Fields 当成同一种现成组件。
- UE 在此首先作为硬件与体验参考，而非已选引擎。如果选用 UE，必须核对当前引擎许可与拟复用 GPL 组件的组合兼容性，不能根据非营利或源码可见直接推定可组合发行。本次官方 EULA 页面返回 HTTP 403，未完成最新条文复核；此项保留为待验证，不把已确认的 Epic 渲染文档当作许可依据。

来源：[Epic Mesh Distance Fields](https://dev.epicgames.com/documentation/en-us/unreal-engine/mesh-distance-fields-in-unreal-engine)、[UE EULA](https://www.unrealengine.com/eula/unreal)。上述是选型约束，不代表已选择某种法律结构或批准引入任何依赖。

### 9.3 当前优先验证路线【建议】

1. 工程候选：Godot 4 + Voxel Tools 的方块渲染路径，建立自己的参数化工程与版本导出适配层；评估 WorldPainter 可复用导出部分。先验证方块 LOD 的补齐成本，再决定是否采用。
2. 对照候选：NAADF 的动态体素/距离场渲染。在相同数据、相同硬件下检查编辑更新成本、显存、画质和资源格式适配，再判断是否值得移植或作为替代渲染路径。
3. 补充组件按缺口选择：体素远景研究 Distant Horizons，局部资产工具参考 vengi，稀疏体积运算评估 OpenVDB。Terrain3D 仅作为分区/高度场算法参考，不选其平滑表面作为预览路径；不要堆入重叠的整套引擎。

首要未决点：Minecraft 风格的方块精确近景与大范围简化远景如何衔接。Voxel Tools 的平滑 LOD 演示或 NAADF 的特定场景都不能替代此项验证。尚未批准开始原型实现。

### 9.4 硬件与交互测试基线【方向已确认，数值为建议】

Epic 当前 UE 推荐配置列出 32 GB RAM、8 GB 及以上显存和 DX12 兼容显卡。这可作为档位参考，不能直接证明 Worldbench 在同配置上达标，也不采用 Epic 内部高端工作站作为普通用户门槛。

| 档位 | 建议测试配置 | 验证重点 |
| --- | --- | --- |
| 常规创作者基线 | 现代 8 核级 CPU、32 GB RAM、8 GB 显存独立 GPU、NVMe SSD | 8K/16K 典型场景下的连续编辑与预览 |
| 较重创作档 | 现代 12–16 核级 CPU、64 GB RAM、12–16 GB 显存、NVMe SSD | 密集建筑、复杂洞穴、超高地形与较重烘焙 |
| 极端规模测试 | 在上述配置上使用预算受控的流式工作集，另行估算导出空间 | 128K 地址范围、随机跳转、缓存增长和导出可恢复性 |

CPU 档位和 NVMe 为 Worldbench 的测试建议，不是 Epic 原文的要求，也不是已验证最低配置。最终 GPU 特性要求随渲染器确定；不默认强制硬件光追。首轮可建议在 Windows 11 测量，但首发操作系统和跨平台范围尚未定案。

候选交互指标：固定 1080p 视口，以 60 FPS 为目标、30 FPS 为重负载降级参考；对事先固定的局部笔刷与场景，记录编辑可见反馈的 p95 延迟，并以 100 ms 作为待讨论目标。必须明确笔刷范围、内容密度、加载状态、阴影与预览质量，不能只测静止相机或空场景。这些数字尚未由用户确认，也未实测达成。

来源：[Epic 硬件与软件规格](https://dev.epicgames.com/documentation/en-us/unreal-engine/hardware-and-software-specifications-for-unreal-engine)。

### 9.5 本次调查版本与来源

以下提交是调查锚点，不是已选依赖版本，也不一定是生产稳定版本。采用时需重新检查稳定发布、构建要求与兼容性。所有调查文件位于仓库外；仓库仍只保存本文。

| 项目 | 调查提交与官方来源 |
| --- | --- |
| Voxel Tools | [`a93bd7e76e808a934c7359095614dc737a147549`](https://github.com/Zylann/godot_voxel/tree/a93bd7e76e808a934c7359095614dc737a147549)；[VoxelLodTerrain API](https://voxel-tools.readthedocs.io/en/latest/api/VoxelLodTerrain/) |
| Terrain3D | [`185145cedadc93251934aba6f60625050ef155ce`](https://github.com/TokisanGames/Terrain3D/tree/185145cedadc93251934aba6f60625050ef155ce) |
| NAADF | [`3ea3bb6a836aa4a30277c055f812d65a9d7d6766`](https://github.com/cg-tuwien/NAADF/tree/3ea3bb6a836aa4a30277c055f812d65a9d7d6766) |
| Distant Horizons | [`6f2ddf22ea77ae3f5cca01cd9bbd21508c7ebc5c`](https://gitlab.com/distant-horizons-team/distant-horizons/-/tree/6f2ddf22ea77ae3f5cca01cd9bbd21508c7ebc5c)；许可见 `license_header.txt` 与 `LICENSE.LESSER.txt`，core 子模块需另锁版本 |
| BlueMap | [`7d80c0f04beee8d3d1b9fa5543c7d65ffa1515ca`](https://github.com/BlueMap-Minecraft/BlueMap/tree/7d80c0f04beee8d3d1b9fa5543c7d65ffa1515ca) |
| prismarine-viewer | [`8a3ce60b40f1ff7a26422a8f8904fbedbe808688`](https://github.com/PrismarineJS/prismarine-viewer/tree/8a3ce60b40f1ff7a26422a8f8904fbedbe808688) |
| vengi | [`0b126f37b9ea28451b8b5b17f501dd59f383d7ce`](https://github.com/vengi-voxel/vengi/tree/0b126f37b9ea28451b8b5b17f501dd59f383d7ce) |
| OpenVDB | [`c2d96f735ad88a7d8e0b0a79fdd1292327ba6d36`](https://github.com/AcademySoftwareFoundation/openvdb/tree/c2d96f735ad88a7d8e0b0a79fdd1292327ba6d36)；当前 README 说明已完成 MPL-2.0 到 Apache-2.0 的重新许可，不能沿用旧许可印象 |
| Chunky | [`bcbd162f7796e25f051d5e92bb05d8f84bea238b`](https://github.com/chunky-dev/chunky/tree/bcbd162f7796e25f051d5e92bb05d8f84bea238b) |
| Amulet-Core | [当前调查提交的 LICENSE](https://github.com/Amulet-Team/Amulet-Core/blob/21237069a71dda3c8c707c7fa4c6e0c60f8cbf14/LICENSE) |
| Amulet-Map-Editor | [当前调查提交的 LICENSE](https://github.com/Amulet-Team/Amulet-Map-Editor/blob/a3592ea2c5ccf0d250ab54f0aa4368fcd20246e5/LICENSE) |
| Voxy | [当前调查提交的 LICENSE.md](https://github.com/MCRcortex/voxy/blob/5bf33350a1cc7bb7e2b5d947b236c56d878b8a7d/LICENSE.md) |
| VoxelPluginFreeLegacy | [`7e64a89ce827b44a75c83a939e2c2e2e42bee61d`](https://github.com/VoxelPlugin/VoxelPluginFreeLegacy/tree/7e64a89ce827b44a75c83a939e2c2e2e42bee61d)；未确认可直接采用的开源许可 |

## 10. 建议推进顺序

以下是候选里程碑，不是已承诺排期。当前仅授权规划和文档维护，尚未授权开始这些实现阶段。30 个功能域的完整对齐是长期范围；本表按依赖分期，不能以 P1–P4 的核心功能替代全部清单。

| 阶段 | 范围 | 建议的通过条件 |
| --- | --- | --- |
| P0：规划收敛 | 体素渲染器/LOD、硬件与体验指标、完整功能树、版本框架和实验高度边界 | 逐项确定依赖、复用边界及可测量的原型目标；不删去高级功能 |
| P1：核心链路原型 | 8K/16K 代表场景、轻量 128K 范围浏览、局部修改、工程保存、1.20.1 小区域完整烘焙 | 工作集受控，交互可用，工程可恢复，游戏能正确加载；验证高度配置模型 |
| P2：基础编辑与地貌 | WP-01/02/04/06–13/21/26 的基础工作流，高度图实例、变换、混合、蒙版和噪波 | 笔刷、过滤、选区、主题、材质、保存撤销及局部更新可用；按子条目验收 |
| P3：图层、结构与道路 | WP-13–18 的完整图层/资源能力，直接建筑实例、节点道路 | 图层语义、植物/对象分布、跨块放置和道路连接正确 |
| P4：高级空间与复杂计算 | WP-03/19/20，洞穴资产识别/连接、侵蚀、ceiling、Master/Detail 与浮空维度 | 地表地下、多层与宏观/细节组合正确，实验项独立验收 |
| P5：格式与工作流对齐 | WP-22–29，多版本、导入/合并、完整后处理、脚本、插件及诊断设置 | 对应版本实际加载、互通样本和破坏性操作恢复测试通过 |
| P6：完整 parity 与规模收口 | 全部 A/C/E 条目、WB 增量及 8K/16K/128K 规模矩阵 | 每个条目有证据及差异说明；未完成项不得因阶段结束自动关闭 |
| 持续验证 | 代表性内容与较大范围导出 | 性能与正确性均有可复核记录 |

P1 优先验证 8K/16K 的实际工作流，同时用较轻量但真正覆盖 128K 坐标范围的工程验证流式加载与寻址。局部高复杂度和超高地形分别设场景，不把 128K 全域高复杂度烘焙作为所有早期工作的前置条件。

P5 是兼容性与完整工作流的收口，不表示 P1 前可以没有导出框架、P2 前可以没有工程保存、P3 前可以没有图层模型。规划时应先确定贯穿各阶段的数据契约。WP-30 的未接入/占位项按其状态评估，用户已明确要求的侵蚀等仍由 WB 增量承担。

不承诺当前尚未估算的人员、工期和预算。完成原型并确定各阶段验收边界后再评估。

## 11. 验证要求与主要风险

### 11.1 原型应记录的指标【具体阈值待定】

- 固定硬件和分辨率下的帧时间、卡顿分布及视口切换延迟。
- 不同笔刷范围和实例大小下的编辑反馈延迟。
- 冷启动、随机跳转和连续飞行时的加载表现。
- 内存、显存、缓存容量与磁盘读取峰值。
- 取消、重做、撤销和重开工程后的结果一致性。
- 代表性导出的每秒区块数、落盘大小、恢复行为和游戏内正确性。
- 标准高度、数据包扩展高度和模组扩展高度分别记录结果，不能以编辑器可显示代替游戏可加载。
- 对各渲染候选记录相同内容下的修改后重建、GPU 上传、透明/非立方方块表现和体素远近景切换；不使用平滑地形演示代替此测试。
- WP 清单按 5.9 验收：区分源码已接入、条件、Alpha、未接入与内部能力；所有实现完成状态须由测试证据支撑。

### 11.2 风险清单

| 风险 | 需要采取的验证或设计措施 |
| --- | --- |
| 大地图工作集失控 | 尽早验证 128K 范围的流式与预算机制 |
| 远景正确但局部内容成本过高 | 独立建立复杂建筑和洞穴的局部测试场景 |
| 多级细节与洞口、悬挑不兼容 | 验证转换边界与重要几何保留策略 |
| 地块接缝、侵蚀或道路依赖错误 | 明确运算邻域、阶段顺序及跨块验证 |
| 自动洞穴识别错误 | 保留人工校正与显式语义蒙版 |
| 预览与导出不一致 | 共享精确生成语义并进行结果比对 |
| Minecraft 版本兼容成本增长 | 先锁定一个版本并建立实际加载验证 |
| 超高工程超出目标游戏的坐标或存档边界 | 明确导出配置、数据包/模组依赖及实际加载验证 |
| 把作者演示或静态 LOD 性能当成动态编辑性能 | 使用统一场景与硬件测量修改后的可见反馈及峰值工作集 |
| 全量导出耗时或磁盘超限 | 代表性测量、容量估算和恢复机制 |
| 历史方案误导后续 LLM | 唯一文档、明确状态、更新当前正文、Git 留档 |
| 源码可见但无开源授权，或引擎与 GPL 许可不兼容 | 固定许可明确的提交并核对依赖与组合方式 |

## 12. 下一轮规划的优先决策

1. 是否采用 Godot + Voxel Tools 作为首要原型路线，以及 NAADF 对照实验的边界。
2. 已确认始终体素显示；下一步确定近景方块模型保真范围、体素 LOD 数据格式、聚合规则、切换/更新策略，以及 Distant Horizons 的参考或复用边界。
3. 超高世界第一阶段只做到 1.20.1 数据包可接受范围，还是同时规划超出该范围的配套模组；需要多大的总跨度和正 Y 高度。
4. 确认 9.4 的测试硬件、视口、交互阈值和首发系统；明确 8K/16K 代表场景的内容密度。
5. 选择首批额外导出版本、降级映射规则及具体开源协议；确定 WorldPainter 复用模块后再确定最终语言与依赖版本。
6. 按 WP-01–30 的完整清单建立阶段依赖、旧资产迁移方案及逐项验收样本；决定哪些能力复用源码、重新实现或通过适配器提供。数量和清单状态不替代运行验证。

## 13. 当前实际进度

| 项目 | 状态 |
| --- | --- |
| 项目命名 | 已确认：Worldbench |
| 单一开发文档约束 | 已确认，并在本文件中建立规则 |
| 需求与可行性分析 | 已更新：始终体素显示、参考 Distant Horizons LOD、WorldPainter 全功能对齐；保留 1.20.1、多版本、超高、8K/16K 优先和完整烘焙要求 |
| WorldPainter 功能审计 | 固定提交的功能级静态审计已落盘：30 个功能域、334 个子条目及 70 个表单归属；未运行 GUI 或逐项功能验证 |
| 开源与复用方向 | 已确认正式开源、不以盈利为目的、接受合规 GPL 复用 |
| 技术选型 | 已完成首轮候选调研，记录提交与许可；尚未定案或集成 |
| 1.20.1 高度边界 | 已核验官方服务端维度配置与字节码校验；尚未进行实际游戏加载验证 |
| 产品代码、原型与性能测试 | 尚未开始 |
| GitHub 私有仓库 | 已创建：`DanielFQZ/Worldbench`；私有；默认分支 `main` |
| 本地文件 | `E:\AppProject\Worldbench\DEVELOPMENT.md` |

## 14. 变更记录

| 日期 | 版本 | 内容 |
| --- | --- | --- |
| 2026-11-19 | 0.1 | 创建 Worldbench 私有仓库并建立唯一开发文档；收录用户需求、源码参考、可行性估算、候选架构与待决策事项。 |
| 2026-11-19 | 0.2 | 更新测试版本、多版本与实验高度需求、常见规模和烘焙定义；确认开源方向；加入开源组件调研、许可核查、建议硬件与 1.20.1 高度静态核验。 |
| 2026-11-19 | 0.3 | 确认体素显示与全功能对齐基准；加入固定提交的 WorldPainter 功能父子目录、源码状态、覆盖账及验收规则；修正平滑预览候选和路线图，保持唯一开发文档。 |
