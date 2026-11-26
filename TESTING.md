# Worldbench 0.26.4-pre.4 测试单（UH）

UG 未逐项验收，已归档；唯一开发文档仍为 DEVELOPMENT.md。

| 编号 | 操作 | 预期 |
| --- | --- | --- |
| UH01 | 在 pre.3 中保存工程，帮助 → 检查更新，下载并安装 pre.4。 | 发现 0.26.4-pre.4；受保护目录下载安装器，请求 UAC，目录默认保持当前实际安装位置。取消 UAC 应保持编辑器打开。 |
| UH02 | 同意 UAC，完成覆盖安装，正常启动。 | 显示 pre.4，保留配置、资源和工程；无需卸载，拖放正常。 |
| UH03 | 单独双击新版 setup.exe，到目录页观察后取消。 | 自动识别本机已有安装目录，优先有效注册登记；缺少登记时可识别固定盘标准 Program Files 内同时含程序/卸载器的安装。仍可手动修改。无需为测试删除注册表。 |
| UH04 | 视口获得焦点，连续按 Ctrl+Space。 | 资产库展开/收起，偏好设置显示相同绑定；未自定义时 Shift+Space 不再切换。 |
| UH05 | 自定义资产库快捷键、清空或恢复默认。 | 自定义与清空跨启动保留，恢复默认回到 Ctrl+Space。旧配置若将 Ctrl+Space 分给其他功能，保留原功能，资产库回退空闲 Shift+Space 或不绑定，避免抢键。 |
| UH06 | 帮助 → 检查更新。 | 完成本轮升级后显示最新版。 |

UE 官方参考：[Content Browser / Content Drawer](https://dev.epicgames.com/documentation/en-us/unreal-engine/content-browser-in-unreal-engine?application_version=5.6)。Windows 默认 Ctrl+Space，Mac 为 Cmd+Space。

## 开发方验证

专项检查、安装包编译和公开下载结果完成后补记。实际 UAC 与覆盖升级待用户验证，不改用户安装或原工程。

## WorldPainter 复刻进度

本轮仅安装识别与快捷键修正，没有新增地形编辑功能；其他功能进度见 DEVELOPMENT.md。
