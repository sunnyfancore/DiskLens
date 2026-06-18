param(
    # 发布配置，默认使用 Release。
    [string]$Configuration = "Release",

    # CMake 构建目录。
    [string]$BuildDir = "build-qt",

    # 便携版输出目录。
    [string]$PortableDir = "build-qt-portable",

    # 便携版压缩包输出路径。
    [string]$PortableZip = "磁盘洞察-便携版.zip",

    # 单文件版输出路径。
    [string]$OneFileExe = "磁盘洞察-单文件版.exe",

    # Qt 安装目录的 bin 路径，留空时自动查找。
    [string]$QtBinPath = "",

    # Visual Studio vcvars64.bat 路径，留空时自动查找。
    [string]$VcVarsPath = "",

    # CMake 可执行文件路径，留空时自动查找。
    [string]$CMakePath = "",

    # 跳过 CMake 构建，只重新部署和打包。
    [switch]$SkipBuild,

    # 跳过单文件版生成。
    [switch]$SkipOneFile,

    # 跳过便携版 zip 生成。
    [switch]$SkipZip
)

$ErrorActionPreference = "Stop"
[Console]::OutputEncoding = [System.Text.Encoding]::UTF8
$script:ScriptRoot = $PSScriptRoot
if ([string]::IsNullOrWhiteSpace($script:ScriptRoot)) {
    $script:ScriptRoot = $env:DISKLENS_SCRIPT_ROOT
}
if ([string]::IsNullOrWhiteSpace($script:ScriptRoot)) {
    throw "无法确定脚本目录，请通过 scripts\package-release.cmd 执行。"
}
$script:ProjectRoot = (Resolve-Path (Join-Path $script:ScriptRoot "..")).Path

Set-Location -LiteralPath $script:ProjectRoot

<#
.SYNOPSIS
输出打包步骤标题。
.PARAMETER Message
需要显示的步骤文本。
#>
function Write-Step {
    param(
        # 需要显示的步骤文本。
        [string]$Message
    )

    Write-Host ""
    Write-Host "==> $Message" -ForegroundColor Cyan
}

<#
.SYNOPSIS
输出普通状态信息。
.PARAMETER Message
需要显示的状态文本。
#>
function Write-Info {
    param(
        # 需要显示的状态文本。
        [string]$Message
    )

    Write-Host "    $Message"
}

<#
.SYNOPSIS
为 cmd.exe 命令参数添加引号。
.PARAMETER Value
需要转义的参数文本。
.OUTPUTS
返回可用于 cmd.exe 的参数文本。
#>
function ConvertTo-CmdArgument {
    param(
        # 需要转义的参数文本。
        [string]$Value
    )

    return '"' + ($Value -replace '"', '\"') + '"'
}

<#
.SYNOPSIS
确认文件存在。
.PARAMETER Path
需要检查的文件路径。
.PARAMETER Description
用于错误提示的文件说明。
#>
function Assert-FileExists {
    param(
        # 需要检查的文件路径。
        [string]$Path,

        # 用于错误提示的文件说明。
        [string]$Description
    )

    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) {
        throw "$Description 不存在：$Path"
    }
}

<#
.SYNOPSIS
确认目录存在。
.PARAMETER Path
需要检查的目录路径。
.PARAMETER Description
用于错误提示的目录说明。
#>
function Assert-DirectoryExists {
    param(
        # 需要检查的目录路径。
        [string]$Path,

        # 用于错误提示的目录说明。
        [string]$Description
    )

    if (-not (Test-Path -LiteralPath $Path -PathType Container)) {
        throw "$Description 不存在：$Path"
    }
}

<#
.SYNOPSIS
重建一个空目录。
.PARAMETER Path
需要重建的目录路径。
#>
function Reset-Directory {
    param(
        # 需要重建的目录路径。
        [string]$Path
    )

    if (Test-Path -LiteralPath $Path) {
        Remove-Item -LiteralPath $Path -Recurse -Force
    }
    New-Item -ItemType Directory -Path $Path | Out-Null
}

<#
.SYNOPSIS
查找 Visual Studio vcvars64.bat。
.PARAMETER PreferredPath
用户指定的首选路径。
.OUTPUTS
返回 vcvars64.bat 的完整路径。
#>
function Find-VcVarsPath {
    param(
        # 用户指定的首选路径。
        [string]$PreferredPath
    )

    if (-not [string]::IsNullOrWhiteSpace($PreferredPath)) {
        Assert-FileExists -Path $PreferredPath -Description "vcvars64.bat"
        return (Resolve-Path -LiteralPath $PreferredPath).Path
    }

    $vswhere = Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio\Installer\vswhere.exe"
    if (Test-Path -LiteralPath $vswhere -PathType Leaf) {
        $installPath = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
        if (-not [string]::IsNullOrWhiteSpace($installPath)) {
            $candidate = Join-Path $installPath "VC\Auxiliary\Build\vcvars64.bat"
            if (Test-Path -LiteralPath $candidate -PathType Leaf) {
                return $candidate
            }
        }
    }

    $knownCandidates = @(
        "D:\Program Files\Microsoft Visual Studio\18\Enterprise\VC\Auxiliary\Build\vcvars64.bat",
        "D:\Program Files\Microsoft Visual Studio\2022\Enterprise\VC\Auxiliary\Build\vcvars64.bat",
        "C:\Program Files\Microsoft Visual Studio\2022\Enterprise\VC\Auxiliary\Build\vcvars64.bat",
        "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"
    )
    foreach ($candidate in $knownCandidates) {
        if (Test-Path -LiteralPath $candidate -PathType Leaf) {
            return $candidate
        }
    }

    throw "未找到 vcvars64.bat，请使用 -VcVarsPath 指定 Visual Studio 路径。"
}

<#
.SYNOPSIS
查找 CMake 可执行文件。
.PARAMETER PreferredPath
用户指定的首选路径。
.OUTPUTS
返回 cmake.exe 的完整路径。
#>
function Find-CMakePath {
    param(
        # 用户指定的首选路径。
        [string]$PreferredPath
    )

    if (-not [string]::IsNullOrWhiteSpace($PreferredPath)) {
        Assert-FileExists -Path $PreferredPath -Description "cmake.exe"
        return (Resolve-Path -LiteralPath $PreferredPath).Path
    }

    $knownCandidates = @(
        "D:\Program Files\Microsoft Visual Studio\18\Enterprise\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe",
        "D:\Program Files\Microsoft Visual Studio\2022\Enterprise\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe",
        "C:\Program Files\Microsoft Visual Studio\2022\Enterprise\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
    )
    foreach ($candidate in $knownCandidates) {
        if (Test-Path -LiteralPath $candidate -PathType Leaf) {
            return $candidate
        }
    }

    $command = Get-Command cmake.exe -ErrorAction SilentlyContinue
    if ($null -ne $command) {
        return $command.Source
    }

    throw "未找到 cmake.exe，请使用 -CMakePath 指定 CMake 路径。"
}

<#
.SYNOPSIS
查找 Qt bin 目录。
.PARAMETER PreferredPath
用户指定的首选路径。
.OUTPUTS
返回 Qt bin 目录完整路径。
#>
function Find-QtBinPath {
    param(
        # 用户指定的首选路径。
        [string]$PreferredPath
    )

    if (-not [string]::IsNullOrWhiteSpace($PreferredPath)) {
        Assert-DirectoryExists -Path $PreferredPath -Description "Qt bin 目录"
        Assert-FileExists -Path (Join-Path $PreferredPath "windeployqt.exe") -Description "windeployqt.exe"
        return (Resolve-Path -LiteralPath $PreferredPath).Path
    }

    $knownCandidates = @(
        "E:\Qt\6.8.3\msvc2022_64\bin",
        "C:\Qt\6.8.3\msvc2022_64\bin"
    )
    foreach ($candidate in $knownCandidates) {
        if (Test-Path -LiteralPath (Join-Path $candidate "windeployqt.exe") -PathType Leaf) {
            return $candidate
        }
    }

    $command = Get-Command windeployqt.exe -ErrorAction SilentlyContinue
    if ($null -ne $command) {
        return Split-Path -Parent $command.Source
    }

    throw "未找到 Qt bin 目录，请使用 -QtBinPath 指定，例如 E:\Qt\6.8.3\msvc2022_64\bin。"
}

<#
.SYNOPSIS
在 Visual Studio 开发环境中运行命令。
.PARAMETER VcVars
vcvars64.bat 的完整路径。
.PARAMETER Command
需要在开发环境中执行的命令。
#>
function Invoke-DeveloperCommand {
    param(
        # vcvars64.bat 的完整路径。
        [string]$VcVars,

        # 需要在开发环境中执行的命令。
        [string]$Command
    )

    $fullCommand = 'call ' + (ConvertTo-CmdArgument $VcVars) + ' && ' + $Command
    & $env:ComSpec /d /s /c $fullCommand
    if ($LASTEXITCODE -ne 0) {
        throw "命令执行失败：$Command"
    }
}

<#
.SYNOPSIS
配置并构建 Qt 版应用。
.PARAMETER CMake
CMake 可执行文件路径。
.PARAMETER VcVars
vcvars64.bat 路径。
.PARAMETER QtBin
Qt bin 目录路径。
#>
function Build-QtApplication {
    param(
        # CMake 可执行文件路径。
        [string]$CMake,

        # vcvars64.bat 路径。
        [string]$VcVars,

        # Qt bin 目录路径。
        [string]$QtBin
    )

    $buildPath = Join-Path $script:ProjectRoot $BuildDir
    $qtPrefix = Split-Path -Parent $QtBin
    $cachePath = Join-Path $buildPath "CMakeCache.txt"

    if (-not (Test-Path -LiteralPath $cachePath -PathType Leaf)) {
        Write-Step "配置 CMake"
        $configureCommand = (ConvertTo-CmdArgument $CMake) +
            " -S " + (ConvertTo-CmdArgument $script:ProjectRoot) +
            " -B " + (ConvertTo-CmdArgument $buildPath) +
            " -DBUILD_QT_UI=ON" +
            " -DCMAKE_PREFIX_PATH=" + (ConvertTo-CmdArgument $qtPrefix)
        Invoke-DeveloperCommand -VcVars $VcVars -Command $configureCommand
    }

    Write-Step "构建 Qt 版应用"
    $buildCommand = (ConvertTo-CmdArgument $CMake) +
        " --build " + (ConvertTo-CmdArgument $buildPath) +
        " --config " + (ConvertTo-CmdArgument $Configuration) +
        " --target DiskLens"
    Invoke-DeveloperCommand -VcVars $VcVars -Command $buildCommand
}

<#
.SYNOPSIS
复制 VC 运行库到便携目录。
.PARAMETER VcVars
vcvars64.bat 路径。
.PARAMETER TargetDir
运行库复制目标目录。
#>
function Copy-CompilerRuntimeFiles {
    param(
        # vcvars64.bat 路径。
        [string]$VcVars,

        # 运行库复制目标目录。
        [string]$TargetDir
    )

    $buildDir = Split-Path -Parent $VcVars
    $auxiliaryDir = Split-Path -Parent $buildDir
    $vcDir = Split-Path -Parent $auxiliaryDir
    $installRoot = Split-Path -Parent $vcDir
    $ideDir = Join-Path $installRoot "Common7\IDE"
    $redistRoot = Join-Path $installRoot "VC\Redist\MSVC"
    $runtimeNames = @(
        "msvcp140.dll",
        "vcruntime140.dll",
        "vcruntime140_1.dll",
        "msvcp140_1.dll",
        "msvcp140_2.dll",
        "concrt140.dll"
    )

    foreach ($runtimeName in $runtimeNames) {
        $targetPath = Join-Path $TargetDir $runtimeName
        if (Test-Path -LiteralPath $targetPath -PathType Leaf) {
            continue
        }

        $sourcePath = Join-Path $ideDir $runtimeName
        if (-not (Test-Path -LiteralPath $sourcePath -PathType Leaf) -and (Test-Path -LiteralPath $redistRoot -PathType Container)) {
            $redistCandidate = Get-ChildItem -Path $redistRoot -Recurse -Filter $runtimeName -ErrorAction SilentlyContinue |
                Where-Object { $_.FullName -match "\\x64\\Microsoft\.VC" } |
                Sort-Object FullName -Descending |
                Select-Object -First 1
            if ($null -ne $redistCandidate) {
                $sourcePath = $redistCandidate.FullName
            }
        }
        if (-not (Test-Path -LiteralPath $sourcePath -PathType Leaf)) {
            $sourcePath = Join-Path $env:WINDIR "System32\$runtimeName"
        }

        Assert-FileExists -Path $sourcePath -Description "VC runtime"
        Copy-Item -LiteralPath $sourcePath -Destination $targetPath -Force
    }
}

<#
.SYNOPSIS
生成纯净便携版目录。
.PARAMETER QtBin
Qt bin 目录路径。
.PARAMETER VcVars
vcvars64.bat 路径。
.OUTPUTS
返回便携版主程序路径。
#>
function New-PortablePackage {
    param(
        # Qt bin 目录路径。
        [string]$QtBin,

        # vcvars64.bat 路径。
        [string]$VcVars
    )

    Write-Step "生成便携版目录"
    $portablePath = Join-Path $script:ProjectRoot $PortableDir
    $buildPath = Join-Path $script:ProjectRoot $BuildDir
    $sourceExe = Join-Path $buildPath "磁盘洞察.exe"
    if (-not (Test-Path -LiteralPath $sourceExe -PathType Leaf)) {
        $sourceCandidate = Get-ChildItem -LiteralPath $buildPath -Filter "*.exe" -File |
            Where-Object { $_.Name -notlike "*Native*" } |
            Sort-Object LastWriteTime -Descending |
            Select-Object -First 1
        if ($null -ne $sourceCandidate) {
            $sourceExe = $sourceCandidate.FullName
            Copy-Item -LiteralPath $sourceExe -Destination (Join-Path $buildPath "磁盘洞察.exe") -Force
            $sourceExe = Join-Path $buildPath "磁盘洞察.exe"
        }
    }
    $portableExe = Join-Path $portablePath "磁盘洞察.exe"
    $deployTool = Join-Path $QtBin "windeployqt.exe"

    Assert-FileExists -Path $sourceExe -Description "Qt 构建产物"
    Reset-Directory -Path $portablePath
    Copy-Item -LiteralPath $sourceExe -Destination $portableExe -Force

    $deployCommand = (ConvertTo-CmdArgument $deployTool) +
        " --release --no-compiler-runtime --no-translations --no-opengl-sw --no-system-d3d-compiler --no-system-dxc-compiler" +
        " --dir " + (ConvertTo-CmdArgument $portablePath) +
        " " + (ConvertTo-CmdArgument $portableExe) +
        " 2>nul"
    Invoke-DeveloperCommand -VcVars $VcVars -Command $deployCommand |
        ForEach-Object {
            Write-Info $_.ToString()
        }
    Copy-CompilerRuntimeFiles -VcVars $VcVars -TargetDir $portablePath
    Get-ChildItem -LiteralPath $portablePath -Filter "vc_redist*.exe" -File -ErrorAction SilentlyContinue |
        Remove-Item -Force

    return $portableExe
}

<#
.SYNOPSIS
生成便携版 zip 压缩包。
.PARAMETER PortablePath
便携版目录路径。
#>
function New-PortableZip {
    param(
        # 便携版目录路径。
        [string]$PortablePath
    )

    if ($SkipZip) {
        Write-Info "已跳过便携版 zip。"
        return
    }

    Write-Step "生成便携版 zip"
    $zipPath = Join-Path $script:ProjectRoot $PortableZip
    if (Test-Path -LiteralPath $zipPath) {
        Remove-Item -LiteralPath $zipPath -Force
    }
    Compress-Archive -Path (Join-Path $PortablePath "*") -DestinationPath $zipPath -CompressionLevel Optimal
}

<#
.SYNOPSIS
用压缩器压缩单个 payload 文件。
.PARAMETER Compressor
payload_compressor.exe 路径。
.PARAMETER Source
需要压缩的源文件。
.PARAMETER Target
压缩后的目标文件。
#>
function Compress-PayloadFile {
    param(
        # payload_compressor.exe 路径。
        [string]$Compressor,

        # 需要压缩的源文件。
        [string]$Source,

        # 压缩后的目标文件。
        [string]$Target
    )

    Assert-FileExists -Path $Source -Description "单文件 payload 源文件"
    & $Compressor $Source $Target
    if ($LASTEXITCODE -ne 0) {
        throw "压缩 payload 失败：$Source"
    }
}

<#
.SYNOPSIS
生成单文件版 exe。
.PARAMETER VcVars
vcvars64.bat 路径。
.PARAMETER PortableExe
便携版主程序路径。
#>
function New-OneFilePackage {
    param(
        # vcvars64.bat 路径。
        [string]$VcVars,

        # 便携版主程序路径。
        [string]$PortableExe
    )

    if ($SkipOneFile) {
        Write-Info "已跳过单文件版。"
        return
    }

    Write-Step "生成单文件版"
    $payloadDir = Join-Path $script:ProjectRoot "build-onefile-payload"
    $portablePath = Split-Path -Parent $PortableExe
    $compressor = Join-Path $payloadDir "payload_compressor.exe"
    $resourceOutput = Join-Path $payloadDir "onefile_resources.res"
    $oneFileOutput = Join-Path $script:ProjectRoot $OneFileExe

    Reset-Directory -Path $payloadDir
    Copy-Item -LiteralPath (Join-Path $script:ProjectRoot "src\app\assets\app.ico") -Destination (Join-Path $payloadDir "app.ico") -Force

    $compressorCommand = "cl /nologo /std:c++20 /O2 /EHsc /utf-8 " +
        (ConvertTo-CmdArgument (Join-Path $script:ProjectRoot "tools\payload_compressor.cpp")) +
        " /Fo:" + (ConvertTo-CmdArgument (Join-Path $payloadDir "payload_compressor.obj")) +
        " /Fe:" + (ConvertTo-CmdArgument $compressor) +
        " Cabinet.lib"
    Invoke-DeveloperCommand -VcVars $VcVars -Command $compressorCommand

    Compress-PayloadFile -Compressor $compressor -Source $PortableExe -Target (Join-Path $payloadDir "app.exe.cmp")

    $rootPayloads = @(
        "Qt6Core.dll",
        "Qt6Gui.dll",
        "Qt6Widgets.dll",
        "msvcp140.dll",
        "vcruntime140.dll",
        "vcruntime140_1.dll",
        "msvcp140_1.dll",
        "msvcp140_2.dll",
        "concrt140.dll"
    )
    foreach ($fileName in $rootPayloads) {
        Compress-PayloadFile -Compressor $compressor -Source (Join-Path $portablePath $fileName) -Target (Join-Path $payloadDir "$fileName.cmp")
    }

    Compress-PayloadFile -Compressor $compressor -Source (Join-Path $portablePath "platforms\qwindows.dll") -Target (Join-Path $payloadDir "qwindows.dll.cmp")
    Compress-PayloadFile -Compressor $compressor -Source (Join-Path $portablePath "styles\qmodernwindowsstyle.dll") -Target (Join-Path $payloadDir "qmodernwindowsstyle.dll.cmp")

    $resourceCommand = "rc /nologo /fo " + (ConvertTo-CmdArgument $resourceOutput) + " " + (ConvertTo-CmdArgument (Join-Path $script:ProjectRoot "tools\onefile_resources.rc"))
    Invoke-DeveloperCommand -VcVars $VcVars -Command $resourceCommand

    if (Test-Path -LiteralPath $oneFileOutput) {
        Remove-Item -LiteralPath $oneFileOutput -Force
    }
    $launcherCommand = "cl /nologo /std:c++20 /O2 /EHsc /MT /utf-8 /DUNICODE /D_UNICODE " +
        (ConvertTo-CmdArgument (Join-Path $script:ProjectRoot "tools\onefile_launcher.cpp")) + " " +
        (ConvertTo-CmdArgument $resourceOutput) +
        " /Fo:" + (ConvertTo-CmdArgument (Join-Path $payloadDir "onefile_launcher.obj")) +
        " /link /SUBSYSTEM:WINDOWS /MANIFEST:NO /OUT:" + (ConvertTo-CmdArgument $oneFileOutput) +
        " shell32.lib user32.lib advapi32.lib ole32.lib Cabinet.lib"
    Invoke-DeveloperCommand -VcVars $VcVars -Command $launcherCommand

    Remove-Item -LiteralPath $payloadDir -Recurse -Force
}

<#
.SYNOPSIS
检查文件是否为 UTF-8 无 BOM。
.PARAMETER Path
需要检查的文件路径。
#>
function Test-Utf8NoBom {
    param(
        # 需要检查的文件路径。
        [string]$Path
    )

    $bytes = [System.IO.File]::ReadAllBytes((Resolve-Path -LiteralPath $Path))
    $hasBom = $bytes.Length -ge 3 -and $bytes[0] -eq 0xEF -and $bytes[1] -eq 0xBB -and $bytes[2] -eq 0xBF
    if ($hasBom) {
        throw "文件包含 UTF-8 BOM：$Path"
    }
}

<#
.SYNOPSIS
检查 exe 是否包含可提取图标。
.PARAMETER Path
需要检查的 exe 路径。
#>
function Test-ExecutableIcon {
    param(
        # 需要检查的 exe 路径。
        [string]$Path
    )

    Add-Type -AssemblyName System.Drawing
    $resolved = (Resolve-Path -LiteralPath $Path).Path
    $icon = [System.Drawing.Icon]::ExtractAssociatedIcon($resolved)
    if ($null -eq $icon) {
        throw "未能提取 exe 图标：$Path"
    }
    Write-Info "$Path 图标：$($icon.Width)x$($icon.Height)"
    $icon.Dispose()
}

<#
.SYNOPSIS
执行发布打包主流程。
#>
function Invoke-PackageRelease {
    Write-Step "解析工具路径"
    $resolvedVcVars = Find-VcVarsPath -PreferredPath $VcVarsPath
    $resolvedCMake = Find-CMakePath -PreferredPath $CMakePath
    $resolvedQtBin = Find-QtBinPath -PreferredPath $QtBinPath
    Write-Info "Visual Studio：$resolvedVcVars"
    Write-Info "CMake：$resolvedCMake"
    Write-Info "Qt：$resolvedQtBin"

    if (-not $SkipBuild) {
        Build-QtApplication -CMake $resolvedCMake -VcVars $resolvedVcVars -QtBin $resolvedQtBin
    } else {
        Write-Info "已跳过 CMake 构建。"
    }

    $portableExe = New-PortablePackage -QtBin $resolvedQtBin -VcVars $resolvedVcVars
    New-PortableZip -PortablePath (Split-Path -Parent $portableExe)
    New-OneFilePackage -VcVars $resolvedVcVars -PortableExe $portableExe

    Write-Step "验证产物"
    Test-Utf8NoBom -Path (Join-Path $script:ProjectRoot "scripts\package-release.ps1")
    Test-ExecutableIcon -Path $portableExe
    if (-not $SkipOneFile) {
        Test-ExecutableIcon -Path (Join-Path $script:ProjectRoot $OneFileExe)
    }

    $artifactPaths = @(
        (Join-Path $script:ProjectRoot $PortableDir)
    )
    if (-not $SkipZip) {
        $artifactPaths += (Join-Path $script:ProjectRoot $PortableZip)
    }
    if (-not $SkipOneFile) {
        $artifactPaths += (Join-Path $script:ProjectRoot $OneFileExe)
    }

    Get-Item -LiteralPath $artifactPaths -ErrorAction SilentlyContinue |
        Select-Object Name, Length, LastWriteTime |
        Format-Table -AutoSize

    Write-Step "打包完成"
}

Invoke-PackageRelease
