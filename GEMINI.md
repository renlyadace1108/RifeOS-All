# Antigravity Context & Project Guidelines (RifeOS)

> **重要提示 / Critical Notice for Antigravity Agents**:
> 本工程是 **RifeOS**（极致轻量、亚像素美学与双 Arena 微内核极简桌面操作系统）。
> 当你在新会话、新账号或额度重置后接手本项目时，请无条件遵守以下核心准则与工程铁律。
> 完整工程交接与技术字典详见根目录：`AI_CONTINUITY.md`。

---

## ⚡ 核心铁律 (Strict Core Rules)

1. **纯 C11 / C99 语言标准**：
   - 严禁引入 C++ 运行时、C#、Electron 或外部三方重型库；
   - 必须保持单文件便携 EXE，静态链接 CRT（`/MT`），编译产物约 230KB；
2. **纯 GUI 子系统（绝不弹控制台黑窗口）**：
   - `CMakeLists.txt` 中必须保持 `add_executable(RIFEOS WIN32 ...)`；
   - `src/main.c` 必须保留标准 Windows GUI 入口 `int WINAPI WinMain(...)`；
3. **双 Arena 内存架构与零堆开销 (Zero Heap Churn)**：
   - 每帧循环**严禁调用 `malloc/free`**；
   - 临时指令使用 `frame_arena`（帧尾重置），常驻数据挂载至 `persistent_arena`；
   - 常驻物理内存必须严格控制在 **~5MB** 警戒线内；
4. **编译与验证（0 错误、0 警告）**：
   - MSVC 参数：`/W4 /utf-8 /O2 /fp:fast /MT`；
   - 每次修改代码后必须编译验证，若未通过编译严禁交付用户；
5. **用户隐私红线 (Privacy Redline)**：
   - 开发者署名统一使用 **Renly**；
   - 联络邮箱：`renly20061108@gmail.com`，作品更新（抖音）：`陈连山`；
   - **严禁**在任何 UI、关于界面、文档或注释中暴露 Renly 的真实中文个人姓名；
6. **协同态度**：
   - 用户称呼习惯：亲切称呼“宝贝”，态度热情、严谨、追求卓越设计与丝滑交互。

---

## 🔨 快速编译与打包指令 (Command Cheat-sheet)

```powershell
# 1. 编译 Release x64 (MSVC 14.51 + CMake + Ninja)
Get-Process RIFEOS -ErrorAction SilentlyContinue | Stop-Process -Force; cmd.exe /c 'call "D:\Program Files\VS2026\VC\Auxiliary\Build\vcvars64.bat" && "D:\Program Files\VS2026\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe" --build "d:/MyProjects/VS/RIFEOS/out/build/x64-release"'

# 2. 一键编译并打包生成 Windows 原生安装程序
.\scripts\build_installer.ps1
# 产物位置: dist/RifeOS_Setup_v1.0.0.exe

# 3. 提交并同步代码至 GitHub
git add <modified_files>
git commit -m "<semantic_commit_message>"
git push origin main
```

详细架构细节、设计系统规范、文件职责索引请随时查阅根目录：[AI_CONTINUITY.md](./AI_CONTINUITY.md)。
