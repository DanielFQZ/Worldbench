# Worldbench

Worldbench 是一个面向大型 Minecraft 世界的开源 3D 地形与资产编辑器。它把体素雕刻、实时预览、程序化实例和 Minecraft 导出放在同一个创作工作流中，目标是让 8K、16K 以及更大地图可以被持续编辑和预览。

## 当前版本

当前公开测试版本为 **0.26.4-pre.2**。Minecraft 资源与导出基线为 **1.20.1**。本版修复受保护安装目录的更新权限与回滚：自动选用安装器请求授权，可写目录仍使用 ZIP。旧客户端需一次手动安装覆盖原目录。保留快捷键录制与冲突处理、Shift+Space 资产库开关、方块网格线开关、工程视角保存和各向异性贴图过滤，保留静态更新清单与跨启动资源缓存。项目仍处于预发布阶段，工程文件格式、渲染性能和资产编辑器会继续演进。

## 已实现能力

- 体素地形的 3D 视口编辑，支持抬升、整平、挖除、填充、涂绘、平滑和选区操作
- 近景高精度方块网格与远景高保真地形预览
- 高度图地貌实例，支持位置、旋转、缩放、智能混合和实时变换预览
- PCG 地表材质、植被散点、道路网络和封闭结构资产
- Minecraft JAR 与资源包导入，方块纹理、模型和中文方块名称读取
- 建筑、植物、洞穴等结构资产的导入、资产库管理、缩略图和实例放置
- 天光、平行光、阴影、天空图、水体、反射和水下雾预览
- MCP 接口，可由支持 MCP 的 LLM 客户端读取和编辑工程
- Minecraft Java 1.20.1 地图导出
- GitHub Releases 更新检查和 Windows x64 直接更新

## 快速开始

1. 从 [Releases](https://github.com/DanielFQZ/Worldbench/releases) 下载 Windows x64 安装包。
2. 首次启动导入 Minecraft 1.20.1 客户端 JAR 或资源包；后续启动校验来源并恢复本地缓存，无需重复解压和解析。
3. 新建或打开 `.wbench` 工程，在 3D 视口中编辑地形和资产。
4. 使用“文件 → 导出 Minecraft 1.20.1”生成游戏存档。

测试步骤和当前限制见 [TESTING.md](TESTING.md)，完整路线与审计记录见 [DEVELOPMENT.md](DEVELOPMENT.md)。

## 构建

构建环境为 Windows x64、Visual Studio 2022、Qt 6.8.3 MSVC x64、CMake 3.24 或更高版本。构建前需要准备 Qt ShaderTools 和可用的 CMake/Ninja。

```powershell
powershell -ExecutionPolicy Bypass -File scripts/build.ps1 -Package
powershell -ExecutionPolicy Bypass -File scripts/package.ps1
```

`build.ps1 -Package` 会生成可运行目录，`package.ps1` 会生成 Windows x64 ZIP、Inno Setup 标准安装包、自签名证书和 SHA-256 校验文件。安装器支持自选安装目录、开始菜单/桌面快捷方式、`.wbench` 文件关联和标准卸载；卸载时可选择是否删除 `Documents\\Worldbench` 用户数据。自签名证书用于标识和完整性校验，Windows 初次运行时可能显示发布者未受信任。

## 许可

Worldbench 以 GNU GPL v3.0 发布，详见 [LICENSE](LICENSE)。第三方组件和资源的许可信息见 [licenses](licenses)。Minecraft JAR、资源包和用户资产不随项目分发。

---

# Worldbench (English)

Worldbench is an open-source 3D terrain and asset editor for large Minecraft worlds. It combines voxel sculpting, live preview, procedural instances, asset authoring, and Minecraft export in one workflow, with 8K, 16K, and larger worlds as the long-term target.

## Current version

The current public preview is **0.26.4-pre.2**. This build fixes updates in protected installation folders by selecting the installer with Windows authorization, and hardens ZIP preflight and rollback. Older clients need one manual installer run. It retains shortcut capture with conflict handling, Shift+Space asset-library access, a block-grid toggle, saved project cameras, and anisotropic texture filtering. Minecraft resource loading and export currently target **1.20.1**. The project is pre-release software and its project format, rendering performance, and asset editors will continue to evolve.

## Features

- 3D voxel terrain editing with raise, flatten, carve, fill, paint, smooth, and selection tools
- High-fidelity near terrain and distant terrain preview
- Heightmap terrain instances with transform controls, smart blending, and interactive previews
- PCG surface materials, vegetation scattering, road networks, and enclosed structure assets
- Minecraft JAR and resource-pack import with block textures, models, and localized block names
- Asset library with imported structures, thumbnails, folders, dependencies, and scene instances
- Preview lighting, shadows, sky images, water, reflections, and underwater fog
- MCP integration for LLM-assisted project inspection and editing
- Minecraft Java 1.20.1 world export
- GitHub Releases update checks and direct Windows x64 updates

## Getting started

1. Download the Windows x64 installer from [Releases](https://github.com/DanielFQZ/Worldbench/releases).
2. Import a Minecraft 1.20.1 client JAR or resource pack once. Subsequent launches validate the sources and restore a local cache without repeating archive extraction and parsing.
3. Create or open a `.wbench` project and edit the terrain and assets in the 3D viewport.
4. Use “File → Export Minecraft 1.20.1” to create a playable world.

See [TESTING.md](TESTING.md) for verification steps and current limitations. The audited roadmap is in [DEVELOPMENT.md](DEVELOPMENT.md).

## License

Worldbench is released under the GNU GPL v3.0. See [LICENSE](LICENSE). Third-party licenses are listed in [licenses](licenses). Minecraft JARs, resource packs, and user assets are not distributed with the project.
