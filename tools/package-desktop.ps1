<#
.SYNOPSIS
    Portable Windows packaging for native LuminaS.

.DESCRIPTION
    Repeatable, additive-only packaging script. Copies the built app plus the
    LibRaw DLL into a NEW versioned OutputRoot folder, runs windeployqt for the
    Qt runtime, writes qt.conf, bundles third-party license texts and a short
    Chinese README.txt.

    Safety contract:
      * Never deletes or overwrites: refuses an existing target directory.
      * Only copies from the repository; never edits sources.
      * No shortcut and no ZIP are produced (handled elsewhere).
      * Does not run the app or deploy.

.PARAMETER QtRoot
    Qt 6 MSVC x64 installation root. Defaults to the project .tools known root.

.PARAMETER LibRawRoot
    LibRaw SDK root. Defaults to the project .tools known root.

.PARAMETER LcmsRoot
    Little CMS source root. Defaults to the project .tools known root.

.PARAMETER OutputRoot
    Directory that receives a new LuminaS-$Version folder. Defaults to dist.

.PARAMETER Version
    Version label used in the package folder name.

.EXAMPLE
    powershell -NoProfile -ExecutionPolicy Bypass -File tools/package-desktop.ps1
#>
[CmdletBinding()]
param(
    [string]$QtRoot,
    [string]$LibRawRoot,
    [string]$LcmsRoot,
    [string]$OutputRoot,
    [string]$Version = 'preview-20261014-v31'
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

# ---------------------------------------------------------------- paths ----
$taskRoot = [IO.Directory]::GetParent($PSScriptRoot).FullName
if ($Version -notmatch '^[A-Za-z0-9][A-Za-z0-9._-]*$') { throw 'Version must be a simple folder label.' }

function Resolve-Root {
    param([string]$Value, [string]$Fallback, [string]$Name)
    if ([string]::IsNullOrWhiteSpace($Value)) { $Value = $Fallback }
    if (-not (Test-Path -LiteralPath $Value -PathType Container)) {
        throw "$Name not found: $Value"
    }
    return (Resolve-Path -LiteralPath $Value).ProviderPath
}

if ([string]::IsNullOrWhiteSpace($QtRoot))      { $QtRoot = Join-Path $taskRoot '.tools/Qt/6.8.3/msvc2022_64' }
if ([string]::IsNullOrWhiteSpace($LibRawRoot))  { $LibRawRoot = Join-Path $taskRoot '.tools/LibRaw-openmp/LibRaw-0.22.2' }
if ([string]::IsNullOrWhiteSpace($LcmsRoot))    { $LcmsRoot = Join-Path $taskRoot '.tools/lcms/Little-CMS-lcms2.19.1' }
if ([string]::IsNullOrWhiteSpace($OutputRoot))  { $OutputRoot = Join-Path $taskRoot 'dist' }

$QtRoot     = Resolve-Root $QtRoot     (Join-Path $taskRoot '.tools/Qt/6.8.3/msvc2022_64')            'QtRoot'
$LibRawRoot = Resolve-Root $LibRawRoot (Join-Path $taskRoot '.tools/LibRaw-openmp/LibRaw-0.22.2')    'LibRawRoot'
$LcmsRoot   = Resolve-Root $LcmsRoot   (Join-Path $taskRoot '.tools/lcms/Little-CMS-lcms2.19.1')     'LcmsRoot'

$buildDir = Join-Path $taskRoot 'build/dev'
$appExe   = Join-Path $buildDir 'LuminaS.exe'
$rawDll   = Join-Path $buildDir 'libraw.dll'
if (-not (Test-Path -LiteralPath $appExe -PathType Leaf)) { throw "Build the app first; missing $appExe" }
if (-not (Test-Path -LiteralPath $rawDll -PathType Leaf)) { throw "Build LibRaw first; missing $rawDll" }

$windeployqt = Join-Path $QtRoot 'bin/windeployqt.exe'
if (-not (Test-Path -LiteralPath $windeployqt -PathType Leaf)) { throw "windeployqt.exe not found under QtRoot: $QtRoot" }

if (-not (Test-Path -LiteralPath $OutputRoot -PathType Container)) {
    New-Item -ItemType Directory -Path $OutputRoot -Force | Out-Null
}
$OutputRoot = (Resolve-Path -LiteralPath $OutputRoot).ProviderPath

$packageDir = Join-Path $OutputRoot "LuminaS-$Version"
if (Test-Path -LiteralPath $packageDir) {
    throw "Refusing to overwrite existing package directory: $packageDir"
}
New-Item -ItemType Directory -Path $packageDir | Out-Null
Write-Host "Package target: $packageDir"

# ------------------------------------------------------- payload copies ----
# LiteralPath everywhere so brackets/spaces in real paths are never globbed.
Copy-Item -LiteralPath $appExe -Destination (Join-Path $packageDir 'LuminaS.exe')          -Force
Copy-Item -LiteralPath $rawDll -Destination (Join-Path $packageDir 'libraw.dll')           -Force
Write-Host 'Copied LuminaS.exe and libraw.dll'

# ------------------------------------------------------------- windeploy ----
# windeployqt writes scratch files; keep them out of the repo build tree and
# out of the user profile so packaging stays repeatable and self-contained.
$tempDir = Join-Path $taskRoot 'build/private-qa/package-temp'
if (-not (Test-Path -LiteralPath $tempDir -PathType Container)) {
    New-Item -ItemType Directory -Path $tempDir -Force | Out-Null
}
$tempDir = (Resolve-Path -LiteralPath $tempDir).ProviderPath
$env:TEMP = $tempDir
$env:TMP  = $tempDir

$env:PATH = (Join-Path $QtRoot 'bin') + ';' + $env:PATH

$deployArgs = @(
    '--release'
    '--compiler-runtime'
    '--no-translations'
    '--no-opengl-sw'
    '--dir'
    $packageDir
    (Join-Path $packageDir 'LuminaS.exe')
)
Write-Host "Running windeployqt: $windeployqt $($deployArgs -join ' ')"
& $windeployqt @deployArgs
$deployExit = $LASTEXITCODE
if ($deployExit -ne 0) {
    Write-Warning "windeployqt failed ($deployExit); using explicit native Widgets dependency deployment."
    foreach ($module in @('Qt6Core.dll','Qt6Gui.dll','Qt6Widgets.dll','Qt6Concurrent.dll','Qt6Svg.dll')) {
        Copy-Item -LiteralPath (Join-Path $QtRoot "bin/$module") -Destination (Join-Path $packageDir $module) -Force
    }
    foreach ($plugin in @('platforms/qwindows.dll','imageformats/qjpeg.dll','imageformats/qgif.dll','imageformats/qico.dll','styles/qmodernwindowsstyle.dll')) {
        $pluginTarget = Join-Path $packageDir $plugin
        New-Item -ItemType Directory -Force -Path ([IO.Path]::GetDirectoryName($pluginTarget)) | Out-Null
        Copy-Item -LiteralPath (Join-Path $QtRoot "plugins/$plugin") -Destination $pluginTarget -Force
    }
}
Write-Host 'Runtime deployment completed'

# --------------------------------------------- verify actual deployment ----
$qtRequiredDlls = @('Qt6Core.dll', 'Qt6Gui.dll', 'Qt6Widgets.dll')
$missing = @()
foreach ($dll in $qtRequiredDlls) {
    if (-not (Test-Path -LiteralPath (Join-Path $packageDir $dll) -PathType Leaf)) { $missing += $dll }
}
if ($missing.Count -gt 0) { throw "windeployqt did not deploy: $($missing -join ', ')" }
Write-Host "Verified Qt DLLs: $($qtRequiredDlls -join ', ')"

# Locate the platform plugin exactly where windeployqt actually put it.
$platformCandidates = @('platforms/qwindows.dll', 'plugins/platforms/qwindows.dll')
$platformRel = $null
foreach ($candidate in $platformCandidates) {
    if (Test-Path -LiteralPath (Join-Path $packageDir $candidate) -PathType Leaf) {
        $platformRel = $candidate
        break
    }
}
if (-not $platformRel) {
    throw "platforms/qwindows.dll not found under package root or plugins/ ; deployment layout unexpected"
}
Write-Host "Detected platform plugin: $platformRel"

# Derive qt.conf Prefix/Plugins from the real, verified location.
if ($platformRel -eq 'platforms/qwindows.dll') {
    $pluginsPrefix = '.'
} else {
    $pluginsPrefix = 'plugins'
}
Write-Host "qt.conf Plugins prefix: $pluginsPrefix"

# Verify remaining deployment artifacts, honoring the detected layout.
$pluginRoot = if ($pluginsPrefix -eq '.') { '.' } else { 'plugins' }
$requiredArtifacts = @(
    (Join-Path $pluginRoot 'platforms/qwindows.dll')
    (Join-Path $pluginRoot 'imageformats/qjpeg.dll')
)
$missingArtifacts = @()
foreach ($artifact in $requiredArtifacts) {
    if (-not (Test-Path -LiteralPath (Join-Path $packageDir $artifact) -PathType Leaf)) {
        $missingArtifacts += $artifact
    }
}
if ($missingArtifacts.Count -gt 0) {
    throw "Missing deployed artifacts: $($missingArtifacts -join ', ')"
}
Write-Host 'Verified platforms/qwindows.dll and imageformats/qjpeg.dll'

# qt.conf next to the executable tells Qt where its plugin root lives.
$qtConf = @"
[Paths]
Prefix=.
Plugins=$pluginsPrefix
"@
Set-Content -LiteralPath (Join-Path $packageDir 'qt.conf') -Value $qtConf -Encoding ASCII
Write-Host 'Wrote qt.conf'

# --------------------------------------------------- MSVC runtime DLLs ----
# windeployqt --compiler-runtime needs a VS developer environment. Without one
# it silently deploys nothing, so locate the redist ourselves and fill the gap.
$msvcCoreDlls = @('msvcp140.dll', 'vcruntime140.dll', 'vcruntime140_1.dll')
$presentCore = @($msvcCoreDlls | Where-Object {
    Test-Path -LiteralPath (Join-Path $packageDir $_) -PathType Leaf
})
if ($presentCore.Count -eq $msvcCoreDlls.Count) {
    Write-Host "MSVC runtime already deployed: $($msvcCoreDlls -join ', ')"
} else {
    Write-Host "MSVC runtime incomplete (have: $($presentCore -join ', ')); locating VS redist"
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
    $vsRoot = $null
    if (Test-Path -LiteralPath $vswhere -PathType Leaf) {
        $vsRoot = & $vswhere -latest -products '*' -version '[17.0,18.0)' `
            -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    }
    if ([string]::IsNullOrWhiteSpace($vsRoot)) {
        Write-Warning "Visual Studio not found via vswhere; MSVC runtime DLLs must be installed by the user (VC++ 2015-2022 x64 redistributable)."
    } else {
        $redistRoot = Join-Path $vsRoot 'VC/Redist/MSVC'
        $crtDirs = @(Get-ChildItem -LiteralPath $redistRoot -Directory -ErrorAction SilentlyContinue |
            Sort-Object -Property Name -Descending |
            ForEach-Object {
                $candidate = Join-Path $_.FullName 'x64/Microsoft.VC143.CRT'
                if (Test-Path -LiteralPath $candidate -PathType Container) { $candidate }
            })
        if ($crtDirs.Count -eq 0) {
            throw "Microsoft.VC143.CRT (x64) not found under $redistRoot"
        }
        $crtDir = $crtDirs[0]
        Write-Host "Copying MSVC runtime from: $crtDir"
        foreach ($dll in @(Get-ChildItem -LiteralPath $crtDir -Filter '*.dll' -File)) {
            Copy-Item -LiteralPath $dll.FullName -Destination (Join-Path $packageDir $dll.Name) -Force
        }
        # OpenMP runtime is needed by the OpenMP LibRaw build.
        # Prefer the desktop x64 redist; onecore is for UWP and may not load.
        $vcompCandidates = @(Get-ChildItem -LiteralPath $redistRoot -Recurse -Filter 'vcomp140.dll' -File -ErrorAction SilentlyContinue |
            Where-Object { $_.FullName -match '[\\/]x64[\\/]' } |
            Sort-Object -Property @{ Expression = { if ($_.FullName -match 'onecore') { 1 } else { 0 } } }, FullName)
        $vcomp = if ($vcompCandidates.Count -gt 0) { $vcompCandidates[0] } else { $null }
        if ($vcomp) {
            Copy-Item -LiteralPath $vcomp.FullName -Destination (Join-Path $packageDir 'vcomp140.dll') -Force
            Write-Host 'Copied vcomp140.dll'
        } else {
            Write-Warning 'vcomp140.dll not found in VS redist; OpenMP LibRaw may fail to load.'
        }
    }
}

# OpenMP is required even when windeployqt already supplied the other CRT DLLs.
if (-not (Test-Path -LiteralPath (Join-Path $packageDir 'vcomp140.dll'))) {
    $vswherePath = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
    $runtimeVs = & $vswherePath -latest -products '*' -version '[17.0,18.0)' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    $openMp = Get-ChildItem -LiteralPath (Join-Path $runtimeVs 'VC/Redist/MSVC') -Recurse -Filter vcomp140.dll -File |
        Where-Object { $_.FullName -match '[\\/]x64[\\/]' -and $_.FullName -notmatch 'onecore' } | Select-Object -First 1
    if (-not $openMp) { throw 'Required x64 OpenMP runtime vcomp140.dll is missing.' }
    Copy-Item -LiteralPath $openMp.FullName -Destination (Join-Path $packageDir 'vcomp140.dll')
}
foreach ($runtimeDll in $msvcCoreDlls) {
    if (-not (Test-Path -LiteralPath (Join-Path $packageDir $runtimeDll))) { throw "Required runtime missing: $runtimeDll" }
}

# ------------------------------------------------ third-party licenses ----
$licenseDir = Join-Path $packageDir 'licenses'
New-Item -ItemType Directory -Path $licenseDir | Out-Null

$licenseSources = [ordered]@{
    'OFL-SourceSerif4.txt' = Join-Path $taskRoot 'assets/fonts/OFL-SourceSerif4.txt'
    'LibRaw-LICENSE.LGPL'  = Join-Path $LibRawRoot 'LICENSE.LGPL'
    'LibRaw-COPYRIGHT'     = Join-Path $LibRawRoot 'COPYRIGHT'
    'Little-CMS-LICENSE'   = Join-Path $LcmsRoot 'LICENSE'
    'Lucide-LICENSE.txt'   = Join-Path $taskRoot 'src/app/icons/lucide/LICENSE.txt'
    'Qt-GPL-3.0-only.txt'  = Join-Path $taskRoot 'assets/licenses/qt/GPL-3.0-only.txt'
    'Qt-GPL-exception-1.0.txt' = Join-Path $taskRoot 'assets/licenses/qt/Qt-GPL-exception-1.0.txt'
    'THIRD-PARTY-SOURCES.txt' = Join-Path $taskRoot 'assets/licenses/qt/SOURCES.txt'
}

$licenseIssues = @()
foreach ($entry in $licenseSources.GetEnumerator()) {
    if (Test-Path -LiteralPath $entry.Value -PathType Leaf) {
        Copy-Item -LiteralPath $entry.Value -Destination (Join-Path $licenseDir $entry.Key) -Force
        Write-Host "License copied: $($entry.Key)"
    } else {
        $licenseIssues += "missing license source: $($entry.Value)"
    }
}

# Qt is used under LGPLv3. Ship Qt's own LGPLv3 text when the installation or
# the project provides the exact same version. The project LibRaw LICENSE.LGPL
# is LGPL-2.1 and is NOT a valid stand-in for Qt's LGPLv3, so it is never used.
$qtLgplCandidates = @(
    (Join-Path $QtRoot 'LICENSE.LGPL3')
    (Join-Path $QtRoot 'LICENSES/LGPL-3.0-only.txt')
    (Join-Path $QtRoot 'LICENSES/LGPL-3.0-only.LICENSE')
    (Join-Path $taskRoot 'docs/licenses/LGPL-3.0-only.txt')
    (Join-Path $taskRoot 'assets/licenses/qt/LGPL-3.0-only.txt')
)
$qtLgplSource = $null
foreach ($candidate in $qtLgplCandidates) {
    if (Test-Path -LiteralPath $candidate -PathType Leaf) {
        $head = Get-Content -LiteralPath $candidate -TotalCount 20 -ErrorAction SilentlyContinue
        if (($head -join ' ') -match 'Version 3') {
            $qtLgplSource = $candidate
            break
        }
    }
}
if ($qtLgplSource) {
    Copy-Item -LiteralPath $qtLgplSource -Destination (Join-Path $licenseDir 'Qt-LGPL-3.0.txt') -Force
    Write-Host "License copied: Qt-LGPL-3.0.txt (from $qtLgplSource)"
} else {
    $licenseIssues += 'missing Qt LGPLv3 license text: no LGPL-3.0 text found in the Qt installation or the project. Qt 6.8.3 is bundled under LGPLv3; obtain the LGPL-3.0 text (e.g. from the Qt source or https://www.gnu.org/licenses/lgpl-3.0.txt) and add it manually. Nothing was downloaded.'
}

if ($licenseIssues.Count -gt 0) {
    Write-Warning "LICENSE ISSUES:"
    foreach ($issue in $licenseIssues) { Write-Warning " - $issue" }
}

# ------------------------------------------------------- Chinese README ----
$readme = @"
LuminaS $Version 便携版（Windows x64）
================================================

【这是什么】
LuminaS 原生 Windows 桌面版，基于 Qt 6 + LibRaw 的 RAW/JPEG 图像处理工具。
本目录为便携版本，解压后直接运行 LuminaS.exe，无需安装。

【功能特点】
- Qt 解码 JPEG，LibRaw 解码 CR3 等 RAW 格式。
- 4K 长边预览：小图按原始尺寸显示，大图生成长边 4K 的预览。
- 调整时持续反馈：裁剪、曲线、手动色差校正等；复杂处理可能需要等待。
- 目前没有正式的导出界面（export UI），请知悉。

【会话状态说明】
- 除已保存的预设库（preset library）外，会话状态不会持久化；
  关闭程序后未保存的编辑不会保留。

【已知限制】
- 应用程序为试用版 $Version（试用版本号随构建而变化）。
- 已支持紫色/绿色去边缘色散（defringe，紫边与绿边校正）。
- 色彩管理按 SDR 显示器 ICC 配置文件处理；若系统未提供可用 ICC，则回退到 sRGB。

【运行要求】
- Windows 10/11 x64。
- 需要 Microsoft Visual C++ 2015-2022 运行库（x64）；
  若本目录已附带 msvcp140.dll / vcruntime140.dll，则通常无需另行安装。

【目录结构】
  LuminaS.exe          主程序
  libraw.dll           LibRaw 原始格式解码库
  qt.conf              Qt 运行库路径配置
  platforms/           Qt 平台插件（qwindows.dll 必需）
  imageformats/        Qt 图像格式插件（qjpeg.dll 等）
  licenses/            第三方许可证文本
  README.txt           本文件

【许可证】
本程序使用 Qt（LGPLv3）、LibRaw（LGPL 2.1）、Little CMS（MIT）以及
Source Serif 4 字体（SIL OFL 1.1）以及 Lucide 矢量图标（ISC，部分源自 Feather 的图标为 MIT）。
详见 licenses/ 目录。
"@
Set-Content -LiteralPath (Join-Path $packageDir 'README.txt') -Value $readme -Encoding UTF8
Write-Host 'Wrote README.txt'

# ----------------------------------------------------------------- done ----
$fileCount = @(Get-ChildItem -LiteralPath $packageDir -Recurse -File).Count
Write-Host ''
Write-Host '========================================='
Write-Host "Package: $packageDir"
Write-Host "Files:   $fileCount"
if ($licenseIssues.Count -gt 0) {
    Write-Host "License issues: $($licenseIssues.Count) (see warnings above)"
}
Write-Host '========================================='
