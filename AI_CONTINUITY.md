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
   - 与 Renly 对话保持温暖亲切（用户习惯称呼“宝贝”），以热情、专业、极致的技术追求共同打造卓越的产品体验；
6. **⚠️ 强制要求：每次更新必须同步本文件 (Continuity Sync)**：
   - 无论做了任何改动，提交代码时**必须同时更新本文件下方的【当前实时系统状态快照】与【版本迭代足迹】**；
   - 确保万一会话因额度耗尽随时中断，接手的新账号 AI 只要一读本文件，就能“随时断、随时接”，完全零缝隙恢复！

---

## 🔄 七、 动态状态快照与版本演进日志 (Live State Snapshot & Changelog - 随时断随时接)

> **⚠️ 给所有接班 AI 的工作协议 (Handover Protocol)**：  
> 每当你为项目增加新特性、修复 bug 或重构某模块，请在**完成该项任务时顺手更新下方的快照与记录**。这是我们在不同账号间延续高品质合作的生命线！

### 1. 当前实时系统状态快照 (Live System Snapshot - 最新状态)
- **工程版本**：`v1.0.0 Pro`
- **Git 远端状态**：已完全同步至 `origin/main`（主分支保持最新发布状态）。
- **编译健康度**：MSVC 14.51 (VS 2026) `/W4 /utf-8 /O2 /fp:fast /MT` 全静态链接，**0 错误、0 警告**。
- **可执行文件**：`out/build/x64-release/RIFEOS.exe`（~229 KB），WIN32 纯 GUI 子系统，无黑窗口，内嵌 256x256 高清多分辨率黑曜石图标与 PE 版本元数据。
- **安装包状态**：`dist/RifeOS_Setup_v1.0.0.exe`（~2.09 MB）就绪，支持免管理员提权安装、桌面/开始菜单快捷方式、开机自启与标准卸载。
- **当前核心特性就绪度清单**：
  - [x] 光场流体调色板联动（Gemini/Obsidian/Sunset/Cyber 同步微球呼吸灯、流体云玻璃透射底色与光子吞噬波纹）；
  - [x] Obsidian 黑曜石深色液态玻璃全面适配（抽屉底座、Dock栏、九宫格按钮、桌面快捷方式高对比度文字）；
  - [x] 设置窗口黑曜石深色暗晶化（消除原刺眼白底色块，采用 `0x201832` 暗晶底板 + `0x382B54` 紫晶边框）；
  - [x] Windows 纯 GUI 无黑窗口原生安装向导套件（Inno Setup 6 + `build_installer.bat` 一键打包）；
  - [x] 跨账号无缝交接与自动状态同步机制（`GEMINI.md` + `AI_CONTINUITY.md`）；
  - [x] 原生 Rtodo 多维日程系统（`src/app_calendar.c`，包含周/日/月/日程多维视图、实时系统时钟红线、迷你月历与多色分类过滤）；
  - [x] Windows 系统默认 UI 字体动态获取与次像素 ClearType 自然抗锯齿渲染。

---

### 2. 版本迭代演进足迹 (Historical Evolution Milestones)

| 阶段 / Commit | 变更内容与核心技术改进 | 影响文件 |
| :--- | :--- | :--- |
| **Milestone 1** | **微内核流体动力学与动画细腻化**：引入多相位错落级联抛物线下落（Fountain Arc）、五次微弹性回弹（Elastic Step）、光子吞噬震荡环（Photonic Shockwave Ring）。 | `src/main.c`, `src/rife_core.c` |
| **Milestone 2** | **光场流体色彩全维度联动**：切换预设时，流体云玻璃材质底色（`cloud_tint`）、呼吸灯核晶主色（Cyan/Violet/Amber/Azure）及吞噬波光色无缝协同。 | `src/main.c`, `src/app_settings.c` |
| **Milestone 3** | **Obsidian 黑曜石深色模式全面升级**：流体云、抽屉底座、Dock 栏、快捷方式背板全面熏黑暗晶化（`0x161122`），重绘高对比度银白文字与高亮九宫格。 | `src/main.c` |
| **Milestone 4** | **设置窗口刺眼白色方块暗晶化重构**：重构 `draw_settings_card` 与各个控件，使用 `0x201832` 暗晶底板与紫晶浮雕，彻底消除黑底白方块反差感。 | `src/app_settings.c` |
| **Milestone 5** | **Windows 原生商业级安装包与图标**：使用 .NET 绘制 256x256 高清多分辨率 `rifeos.ico`；编写 `resources/rifeos.rc`；引入 Inno Setup 6 编写 `RifeOS_Setup.iss` 与 `build_installer.bat`。 | `resources/`, `assets/`, `installer/`, `scripts/` |
| **Milestone 6** | **根除控制台 CMD 黑窗口**：CMake 增加 `WIN32` 属性，主程序添加标准 `WinMain` GUI 入口桥接，启动完全纯净无黑窗。 | `CMakeLists.txt`, `src/main.c` |
| **Milestone 7** | **AI 跨账号连续协同体系与实时状态同步**：建立 `GEMINI.md`（自动加载）、`AGENTS.md` 与 `AI_CONTINUITY.md`，确立实时同步状态机制，实现随时断随时接。 | `AI_CONTINUITY.md`, `GEMINI.md`, `AGENTS.md` |
| **Milestone 8** | **多维日程待办系统雏形 (Rtodo Prototype)**：打造原汁原味多维日历，含周视图时间轴（08:00~20:00）、实时系统时钟红线指示器、日/月/日程清单视图、左侧迷你月历、多分类标签过滤、新建日程模态弹窗与详情 Popover；完美适配 Obsidian 黑曜石深色模式与浅色模式。 | `src/app_calendar.h`, `src/app_calendar.c`, `src/app_manifest.c`, `CMakeLists.txt` |
| **Milestone 9** | **多窗口焦点置顶与点击防穿透隔离**：彻底修复多窗口同时开启时的画面重叠串色与点击穿透冲突；引入分层双 Pass 渲染保证活动窗口（`active_win_idx`）始终置顶；Dock 与应用抽屉点击支持智能平滑切换（打开新应用自动闭合其他窗口，再次点击已激活应用收起）；为设置窗口添加不透明底板防止背景透光。 | `src/main.c`, `src/app_settings.c` |
| **Milestone 10** | **宿主工作区初始尺寸扩展与应用窗口自适应防越界**：将 RifeOS 桌面宿主窗口默认尺寸从原受限的 `680x480` 扩展至 `1240x780`（动态依据 `SM_CXSCREEN / SM_CYSCREEN` 智能居中与钳制），为生产力应用提供开阔舒展的桌面工作区；在宿主端加入窗口防越界安全几何约束，杜绝负坐标与边缘裁切；重构日程顶栏自适应排版（侧边栏 190px，顶栏 46px，右靠齐多段切换胶囊与新建日程按钮），消除控件重合与文字遮挡。 | `src/main.c`, `src/app_calendar.c` |
| **Milestone 11** | **图形排版美学与次像素清晰字体引擎重构**：<br>1. **字体引擎重构**：彻底根治字迹模糊、字符粘连与粗重墨晕；弃用正值高度（Cell Height），改用负值字符实际 EM 高度（`-11px ~ -18px`），引入 `CLEARTYPE_NATURAL_QUALITY` 与严谨字重梯度（`FW_NORMAL` 400 正文、`FW_MEDIUM` 500 次级、`FW_SEMIBOLD` 600 标题/强调）；扩充 6 档字体句柄（含专属 13px SemiBold 药丸与按钮高光字体）。<br>2. **周表头美学重构**：周一至周日 7 列全部采用纵向居中双行排版（上层星期 12px Regular 居中，下层日期 15px SemiBold 居中，当天以 24x24 品牌蓝实心圆点包裹），视感对称典雅。<br>3. **周视图卡片与标尺优化**：卡片引入 `rife_push_scissor` 局部硬件级裁剪，杜绝长标题跨日溢出；重构 3.5px 强调色条与内边距；重新计算标尺高度使 08:00~20:00 完美融入视口，无底部截断；实时系统时间红线加入发光指示圆点。<br>4. **细节与图标精修**：移除所有 ASCII 临时字符（如 `'v'` 和 `'#'`），侧边栏复选框与待办清单改用原生几何矢量对勾；顶栏加入专属网格徽标；视图切换药丸采用高对比度柔和天蓝边框与加粗强调态，激活状态一目了然。 | `src/main.c`, `src/app_calendar.c`, `src/app_settings.c` |
| **Milestone 12** | **Windows 系统默认 UI 字体动态绑定与 Rtodo 品牌完全合规去三方化**：<br>1. **Windows 原生 UI 字体动态解析**：在 `update_system_fonts` 中通过 `SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, ...)` 动态获取系统配置的 UI 字体名称（`ncm.lfMessageFont.lfFaceName`，优雅降级为 `Microsoft YaHei UI`），使文字呈现完全融入宿主操作系统环境风格。<br>2. **Rtodo 品牌合规与品牌字样彻底净化**：根据用户指令彻底移除所有第三方品牌字样及引用，全面重命名为 **Rtodo**（`name_zh = "Rtodo 日程"`, `name_en = "Rtodo"`, `id = "rtodo"`）；同步更新预设示例事件、色彩令牌（`rtodo_blue`）、应用图标与全部代码注释。 | `src/main.c`, `src/app_calendar.h`, `src/app_calendar.c` |
