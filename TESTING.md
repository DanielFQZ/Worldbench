# Worldbench 0.23.0 验收单 · BI

打开 [Worldbench 0.23.0](E:/AppProject/Worldbench/dist/Worldbench-0.23.0/Worldbench.exe)，在菜单中打开[示例工程](E:/AppProject/Worldbench/dist/Worldbench-0.23.0/examples/creation/creation-demo.wbench)并另存副本。观察纹理时从资源菜单载入 Java 1.20.1 客户端 JAR。分发构建的提交与校验值见 [build-info.json](E:/AppProject/Worldbench/dist/Worldbench-0.23.0/build-info.json)。

| 编号 | 操作 | 预期 |
| --- | --- | --- |
| BI01 视口透出 | 在编辑器后放一个亮色窗口。近看树冠和草地边缘，侧移镜头，再拉远。分别开启和关闭抗锯齿。 | 树叶缝隙只显示场景中的天空、树木和地形。草地轮廓不露出桌面或其他窗口。 |
| BI02 文件与文件夹 | 在资产库选一个文件夹。从资源管理器同时拖入一个结构文件和一个含两张灰度 PNG 的子目录。试一次 Ctrl+Z，再重做。 | 结构进入所选库目录，两张高度图保留子目录。一次撤销或重做覆盖整批。拖入不支持的文件会计入跳过数。 |
| BI03 共享依赖 | 在空工程中把示例目录的 tree.wbasset、landscape.wbpcg、forest-range.wbasset 一起拖入。重复导入图，再把森林范围拖进场景两次。在图中调整散布密度。 | 库中只有一份被引用的树和一张图。两次放置只增加范围，两处森林都响应图修改。改树的库名或移动目录后，保存重开仍能生成。 |
| BI04 跨工程与旧预设 | 从原工程导出范围预设，将 .wbasset 与旁边的 Worldbench.dependencies 一起复制到新目录，再导入空工程。也可直接导入 0.22.0 的旧森林预设。 | 空工程能补齐所需图与树，范围可放置。旧格式直接读取。已保存的工程可以单独移走再打开。 |
| BI05 缩略图 | 近看资产库的树皮、叶片或一格原木。加载会替换这些方块的资源包。 | 原木顶面能分辨年轮，侧面能分辨树皮。资源加载完成后，受影响的卡片采用当前贴图。 |
| BI06 近远精度 | 在 3K 工程副本中把树或建筑放在相隔几个区块的两处，调小偏好设置里的全精度范围。沿地面接近，再升高直到地形全部切换远景。 | 进入精确区的结构恢复方块细节。区外及高空使用简化结构。横跨精确区边缘的大建筑整件切换。打开资产编辑标签仍可看全精度。 |

## 实际参考图

树的卡片、文件夹和两张高度图来自 Windows 拖放测试。原木小图为加载原版 JAR 后实际生成的卡片。

![资产库拖入文件和目录](E:/AppProject/Worldbench/test-reference/0.23.0/asset-library.png)

![原木纹理缩略图](E:/AppProject/Worldbench/test-reference/0.23.0/oak-log.png)

以下为同一组树在近、远镜头下的实际视口截图。两帧提交的结构三角数分别为 16450 和 6160。

![近处树冠与地形](E:/AppProject/Worldbench/test-reference/0.23.0/near.png)

![远处森林和地形](E:/AppProject/Worldbench/test-reference/0.23.0/far.png)

## WorldPainter 相关进度

| 能力 | 本轮交付 | 验收项 |
| --- | --- | --- |
| WB-05.4 规则复用、WB-06.1/06.3 物体导入与预设、WB-07.2 植被资产 | 结构文件夹导入，共享结构与图，范围跨工程复用，真实材质卡片 | BI02–BI05 |
| WB-12.3/12.4 网格与远景预览 | 结构与地形同步切换精度，修复画面合成 | BI01、BI06 |

8K／16K 全流程性能认证、多版本导出和生态群落规则仍在开发规划中。本轮等你按 BI 编号反馈。
