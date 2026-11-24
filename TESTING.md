# Worldbench 0.26.1 验收单 · BM

[启动 Worldbench](E:/AppProject/Worldbench/dist/Worldbench-0.26.1/Worldbench.exe) · [开发文档](DEVELOPMENT.md) · [构建信息](E:/AppProject/Worldbench/dist/Worldbench-0.26.1/build-info.json)

本轮只验收两项反馈。测试工程使用 `C:/Users/38683/Downloads/测试工程.wbench` 的只读副本。

| 编号 | 操作 | 预期 |
| --- | --- | --- |
| BM01 封闭结构接地 | 在远景和近景之间靠近一个封闭结构，观察结构底部与地形接触处，再拖动结构。 | 结构使用自己的网格或 LOD，地形不会在结构边缘额外生成一圈全精度方块。洞口、地基和地表衔接连续。 |
| BM02 地貌实例拖动 | 选中高度图实例，按 **W**，连续横向拖动 20–50 次，观察拖动过程和松手后的收敛。 | 鼠标移动不等待完整创作生成。拖动时使用低精度地形预览，视口保持可操作。松手后只执行一次最终地形与资产接触校验。 |
| BM03 取消地貌拖动 | 拖动高度图实例后按 **Esc** 或取消操作。 | 地貌实例恢复到拖动前的位置，网格回到稳定状态，撤销历史不增加空操作。 |
| BM04 结构回归 | 移动普通建筑和岩洞，观察近景、远景、洞口和撤销重做。 | 本轮接地修复不影响独立结构的快速平移和局部洞口更新。 |

### 自动化证据

`CreationUiTests --terrain-latency` 在最新工程副本上执行 48 次地貌实例变换。每次 `setTerrainOption` 的最高同步耗时为 0.99 ms，过程中保留已有程序化生成快照，并发布或排队低精度预览。结果见 [terrain-latency.json](test-reference/0.26.1/terrain-latency.json)。

`CreationUiTests --placement-latency` 继续执行普通建筑、近景建筑、岩洞和两倍尺寸建筑的位移回归。参考图见 [placements.png](test-reference/0.26.1/placements.png)。分发包打开 3K 工程的截图见 [package-smoke.png](test-reference/0.26.1/package-smoke.png)。

本轮实现从旧版地貌拖动路径恢复的交互行为。鼠标移动期间不构建精确方块区块，也不把生成对象的几何补片放到远景边界。结构与地形接触的完整校验保留在松手阶段。

### WorldPainter 相关进度

| 开发条目 | 本轮验收范围 |
| --- | --- |
| WB-06.3／06.4 结构放置、地基与碰撞 | BM01、BM04 检查接地边缘与结构移动。 |
| WB-08.2–08.4 洞室、入口和地下放置 | BM01、BM04 核对封闭结构接触和洞口显示。 |
| WB-12.3／12.4 近远网格 | BM01、BM02 检查远景 LOD、交互预览和精确区边界。 |

本轮集中修复封闭结构接地重复体素和地貌实例拖动卡顿。8K／16K 复杂地图的完整编辑与烘焙性能仍待认证。
