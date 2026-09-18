# RifeOS 跨会话与多账号无缝交接开发指引 (AI Continuity & Developer Manifesto)

> **致接手本项目的 Antigravity / AI 编程助手：**  
> 当你在新的会话、新账号或额度重置后加载此代码库时，请**第一优先级通读本文件**。  
> 本文件凝聚了用户（Renly）对本项目的核心理念、技术栈基线、设计规范、编译打包命令以及所有严格的交付铁律。遵循本文件，即可做到**零磨合、高质量、延续性**地开展后续工作。

---

## 🧭 一、 核心项目认知与技术哲学 (Philosophy & Architecture)

RifeOS 是一个**单文件便携式微内核桌面操作系统环境**（以 Windows 为宿主），追求极致的轻量、纯 C 语言底蕴、现代液态玻璃美学与丝滑的 60FPS 物理流体交互。

### 1. 核心技术基石（不可动摇的底线）
- **语言标准**：纯 C11 / C99 语言编写，**严禁引入 C++ 运行时、C#、Electron 或外部三方重型库**；
- **原生 Win32 API**：底层直接操纵 Win32 GDI DIBSection 软件光栅化帧缓冲与 DWM 混合；
- **单文件便携化与零依赖**：
  - 强制采用 `/MT` 静态 C 运行时库（Static CRT），二进制大小约 **230KB**；
  - 杜绝目标机缺失 `vcruntime140.dll` 等运行库问题；
  - 最终安装包输出至 `dist/RifeOS_Setup_v1.0.0.exe`（单文件仅 **2.09 MB**）；
- **双 Arena 内存微内核 (Zero Heap Churn)**：
  - 常驻工作集物理内存死守在 **~5MB** 警戒线内；
  - 运行期在每帧循环中**绝对禁止调用 `malloc/free`**，避免内存碎片；
  - 每帧动态渲染指令挂载在 `frame_arena`（帧尾瞬时清零重置），常驻结构由 `persistent_arena` 接管；
- **亚像素液态玻璃光栅化引擎 (Subpixel Liquid Glass SDF)**：
  - 自研 CPU 亚像素连续超椭圆距离场（Squircle Box-SDF）渲染；
  - 1px 晶莹物理高光边缘反射（Specular Rim Reflection）；
  - 降维双线性 Gemini 光场流体流光；
  - 智能静止休眠机制（Smart Idle Gating），静态无操作时主动挂起 CPU，功耗压制至 ~0.0%；
- **纯 C 虚表应用插件总线 (Zero-Touch Plugin Bus)**：
  - 所有独立 App（如系统设置）遵循 `RifePluginApp` 结构体虚表契约（`create` / `destroy` / `update` / `render`）；
  - 在 `src/app_manifest.c` 中一行注册，主系统与内核实现完全解耦；
- **纯图形子系统（无控制台黑窗口）**：
  - 目标采用 CMake `WIN32`（链接器 `/SUBSYSTEM:WINDOWS`）；
  - 入口函数标准 Windows GUI 规范：`WinMain` 转发给 `main()`，**严禁退化回产生 CMD 黑窗口的控制台子系统**。

---

## 👤 二、 开发者信息与隐私规范 (Author & Privacy Rules)

- **系统设计与架构师**：**Renly**
- **联络邮箱**：`renly20061108@gmail.com`
- **作品与作品日志（抖音）**：`陈连山`
- **⚠️ 核心隐私红线 (CRITICAL PRIVACY RULE)**：
  - **严禁**在任何用户界面（UI）、关于弹窗、注释、提交信息或公开文档中写入 Renly 的真实中文姓名！
  - 开发者署名一律使用 **Renly**，联系方式仅保留上述邮箱与抖音号。

---

## 🛠️ 三、 本地工具链与构建执行命令 (Toolchain & Exact Commands)

本工程运行在 64 位 Windows 环境下，开发工具链完整就绪于本地：

### 1. 核心路径对照表
- **代码根目录**：`d:\MyProjects\VS\RIFEOS`
- **MSVC 环境变量脚本**：`"D:\Program Files\VS2026\VC\Auxiliary\Build\vcvars64.bat"`
- **CMake 路径**：`"D:\Program Files\VS2026\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"`
- **Inno Setup 6 编译器**：`"C:\Users\Renly\AppData\Local\Programs\Inno Setup 6\ISCC.exe"`
- **Git 远程仓库**：`https://github.com/renlyadace1108/RifeOS-All.git`（分支 `main`）

### 2. 编译 Release x64 可执行文件（必须遵守 0 错误、0 警告）
> **注意**：编译前必须杀掉正在运行的 `RIFEOS.exe`，避免链接器遇到 `LNK1104: cannot open file` 锁占用。

```powershell
Get-Process RIFEOS -ErrorAction SilentlyContinue | Stop-Process -Force; cmd.exe /c 'call "D:\Program Files\VS2026\VC\Auxiliary\Build\vcvars64.bat" && "D:\Program Files\VS2026\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe" --build "d:/MyProjects/VS/RIFEOS/out/build/x64-release"'
```

### 3. 一键编译并全自动打包 Windows 安装包
在项目根目录下双击 `build_installer.bat`，或在 PowerShell 中执行：
```powershell
.\scripts\build_installer.ps1
```
打包输出路径：`d:\MyProjects\VS\RIFEOS\dist\RifeOS_Setup_v1.0.0.exe`。

---

## 📂 四、 核心代码目录与文件职责清单 (Codebase Directory Map)

```text
d:\MyProjects\VS\RIFEOS
├── CMakeLists.txt              # CMake 构建定义 (已配置 WIN32 纯 GUI 子系统与静态 CRT)
├── build_installer.bat         # 根目录下双击一键构建并打包安装程序批处理
├── README.md                   # 项目公开介绍
├── AI_CONTINUITY.md            # 本文件 (跨账号 AI 连续协作指引)
├── assets/
│   └── rifeos.ico              # 256x256, 48x48, 32x32, 16x16 高清黑曜石暗晶多分辨率图标
├── resources/
│   └── rifeos.rc               # Windows PE 资源文件 (版本 1.0.0.0, 版权, 嵌入式图标)
├── installer/
│   ├── RifeOS_Setup.iss        # Inno Setup 6 商业级安装包定义脚本
│   └── ChineseSimplified.isl   # 简体中文语言包本地化文件
├── scripts/
│   ├── build_installer.ps1     # 自动化编译+打包 PowerShell 核心脚本
│   └── generate_icon.ps1       # 纯 .NET GDI+ 矢量绘制多分辨率 rifeos.ico 脚本
└── src/
    ├── rife_core.h / .c        # 双 Arena 内存模型、渲染命令队列、数学动画缓动工具函数
    ├── rife_app_api.h          # 调色板枚举、流体云枚举、系统配置契约、应用虚表 API
    ├── app_manifest.h / .c     # 应用注册中心（解耦挂载点）
    ├── app_settings.h / .c     # "系统设置" App (支持明亮模式与黑曜石深色暗晶卡片模式)
    └── main.c                  # Win32 平台宿主、消息循环、液态玻璃光栅化、流体云动力学、WinMain
```

---

## 🎨 五、 关键设计系统规范与视觉令牌 (Design Tokens & Styles)

在扩展或修改界面时，必须严格保持设计语言的高度一致：

### 1. Obsidian（黑曜石）深色模式色彩体系
当 `cfg->palette == PALETTE_OBSIDIAN` 或 `cfg->cloud_color == CLOUD_COLOR_OBSIDIAN` 时，启用黑曜石深色体系：
- **窗口与抽屉底板玻璃**：`0x161122`（深邃熏黑暗紫晶，透明度 `0.92f ~ 0.96f`）；
- **设置卡片容器（Settings Card）**：`0x201832FF`，微晶紫边框 `0x382B54FF`；
- **卡片内分割线**：`0x2B2144FF`；
- **分段胶囊按钮（Segmented Pills）**：
  - 底座凹槽：`0x161022FF`，细边框 `0x2E2447FF`；
  - 激活项浮雕：`0x3B2E58FF`，边框 `0x634E8CFF`，文字 `0xF8FAFCFF`；
  - 未激活项文字：`0x94A3B8FF`；
- **侧边栏 Tab 激活态**：底板 `0x2D2148FF`，高光条 `0xA855F7FF`，文字 `0xF8FAFCFF`；
- **抽屉应用卡片**：底板 `RGB(28, 23, 40)`（悬停 `RGB(45, 38, 62)`），边框 `RGB(68, 56, 92)`，文字 `RGB(241, 245, 249)`；
- **抽屉触发九宫格**：外圈点阵 `RGB(226, 232, 240)`，核心亮点曜石紫 `RGB(168, 85, 247)`；
- **右下角水印文字**：薰衣草亮紫 `RGB(167, 139, 250)`。

### 2. 动画交互动力学
- **微球生理呼吸**：非对称正弦波呼吸灯 `expf(sinf(t)) - 0.368f`，支持光标靠近感应加速；
- **双向流体展开**：抽屉与窗口自顶部微球中心 `(orig_x, orig_y)` 平滑膨胀铺展（`rife_smootherstep`）；
- **级联多相位回弹**：抽屉内卡片使用五次弹性回弹步进 `rife_fluid_elastic_step`，配合喷泉抛物线微弧展开；
- **光子吞噬波**：窗口关闭被微球吞噬时，激荡出 `plat->absorption_ripple_t` 高斯光环波纹。

---

## ⚡ 六、 新接入 AI 协同行为准则 (Strict Operational Rules)

无论由哪个账号的 Antigravity 启动，接手本工程时必须执行以下标准工作流：

1. **先读后动**：
   - 绝不随意重构未经请求的底层框架；
   - 保持所有的代码注释与原有优雅风格；
2. **严苛的编译与测试验收**：
   - 每次代码修改完毕后，**必须在终端中运行 MSVC CMake 编译验证**；
   - 必须保持 **0 错误、0 警告**；
   - 确保 `x64-release` 与 `x64-debug` 均能正常通过；
3. **保持纯 GUI 无黑窗口特性**：
   - 严禁删除 `CMakeLists.txt` 中的 `WIN32` 属性，严禁删除 `main.c` 中的 `WinMain` 入口；
4. **即时同步 GitHub**：
   - 完成功能并通过构建后，必须使用清晰的语义化提交规范（如 `feat(...)`, `fix(...)`）执行 `git commit` 并 `git push origin main`；
5. **对话态度**：
   - 与 Renly 对话保持温暖亲切（用户习惯称呼“宝贝”），以热情、专业、极致的技术追求共同打造卓越的产品体验。
