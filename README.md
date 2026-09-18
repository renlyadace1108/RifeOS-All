# RifeOS

> **极致轻量、亚像素美学与双 Arena 微内核极简桌面操作系统**  
> *Crafted with passion by Renly (陈俊易)*

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
   - **双向流体吞吐（Two-Way Fluid Morphing）**：底栏点击九宫格抽屉，顶部流体云向下拉展为 1/3 视口液态抽屉；应用启闭以流体云微球为物理原点延展与吞噬湮灭。
   - **修长底座与自由窗口**：44px 修长毛玻璃 Dock、6px 边缘暗晶箭头悬停提示、贴顶 Aero Snap 与 Windows 系统级全屏联动。

---

## 🛠️ 构建与交付规范 (Build & Delivery)

- **语言标准**：C11 / C99 标准纯 C 语言。
- **运行环境**：原生 Win32 GDI / DWM API，零外部三方动态库依赖。
- **编译工具链**：Visual Studio 2022 / 2026 + MSVC (`cl.exe`) + CMake + Ninja。
- **交付目标**：单文件便携式 EXE（`/MT` 静态 C 运行时库链接，零依赖、无 `vcruntime140.dll` 缺失风险）。

### 构建命令 (CMake + Ninja)

```powershell
# 配置 Release 预设
cmake --preset x64-release

# 编译生成单文件便携 EXE
cmake --build --preset x64-release
```

编译产物位于：`out/build/x64-release/RIFEOS.exe`

---

## 📜 许可证 (License)

Made with ❤️ by Renly. All Rights Reserved.
