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
  - [x] 原生 Rtodo 多维日程系统（`src/app_calendar.c`，严格确立为 Rtodo 品牌，提供通透轻盈美学布局、周/日/月/日程多维视图、实时系统时钟红线、迷你月历与多色分类过滤）；
  - [x] 亚像素窗口圆角视口安全裁剪引擎（`rife_push_scissor_round` 复合裁切防直角溢出）；
  - [x] Windows 系统默认 UI 字体动态获取与次像素 ClearType 自然抗锯齿渲染；
  - [x] 窗口展开 1 秒抖动彻底根除引擎（整数栅格量化、临界 snap 截断与固定目标排版+硬件圆角裁切过渡）；
  - [x] 飞书原生质感极简日程界面对齐（周日首列、21px 加粗日期标头、右下角蓝底悬浮 FAB 按钮、通透顶栏）；
  - [x] 舒展大时间网格与纵向平滑滚动引擎（60px 大小时格、24小时全天候、顶栏固定、原生鼠标滚轮驱动与微动滚动条）；
  - [x] 全界面液态玻璃沉浸化（彻底移除实心纯白底板，支持亚像素 Alpha 混合与全透光流体质感）；
  - [x] 全局纤细轻量字体体系（FW_LIGHT 300 / FW_NORMAL 400，消除粗重墨晕感）；
  - [x] 时间标尺防碰撞药丸胶囊徽标（整点交界高饱和红色药丸，彻底杜绝字形穿透）；
  - [x] 彻底移除预设药丸与全量用户自定义日程体系（标题、地点、备注文本输入框、焦点光圈、打字与光标引擎、UTF-8 边界安全退格、Ctrl+V 剪贴板粘贴）；
  - [x] 用户自定义分类标签系统（`CustomTag` 动态增删、6 档晶莹色彩微调、侧边栏分类动态遍历勾选与快捷新建）；
  - [x] 复合原子化零堆持久化引擎（`rtodo_data.bin` 包含自定义标签与日程完整存储，初次启动 0 假数据干净纯粹）；
  - [x] 系统级与应用级双轨工业级持久化安全防护（统一基于 `%APPDATA%\RifeOS` 标准路径，支持便携模式兼容，权限安全防 UAC 拦截，软件升级安装永不丢配置与数据）；
  - [x] Rtodo 侧边栏“标签”重命名与全生命周期增删管理（侧边栏精致 `×` 删除图标，数据自动迁移与自愈持久化）；
  - [x] 15 分钟为单位的细分时间轴与网格吸附引擎（4 分割辅助线、`:00`/`:15`/`:30`/`:45` 精准吸附、15分/30分/1小时/2小时时长、15分钟循环步进）；
  - [x] Ctrl + 鼠标滚轮平滑缩放时间轴引擎（44px~240px 自由缩放，以鼠标时间点为锚点 Zoom-to-Mouse-Anchor，松开 Ctrl 保持垂直滚动）；
  - [x] 1 分钟原子级最小时间单位系统（0-59 分钟原子级吸附、`[-] HH [+] : [-] MM [+]` 独立微调、`1分/15分/30分/1h` 极短微任务支持、瑞士钟表级 5 分钟与 1 分钟分层刻度标尺、最大 480px/h 超级微距缩放）；
  - [x] Rtodo 侧边栏全功能实时搜索与浮层穿梭导航系统（多维度 UTF-8 / ASCII 大小写不敏感即时匹配、视图全量联动过滤、340px 晶莹液态玻璃搜索结果浮层、一键日期跳转与时间轴平滑自动滚动物理吸附）；
  - [x] Rtodo 时间轴极限放大 1 分钟物理网格阵列与全自由起止时间设定系统（Ctrl+滚轮最大放大至 720px/h 呈现全幅 1 分钟独立格子与 Hover 晶莹高光单元格、新建日程模态框支持开始时间与结束时间各自分秒自由调节 `[-] HH [+] : [-] MM [+]`、鼠标滚轮直接微调、快捷时长增量胶囊与实时动态时长徽标）。

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
| **Milestone 12** | **Windows 系统默认 UI 字体动态绑定与品牌字样彻底净化**：<br>1. **Windows 原生 UI 字体动态解析**：在 `update_system_fonts` 中通过 `SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, ...)` 动态获取系统配置的 UI 字体名称（`ncm.lfMessageFont.lfFaceName`，优雅降级为 `Microsoft YaHei UI`），使文字呈现完全融入宿主操作系统环境风格。<br>2. **第三方字样彻底净化**：彻底移除所有第三方品牌字样及引用，全面更名为 **Rtodo**（`id = "rtodo"`）。 | `src/main.c`, `src/app_calendar.h`, `src/app_calendar.c` |
| **Milestone 13** | **窗口圆角裁剪防直角溢出、通透轻盈界面重构与 Rtodo 品牌严格落地**：<br>1. **复合圆角安全视口裁剪引擎**：扩展 `rife_push_scissor_round` 与 `CMD_SCISSOR_PUSH` 底层 GDI 裁剪逻辑。针对独立应用窗口，自动构建组合区域（`CreateRoundRectRgn` 结合 `CreateRectRgn` 进行 `CombineRgn(..., RGN_AND)`），使窗口内容顶部紧密贴合标题栏下方平直边缘，底部双角严格契合 20px squircle 亚像素圆角，并内缩 1px 保护窗口外缘晶莹反光高光边缘，彻底消除右下角与左下角白色矩形直角溢出。<br>2. **通透轻盈美学重塑 (Airy & Lightweight UI)**：彻底摒弃原有臃肿感，大幅增加呼吸留白：<br>   - 移除侧边栏底部厚重卡片，侧边栏精简至 180px，强化垂直通透感；<br>   - 顶栏精炼至 44px 高度，"+ 新建日程" 按钮向内收拢 20px 边距杜绝裁切；<br>   - 周网格线由深色重线调整为极简微淡线（`#F3F4F6`，深色模式 `#201933`），卡片强调条精简为 2px，卡片边距 2px，重塑通透灵动感。<br>3. **Rtodo 品牌严格规范**：中文与英文显示名严格确立为 **Rtodo**（杜绝 "Rtodo 日程" 或任何第三方品牌词），同步更新应用清单、窗口标题与全局代码注释。 | `src/rife_core.h`, `src/rife_core.c`, `src/main.c`, `src/app_calendar.c`, `AI_CONTINUITY.md` |
| **Milestone 14** | **根除窗口打开颤抖与飞书原生极简美学全面对齐**：<br>1. **窗口颤抖根治 (Anti-jitter Engine)**：将衰减速度提升至 `lambda = 22.0f` 并引入 `anim >= 0.985f` 临界硬切断；动画期间使用 `floorf()` 整数栅格量化杜绝亚像素振荡；展开动画中锁定目标排版尺寸，配合底层硬件圆角裁剪平滑揭示，彻底消除了每帧重排计算引起的字符颤抖。<br>2. **飞书原生纯净美学重塑**：顶栏移除挤占空间的 "+ 新建日程" 按钮，改由右下角 44x44px 飞书蓝圆形悬浮按钮（FAB）唤起；全线支持周日首列自然周历（`get_day_of_week_sun` / `get_week_sunday`）；引入 21px 加粗大号日期排版，配以淡雅星期名与文雅 `GMT+8` 标注，视觉通透清爽、绝无臃肿感。 | `src/main.c`, `src/app_calendar.c`, `AI_CONTINUITY.md` |
| **Milestone 15** | **舒展大时间网格与纵向平滑滚动引擎 (Spacious Grid & Vertical Scrolling)**：<br>1. **全天候 24 小时大格子舒展排布**：彻底摒弃挤压小时格子的做法，将每小时行高提升至 **60px（1px = 1min 黄金比例）**，支持 00:00~24:00 全天候排布与多行富文本日程卡片呈现。<br>2. **顶栏固定与硬件视口平滑滚动**：周表头（`周日`..`周六` 及 21px 大号日期）在顶端始终保持固定置顶，下方时间网格、事件卡片与当前时间红线随滚轮平滑上下移动，由底层硬件视口精准裁切。<br>3. **原生鼠标滚轮驱动与微动滚动条**：在宿主 `WndProc` 中捕获 `WM_MOUSEWHEEL` 并精确转换屏幕坐标至客户区坐标，主循环自动转发至悬停窗口；右侧内嵌 4px 极简微动圆角滚动滑块指示器。<br>4. **智能定位与快捷重置**：窗口初始打开及点击 "[ 今天 ]" 按钮时，自动智能平滑定位于当天核心工作时段（08:00 AM），视觉开箱即舒服舒展。 | `src/main.c`, `src/app_calendar.c`, `AI_CONTINUITY.md` |
| **Milestone 16** | **全界面液态玻璃重塑与全局纤细轻量字体引擎升级 (Full Liquid Glass & Slender Typography)**：<br>1. **全景液态玻璃贯通**：彻底移除 Rtodo 原实心不透明纯白底板（`0xFFFFFFFF`），让宿主窗口底部晶莹剔透的双线性亚像素液态玻璃与桌面多谐波光场贯通整窗；侧边栏使用 `0xFFFFFF28` 极淡磨砂流体遮罩，主网格线与表头改用透明度微透线，整体呈现真正流动通透的高级液态玻璃质感。<br>2. **全局纤细轻量字体体系 (Slender Typography Engine)**：重构字重梯度，将大号日期数字与重要标题从 `FW_BOLD` (700) 下调为 **`FW_LIGHT` (300)**，正文/小字/标注全面采用 `FW_LIGHT` (300) 与 `FW_NORMAL` (400)，彻底消除粗大笨重与墨晕感，呈现 Apple/飞书原生极简轻灵美学。<br>3. **底层渲染引擎支持 Alpha 镂空与亚像素混合**：新增 `rife_blend_round_rect_pixels`（SDF 亚像素圆角透明混合，自动兼容 `GetClipBox` 视口裁剪），为 `CMD_RECT` 与 `CMD_ROUND_RECT` 注入真实 Alpha 混合通道与 `HOLLOW_BRUSH` 纯线框镂空机制。<br>4. **实时红线时间标尺防碰撞药丸徽标**：针对整点刻度紧贴实时时间（如 `12:59` 紧贴 `13:00`）时的文字交叠问题，引入 `38x16px` 高饱和红色圆角胶囊药丸徽标底板（`0xF53F3FFF`）内嵌纯白文字，完美遮蔽下层重叠刻度。 | `src/main.c`, `src/app_calendar.c`, `AI_CONTINUITY.md` |
| **Milestone 17** | **日程全功能闭环落地、真实磁盘持久化与硬编码测试数据彻底清除 (Complete Interactive Workflow, Disk Persistence & Clean Slate)**：<br>1. **彻底清空测试数据 (Clean Slate)**：完全移除原 `e1`~`e7` 七条硬编码假数据，初始化时默认空日程状态（`event_count = 0`），视觉纯净整洁。<br>2. **纯 C 零堆磁盘持久化引擎 (Disk Persistence Engine)**：新增 `save_calendar_events` 与 `load_calendar_events`，基于 `rtodo_events.bin` 二进制文件进行快速、零堆分配的原子化序列化读写；任何创建、完成标记切换或删除操作均即刻同步持久化，重启系统依然保留用户真实日程。<br>3. **全视图网格空白点击建日程 (Click-to-Create in Grid)**：周视图网格中点击任意空白时间单元格（列=周几，行=时段），日视图时间网格，月视图日期方格，均自动计算目标日期与对应小时，直接弹出新建模态卡片并自动预设好时段。<br>4. **新建日程模态卡片交互升级 (Rich Event Creation Modal)**：尺寸扩充至 `400x340px`，支持 4 类分类切换、6 常用预设主题标签（2x3 胶囊）、开始时间时钟加减调整与 `:00`/`:30` 快速切换、4 档时长选择（`30分`/`1小时`/`1.5时`/`2小时`）并实时动态计算结束时间、4 类工作地点（线上会议、会议室A、办公室、居家远程）一键选择。<br>5. **日程详情弹窗删除与完成闭环 (Popover Inspection & Deletion)**：点击已有日程卡片唤起浮层，新增醒目红色描边 "删除日程" 操作，支持即刻数组移除并同步写盘；支持一键切换 "标记完成 / 已完成"；日程待办列表支持直接点击几何矢量对勾标记完成，并在零日程时呈现精致优雅的空状态（Empty State）磨砂卡片。 | `src/app_calendar.c`, `AI_CONTINUITY.md` |
| **Milestone 18** | **彻底移除预设标签与胶囊、全量用户自定义日程与动态标签系统 (Full Custom Fields & User Tag System)**：<br>1. **彻底剔除固定预设胶囊**：全面删除写死的 6 项预设主题（“团队周会”、“代码重构”等）与 4 项预设地点（“线上会议”、“会议室A”等），100% 赋权给用户自主决定。<br>2. **原生纯 C 文本输入与光标引擎**：在 Win32 宿主（`src/main.c`）捕获 `WM_CHAR`、`WM_KEYDOWN`，支持全键盘英数/符号输入、`Ctrl+V` Windows 系统剪贴板 Unicode 文本瞬时转 UTF-8 粘贴；实现多字节 UTF-8 安全边界退格删除（`utf8_pop_back`），光标闪烁（`|`）与聚焦高光（`active_field` 状态机），支持 Tab 键焦点循环轮转。<br>3. **用户自定义标签管理体系 (CustomTag Engine)**：支持用户自主创建并命名标签（如“健身”、“读书”、“项目A”），配备 6 档现代晶莹色彩拾色器；侧边栏“我的日历”动态展示用户自定义标签并联动过滤，顶栏提供 `[+]` 快捷新建入口。<br>4. **复合零堆持久化升级 (`rtodo_data.bin`)**：升级为包含自定义标签列表与日程清单的原子二进制文件 `rtodo_data.bin`，零堆内存波动，重启完好留存。 | `src/app_calendar.h`, `src/app_calendar.c`, `src/main.c`, `AI_CONTINUITY.md` |
| **Milestone 19** | **系统级与应用级双轨工业级持久化安全防护 (Dual-Track Robust Persistence Engine)**：<br>1. **应用数据与系统配置持久化闭环**：<br>   - Rtodo 日程数据：用户自定义的全部标签、创建的全部日程、完成勾选态与删除操作，实时原子化写入 `rtodo_data.bin`；<br>   - 系统偏好设置：设置面板（深色/明亮主题、流体云色彩、呼吸灯节律、界面语言、液态玻璃透光度等）修改后自动原子化写入 `system_config.bin`，开机自启或重开软件自动加载；<br>2. **工业级 Windows 安全路径对齐**：采用优先 `%APPDATA%\RifeOS` + 便携模式目录检测的双轨路径解析，权限安全无阻碍，完全免疫由于桌面快捷方式、启动目录（CWD）变化导致的相对路径读写失败；升级重装不丢数据；Inno Setup 安装脚本快捷方式补充显式 `WorkingDir: "{app}"`。 | `src/app_calendar.c`, `src/app_manifest.h`, `src/app_manifest.c`, `src/app_settings.c`, `src/main.c`, `installer/RifeOS_Setup.iss`, `AI_CONTINUITY.md` |
| **Milestone 20** | **全界面边缘锯齿根治、全按钮液态玻璃升级、文字排版绝对居中与快捷回到今天系统 (Zero Aliasing, Liquid Glass Buttons, True Centered Typography & Quick Today Engine)**：<br>1. **全界面边缘锯齿彻底根治 (Zero Aliasing Engine)**：彻底清除 `src/main.c` 中旧有的 GDI `RoundRect()` 1 位二进制阶梯锯齿硬光栅 fallback，所有圆角矩形（无论纯色 `a=255`、线框 `a=0, ba>0` 或半透玻璃）统一接入底层 CPU 亚像素连续超椭圆距离场（Squircle Box-SDF）渲染器 `rife_blend_round_rect_pixels()`；引入内部核心高速填充跳过逻辑（`dist <= -1.5f` 且无描边时直接整型写入），实现极致 60FPS 帧率下 100% 丝滑无狗牙平整圆角。<br>2. **全局按钮框液态玻璃质感重构 (Liquid Glass Buttons)**：构筑统一轻量组件 `rife_draw_liquid_glass_button`；次级/取消按钮采用半透磨砂玻璃微透底板、高光折射反光边缘与灵动 Hover 反馈；核心确定按钮采用液态水晶蓝微透琉璃配合顶边镜面高光弧（Specular Rim Highlight）；弹窗输入框采用微透磨砂底色并加入顶部 1px 晶莹折射高光线。<br>3. **文字与图标 100% 严格几何居中 (Mathematically Centered Typography)**：实现精确 UTF-8 / CJK 字形几何测算函数 `cal_measure_text_width`，精准识别 ASCII（窄/常规/宽字符）与 CJK 中日韩字符宽度；依据字重基线动态计算精确中心基准坐标 `tx = bx + (bw - tw) * 0.5f` 与 `ty = by + (bh - font_h) * 0.5f`，彻底根除字形上下偏心与视觉上浮。<br>4. **快捷回到今天系统 (Instant Today Navigation)**：键盘快捷键支持非打字状态下按下 `T` / `t` 或 `Home` 键立即瞬移回当天并平滑定位至核心工作时段；顶栏非今天时“今天”按钮自动点亮为液态水晶蓝药丸并呈现 `今天 T` 快捷键提示；主视图右下角浮现晶莹剔透的 `⟲ 回到今天` 液态玻璃悬浮胶囊，随时一键穿梭返回当前日期。 | `src/main.c`, `src/app_calendar.c`, `src/app_settings.c`, `AI_CONTINUITY.md` |
| **Milestone 21** | **文本输入管道彻底修复、焦点保持机制与 GDI 原生光学居中渲染引擎 (Text Input Restoration, Focus Retention & Pixel-Perfect DrawText Centering Engine)**：<br>1. **活动窗口焦点抹除 Bug 彻底根治**：定位并清除了 `src/main.c` 中 `WM_LBUTTONUP` 误将 `plat->active_win_idx` 强制置为 `-1` 的致命 Bug，确保鼠标点击输入框松开后窗口稳定保持激活；仅在用户明确点击纯桌面空白底板时解除焦点；<br>2. **全流程 Unicode 消息管道升级**：全面换装 Windows 原生 `WNDCLASSW`、`RegisterClassW`、`CreateWindowExW`、`PeekMessageW`、`DispatchMessageW`、`DefWindowProcW`，原生直接支持中文输入法（IME）UTF-16 字符直通与多字节 UTF-8 转换，扩充 `RifeInput::text_input` 至 128 字节；支持所有按键事件（Tab、Esc、回车、退格）无阻碍分发至当前聚焦应用窗口插件；<br>3. **输入框 60FPS 平滑闪烁光标与实时渲染**：在 `calendar_render` 中驱动输入框闪烁时钟，聚焦时实时请求重绘，保证光标律动丝滑自然，支持中英文、数字键入与多字节安全退格；<br>4. **基于 GDI `DrawTextW` 的原生像素级矩形居中指令 (`CMD_TEXT_RECT`)**：新增 `CMD_TEXT_RECT` 与 `rife_draw_text_rect`，接入 Windows GDI 原生 `DrawTextW(..., DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX)`，依据当前激活字体的物理字形直接完成 100% 绝对水平与垂直双向居中；<br>5. **全界面所有按钮与胶囊居中全面重构**：`rife_draw_liquid_glass_button` 全线接入 `CMD_TEXT_RECT`，“今天”、翻页箭头 `<` `>`、视图切换胶囊 [日] [周] [月]、悬浮胶囊、弹窗 [取消] 与 [确定]、自定义标签胶囊全部达到物理与光学绝对居中。 | `src/rife_core.h`, `src/rife_core.c`, `src/main.c`, `src/app_calendar.c`, `src/app_settings.c`, `AI_CONTINUITY.md` |
| **Milestone 22** | **Rtodo 标签重命名与增删生命周期管理、15 分钟细分时间轴与 Ctrl+滚轮平滑缩放引擎 (Sidebar Tag Renaming & Full Lifecycle Management, 15-Minute Timeline Unit & Ctrl+Wheel Zoom Engine)**：<br>1. **侧边栏栏目重命名**：将侧边栏的“我的日历”正式更名为“**标签**”；<br>2. **标签全生命周期增删管理**：侧边栏每个标签行右侧内嵌专属悬浮 `×` 删除操作按钮（平时微透低调，hover 时呈微红圆角高亮），点击即刻自 `tags[]` 中安全剔除并前移后续元素，`tag_count--`；自动遍历自愈所有关联日程的 `tag_idx`（关联项回退为首个标签，后续项索引依次递减），弹窗选择状态同步自愈，并即刻触发原子持久化写入 `rtodo_data.bin`；<br>3. **15 分钟为基本单位的细分时间轴与网格吸附**：引入动态时间缩放因子 `state->time_scale`（默认 96px/小时，15 分钟即占用 24px 大格子）；周视图与日视图每小时内部精确细分为 4 个 15 分钟单元；`:30` 半点呈现清晰微分割线并标注 `:30` 刻度，`:15` 和 `:45` 呈现轻柔辅助微细线；点击空白时间网格按垂直位置精准吸附至 15 分钟对齐时段（`:00`, `:15`, `:30`, `:45`）；新建弹窗时长选项升级为 `{ "15分", "30分", "1小时", "2小时" }`，分钟微调支持按 15 分钟循环步进（`00 -> 15 -> 30 -> 45 -> 00`）；<br>4. **Ctrl + 鼠标滚轮平滑缩放引擎**：检测 `VK_CONTROL` 键状态，按住 Ctrl 时滚轮以当前鼠标指针在时间轴聚焦的时间点为不动点（Zoom-to-Mouse-Anchor）平滑放大/缩小刻度（44px~240px），视效自然稳定不跳脱；松开 Ctrl 时保持原有纵向大表格平滑滚动。 | `src/app_calendar.c`, `AI_CONTINUITY.md`, `walkthrough.md` |
| **Milestone 23** | **Rtodo 1 分钟原子级高精度时间轴、吸附与微调系统 (1-Minute Atomic Precision Timeline, Grid Snapping & Controls System)**：<br>1. **1 分钟原子级网格吸附与点击新建**：周视图与日视图点击空白时间网格时，根据点击垂直坐标与当前缩放倍率高精度计算精确分钟数（`total_mins = exact_h * 60.0f + 0.5f; state->new_hour = total_mins / 60; state->new_min = total_mins % 60;`），直接以 1 分钟为最小物理原子吸附对齐（0~59分全覆盖）；默认时长设为 1 分钟（`new_duration_idx = 0`）；<br>2. **1 分钟多维时长体系与终点时间推导**：重构 `calc_event_end_time`，将事件时长选项扩充为 `{ "1分", "15分", "30分", "1h" }`（分钟数分别为 1, 15, 30, 60），精确支持微任务与极短日程；弹窗内实时显示高精度起止时间（如 `09:05 至 09:06`）；<br>3. **弹窗时间控制器拆分与 1 分钟精密调节**：将原粗粒度单一按钮重构为独立的小时调节 `[-] HH [+]` 与分钟调节 `[-] MM [+]`（中间以精美居中 `:` 分隔）；支持点击 `[-]` / `[+]` 按钮、点击 `HH` / `MM` 数值框直接步进调节；弹窗内悬停小时或分钟框时，滑动鼠标滚轮直接以 1 小时或 1 分钟为单位微调，操作得心应手；<br>4. **瑞士钟表级多级分层时间标尺刻度体系**：时间轴支持平滑缩放至最大 480px/小时（1 分钟 = 8.0px）；标尺引入分层刻度系统：`hour_h >= 90px` 时标尺边缘呈现 5 分钟刻度；`hour_h >= 180px` 时标尺边缘精细绘制 1 分钟微刻度线（长 3px）；`hour_h >= 240px` 时主网格绘制 5 分钟辅助微透网格线与文字标注；视觉宛如瑞士机械精密仪器般细腻；<br>5. **1 分钟微日程卡片紧凑胶囊徽标呈现**：周视图与日视图中为 1 分钟等极短日程设定优雅的最小物理渲染高度（周视图 `14px`，日视图 `18px`），配合硬件级圆角视口裁剪，单行标题与标签色彩指示条清晰可辨，点击与悬停响应灵敏。 | `src/app_calendar.c`, `AI_CONTINUITY.md`, `walkthrough.md` |
| **Milestone 24** | **Rtodo 侧边栏全功能实时搜索与浮层穿梭导航系统 (Sidebar Interactive Search, Instant Filtering & Floating Results Popover)**：<br>1. **搜索框全键盘交互与光标闪烁**：侧边栏搜索框深度接入输入管道（`active_field == 5`），支持焦点高亮液态玻璃边框、60FPS 丝滑光标闪烁、全键盘字符键入、多字节 UTF-8 安全退格、`Ctrl+V` 系统剪贴板粘贴、`ESC` 瞬时清空并取消聚焦；右侧具备 `×` 清空按钮（非空时）与 `+` 快速新建按钮（空时）；<br>2. **多字段高容错即时检索引擎**：实现 `utf8_contains_ignore_case` 与 `ascii_to_lower` 纯 C 算法，支持不区分大小写的英文字符匹配与 UTF-8 / CJK 中文字符串连续字节子串检索；同时覆盖日程标题（Title）、发生地点（Location）、详细备注（Desc）以及所属分类标签名（Tag Name）；<br>3. **多维视图全量联动即时过滤**：在 `is_event_visible` 过滤器中挂接搜索匹配状态，用户键入搜索词时，周视图网格、日视图时间轴、月视图方格、日程清单列表即刻实时剔除非匹配项，只高亮呈现相关日程；<br>4. **340px 晶莹液态玻璃搜索结果悬浮面板 (Floating Results Popover)**：当搜索框有内容时，侧边栏右侧即刻优雅滑出 340px 宽的专属悬浮卡片，清晰呈现匹配总条数与前 6 条精细日程卡片（包含色彩标签指示柱、事件标题、日期时间段与地点详情）；支持 Hover 悬停晶莹高光反馈；无匹配时呈现精致空状态提示；<br>5. **一键穿梭导航与时间轴平滑对齐 (Instant Teleport Navigation)**：在搜索框中按下回车（Enter）或直接点击悬浮结果卡片，系统即刻将当前视图年/月/日切换至该日程所在日期，主时间轴自动平滑滚动并将视口对齐至该日程发生时间，自动选中并弹出该日程详情浮层卡片，搜索结果浮层自动收起，完成极致流畅的检索-穿梭-查看闭环。 | `src/app_calendar.c`, `AI_CONTINUITY.md`, `walkthrough.md` |
| **Milestone 25** | **Rtodo 时间轴极限放大 1 分钟物理网格阵列与全自由起止时间设定系统 (1-Minute Physical Grid Cells Array on Max Zoom & Fully Customizable Event Start/End Time)**：<br>1. **时间轴极限微距缩放与 1 分钟物理格子阵列 (Physical 1-Minute Grid Cells)**：将 Ctrl+滚轮缩放上限拓展至 720px/小时（1 分钟高度达 12.0px，响应步进由 18px 动态升至 28px）；当 `hour_h >= 240px` 时，在周视图与日视图的主网格中，为 60 分钟每一个独立分钟绘制全幅横贯微透水平分隔线（`line_1m`）；标尺边缘绘制精密 1 分钟短刻度；鼠标在时间轴移动时，实时计算光标所在列与精确到分钟的物理单元格，悬停位置以液态玻璃微透晶莹底板（`col_w x cell_h`）高光标出当前聚焦的 1 分钟小格子，视觉清晰通透。<br>2. **日程起止时间 100% 自由自定义 (Fully Custom Start & End Time)**：彻底打破固定预设时长的限制，允许用户随心所欲自由设定开始时间与结束时间；模态弹窗重构时间输入为对称双调节器：`开始: [-] HH [+] : [-] MM [+]   至   结束: [-] HH [+] : [-] MM [+]`；开始时间与结束时间的小时、分钟均支持点击 `[-]`/`[+]` 独立微调、点击数字框直接步进、悬停时滚动鼠标滚轮以 1 分钟为原子单位平滑滚轮调时；调整开始时间时，自动保持当前设定的持续时长平移结束时间；调整结束时间时，自动保证结束时间严格晚于开始时间（至少 1 分钟）；<br>3. **快捷增量胶囊与实时动态时长徽标 (Quick Increment Pills & Real-time Duration Badge)**：新增快捷时长增量胶囊行：`[+1分]`, `[+15分]`, `[+30分]`, `[+1h]`, `[+2h]`，点击直接在开始时间基础上追加指定时长，当前持续时间与快捷选项吻合时自动高亮为液态水晶蓝；右侧内嵌专属液态玻璃动态时长徽标（`时长: XX分钟` 或 `时长: X小时X分`），随时间调节实时变动；底部日程概要同步动态展示起止全貌与总时长（如 `9月19日 09:30 至 10:45 (共 75 分钟)`）。 | `src/app_calendar.c`, `AI_CONTINUITY.md`, `walkthrough.md` |
