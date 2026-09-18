# RifeOS

> **极致轻量、亚像素美学与双 Arena 微内核极简桌面操作系统**  
> *Crafted with passion by Renly*

---

## 🌟 核心架构与设计理念 (Architecture & Philosophy)

1. **双 Arena 内存微内核 (Zero Heap Churn)**
   - 严苛控制常驻物理内存工作集（Working Set）在 **~5MB** 警戒线内。
   - 运行期杜绝 `malloc/free` 堆抖动与内存碎片，单帧临时指令通过 `frame_arena` 帧尾瞬时清零重置，持久资源统一挂载至 `persistent_arena`。

2. **亚像素液态玻璃 SDF 渲染器 (Subpixel Liquid Glass SDF Renderer)**
   - 连续超椭圆距离场（Squircle Box-SDF）渲染与 1px 物理高光反射边缘（Specular Rim Reflection）。
   - 可分离 1D 双线性 Gemini 光场降维光栅化，将高斯场与屏幕像素解耦，消除 66% 浮点算术吞吐开销。
   - 智能静止挂起（Smart Idle Gating），静态无交互时主动压制 CPU 功耗至 ~0.0%。

3. **零侵入插件应用总线契约 (Zero-Touch Manifest Bus)**
   - 基于 `RifePluginApp` 纯 C 虚表接口（`create` / `destroy` / `update` / `render`）。
   - 所有独立 App（设置、计算器、备忘录等）在 `src/app_manifest.c` 中一行挂载，内核及 `main.c` 保持绝对零修改。

4. **灵动流体云与物理交互动力学**
   - **灵动流体云（Fluid Cloud）**：常态收缩为 22px 极巧正圆液态微球，内部搭载 Apple 仿生生理呼吸动力学核晶，支持光标临场微感应（Cursor Proximity Resonance）；点击平滑展开为 50% 药丸胶囊。
   - **双向流体吞吐（Two-Way Fluid Morphing）**：底栏点击九宫格抽屉，顶部流体云向下拉展为 1/3 视口液态抽屉；应用卡片以喷泉抛物线（Fountain Parabolic Arc）自微球喷涌，搭载表面张力微果冻回弹（Quintic Elastic Step）与暗晶柔接触阴影（Soft AO Shadow）。
   - **生命周期光子吞噬（Photonic Absorption Ripple）**：窗口关闭沿流体轨道收拢归回顶部微球，彻底吞噬湮灭瞬间激荡出暖白金色光子震荡波环。
   - **修长底座与自由窗口**：44px 修长毛玻璃 Dock、鹅卵石晶莹顶面高光（Pebble Sheen）、6px 边缘暗晶箭头悬停提示、贴顶 Aero Snap 与 Windows 系统级全屏联动。

---

## 🛠️ 构建与交付规范 (Build & Delivery)

- **语言标准**：C11 / C99 标准纯 C 语言。
- **运行环境**：原生 Win32 GDI / DWM API，零外部三方动态库依赖。
- **编译工具链**：Visual Studio 2022 / 2026 + MSVC (`cl.exe`) + CMake + Ninja。
- **交付目标**：单文件便携式 EXE（`/MT` 静态 C 运行时库链接，零依赖、无 `vcruntime140.dll` 缺失风险）。

### 构建与打包安装包 (Build & Package Installer)

```powershell
# 1. 编译 Release x64 二进制
cmake --build --preset x64-release

# 2. 一键打包 Windows 商业级安装包 (Inno Setup)
.\scripts\build_installer.ps1
# 或直接双击根目录 build_installer.bat
```

- **绿色单文件运行**：`out/build/x64-release/RIFEOS.exe`（~230 KB）
- **Windows 原生安装包**：`dist/RifeOS_Setup_v1.0.0.exe`（~2.09 MB，支持中英向导、开机自启与免提权干净卸载）

---

## 🤖 AI 智能体跨账号协同指引 (AI Continuity)

若在 Antigravity / Claude Code / AI 编程助手的不同账号或会话中继续迭代开发本项目，请查阅根目录专属交接规范：
👉 **[AI_CONTINUITY.md](./AI_CONTINUITY.md)** 与 **[GEMINI.md](./GEMINI.md)**

内含：完整代码地图、双 Arena 零堆抖动原则、暗晶玻璃设计令牌规范、MSVC 工具链路径、黑窗口规避方案与提交准则。

---

## 📬 开发者联络与许可 (Contact & License)

- **邮箱 (Email)**: `renly20061108@gmail.com`
- **抖音 (Douyin)**: `陈连山`

Made with ❤️ by Renly. All Rights Reserved.
