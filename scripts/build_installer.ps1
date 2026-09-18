<#
.SYNOPSIS
    RifeOS 一键构建与安装包打包脚本 (One-Click Build & Package)
    Developed by Renly
#>

$ErrorActionPreference = "Stop"

Write-Host "==================================================" -ForegroundColor Cyan
Write-Host "          RifeOS 自动化安装包构建系统             " -ForegroundColor Cyan
Write-Host "==================================================" -ForegroundColor Cyan

# 1. 检查并生成品牌图标
$icoPath = Join-Path $PSScriptRoot "..\assets\rifeos.ico"
if (-not (Test-Path $icoPath)) {
    Write-Host "[1/4] 正在生成 RifeOS 高清多分辨率矢量图标..." -ForegroundColor Yellow
    & (Join-Path $PSScriptRoot "generate_icon.ps1")
} else {
    Write-Host "[1/4] 品牌图标已就绪: $icoPath" -ForegroundColor Green
}

# 2. 终止运行中的 RIFEOS 进程，防止链接被占用
Get-Process RIFEOS -ErrorAction SilentlyContinue | Stop-Process -Force
Start-Sleep -Milliseconds 300

# 3. 编译 MSVC Release x64 可执行文件
Write-Host "[2/4] 正在使用 MSVC 14.51 + CMake 构建 Release x64 二进制..." -ForegroundColor Yellow

$vcvars = "D:\Program Files\VS2026\VC\Auxiliary\Build\vcvars64.bat"
$cmake = "D:\Program Files\VS2026\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
$buildDir = "d:\MyProjects\VS\RIFEOS\out\build\x64-release"

$cmd = "call `"$vcvars`" && `"$cmake`" --build `"$buildDir`" --config Release"
cmd.exe /c $cmd
if ($LASTEXITCODE -ne 0) {
    Write-Error "CMake 编译失败，请检查编译日志！"
    exit 1
}

$exePath = Join-Path $buildDir "RIFEOS.exe"
if (-not (Test-Path $exePath)) {
    Write-Error "未找到构建产物: $exePath"
    exit 1
}
Write-Host "      构建产物就绪: $exePath ($((Get-Item $exePath).Length) bytes)" -ForegroundColor Green

# 4. 寻找 Inno Setup 编译器 ISCC.exe
Write-Host "[3/4] 正在检测 Inno Setup 编译器..." -ForegroundColor Yellow
$isccPaths = @(
    "C:\Users\Renly\AppData\Local\Programs\Inno Setup 6\ISCC.exe",
    "C:\Program Files (x86)\Inno Setup 6\ISCC.exe",
    "C:\Program Files\Inno Setup 6\ISCC.exe"
)

$iscc = $null
foreach ($p in $isccPaths) {
    if (Test-Path $p) {
        $iscc = $p
        break
    }
}

if (-not $iscc) {
    $cmdIscc = Get-Command iscc.exe -ErrorAction SilentlyContinue
    if ($cmdIscc) { $iscc = $cmdIscc.Source }
}

if (-not $iscc) {
    Write-Host "正在通过 winget 自动安装 Inno Setup 6..." -ForegroundColor Cyan
    winget install JRSoftware.InnoSetup --source winget --accept-source-agreements --accept-package-agreements
    foreach ($p in $isccPaths) {
        if (Test-Path $p) {
            $iscc = $p
            break
        }
    }
}

if (-not $iscc) {
    Write-Error "未能定位 Inno Setup 编译器 ISCC.exe！"
    exit 1
}
Write-Host "      定位到编译器: $iscc" -ForegroundColor Green

# 5. 编译安装包
Write-Host "[4/4] 正在编译最终安装包 Setup.exe..." -ForegroundColor Yellow
$issPath = Join-Path $PSScriptRoot "..\installer\RifeOS_Setup.iss"
& "$iscc" "$issPath"
if ($LASTEXITCODE -ne 0) {
    Write-Error "Inno Setup 编译失败！"
    exit 1
}

$distExe = "d:\MyProjects\VS\RIFEOS\dist\RifeOS_Setup_v1.0.0.exe"
if (Test-Path $distExe) {
    $sizeMB = [Math]::Round(((Get-Item $distExe).Length / 1MB), 2)
    Write-Host ""
    Write-Host "==================================================" -ForegroundColor Green
    Write-Host "  打包完成！Windows 安装包已生成：                " -ForegroundColor Green
    Write-Host "  文件路径: $distExe" -ForegroundColor White
    Write-Host "  安装包体积: $sizeMB MB" -ForegroundColor White
    Write-Host "==================================================" -ForegroundColor Green
}
