param(
    [Parameter(Position = 0)]
    [ValidateSet("clean", "debug", "release", "help")]
    [string] $Action = "help",

    [string] $ProjectName = "UpgradeAndTest",

    [string] $Sysroot = "",

    [string] $ToolchainBin = "",

    [string] $MakePath = "",

    [string] $Arm64LibsPrefix = "",

    [int] $Jobs = 20
)

$ErrorActionPreference = "Stop"

function Show-Help {
    Write-Host "Usage:"
    Write-Host "  powershell -ExecutionPolicy Bypass -File .\build.ps1 clean"
    Write-Host "  powershell -ExecutionPolicy Bypass -File .\build.ps1 debug"
    Write-Host "  powershell -ExecutionPolicy Bypass -File .\build.ps1 release"
    Write-Host ""
    Write-Host "Options:"
    Write-Host "  -ProjectName <name>     project/exe name (default: UpgradeAndTest)"
    Write-Host "  -Sysroot <path>         aarch64 sysroot (default: derived from compiler path)"
    Write-Host "  -ToolchainBin <path>    toolchain bin directory (default: from env or compiler path)"
    Write-Host "  -MakePath <path>        make.exe path (default: from env or PATH)"
    Write-Host "  -Arm64LibsPrefix <path> third-party libs prefix (default: <script>\libs)"
    Write-Host "  -Jobs <n>               parallel build jobs (default: 8)"
    Write-Host ""
    Write-Host "Environment variables (preferred):"
    Write-Host "  CROSS_CXX_COMPILER / CXX          compiler to derive origin sysroot"
    Write-Host "  TOOLCHAIN_BIN                     toolchain bin directory"
    Write-Host "  MAKE_PATH / CMAKE_MAKE_PROGRAM     make.exe path"
}

function Resolve-ProjectRoot {
    if ($PSScriptRoot -and $PSScriptRoot.Trim() -ne "") {
        return (Resolve-Path $PSScriptRoot).Path
    }
    if ($PSCommandPath -and $PSCommandPath.Trim() -ne "") {
        return (Resolve-Path (Split-Path -Parent $PSCommandPath)).Path
    }
    if ($MyInvocation.ScriptName -and $MyInvocation.ScriptName.Trim() -ne "") {
        return (Resolve-Path (Split-Path -Parent $MyInvocation.ScriptName)).Path
    }
    
    throw "Unable to resolve script directory. Please ensure you are running this script from a file."
}

function Get-DefaultArm64LibsPrefix([string] $toolchainBin, [string] $sysroot) {
    if ($Arm64LibsPrefix -and $Arm64LibsPrefix.Trim() -ne "") {
        return $Arm64LibsPrefix
    }
    $candidates = @()
    if ($toolchainBin -and $toolchainBin.Trim() -ne "") {
        $p1 = Split-Path -Parent $toolchainBin
        $p2 = Split-Path -Parent $p1
        $p3 = Split-Path -Parent $p2
        $candidates += (Join-Path $p3 "libs")
    }
    if ($sysroot -and $sysroot.Trim() -ne "") {
        $s1 = Split-Path -Parent $sysroot
        $s2 = Split-Path -Parent $s1
        $s3 = Split-Path -Parent $s2
        $candidates += (Join-Path $s3 "libs")
    }
    foreach ($c in $candidates) {
        if ($c -and (Test-Path $c)) {
            return $c
        }
    }
    if ($candidates.Count -gt 0) {
        return $candidates[0]
    }
    return ""
}

function Resolve-PathFromEnv([string[]] $varNames) {
    foreach ($name in $varNames) {
        $value = [Environment]::GetEnvironmentVariable($name)
        if ($value -and $value.Trim() -ne "") {
            return $value
        }
    }
    return ""
}

function Get-CommandResolvedPath($cmd) {
    if ($null -eq $cmd) {
        return ""
    }
    if ($cmd.Path -and $cmd.Path.Trim() -ne "") {
        return $cmd.Path
    }
    if ($cmd.Source -and $cmd.Source.Trim() -ne "") {
        return $cmd.Source
    }
    if ($cmd.Definition -and $cmd.Definition.Trim() -ne "") {
        return $cmd.Definition
    }
    return ""
}

function Resolve-CompilerPath {
    $candidate = Resolve-PathFromEnv -varNames @("CROSS_CXX_COMPILER", "CXX", "AARCH64_CXX")
    if (-not $candidate -or $candidate.Trim() -eq "") {
        $candidate = "aarch64-none-linux-gnu-g++"
    }

    if (Test-Path $candidate) {
        return (Resolve-Path $candidate).Path
    }

    $cmd = Get-Command $candidate -ErrorAction SilentlyContinue
    if ($null -ne $cmd) {
        $resolved = Get-CommandResolvedPath $cmd
        if ($resolved -and $resolved.Trim() -ne "") {
            return $resolved
        }
    }

    throw "Compiler not found: $candidate"
}

function Resolve-ToolchainBinDir {
    if ($ToolchainBin -and $ToolchainBin.Trim() -ne "") {
        return $ToolchainBin
    }

    $fromEnv = Resolve-PathFromEnv -varNames @("TOOLCHAIN_BIN", "ARM_GNU_TOOLCHAIN_BIN", "AARCH64_TOOLCHAIN_BIN")
    if ($fromEnv -and $fromEnv.Trim() -ne "") {
        return $fromEnv
    }

    $compilerPath = Resolve-CompilerPath
    return (Split-Path -Parent $compilerPath)
}

function Resolve-OriginSysroot {
    if ($Sysroot -and $Sysroot.Trim() -ne "") {
        return $Sysroot
    }

    $compilerPath = Resolve-CompilerPath
    $binDir = Split-Path -Parent $compilerPath
    
    $installRoot1 = Split-Path -Parent $binDir
    $originSysroot1 = Join-Path $installRoot1 "origin\armv8a-ucas-linux"
    if (Test-Path $originSysroot1) {
        return $originSysroot1
    }

    $installRoot2 = Split-Path -Parent $installRoot1
    $originSysroot2 = Join-Path $installRoot2 "origin\armv8a-ucas-linux"
    if (Test-Path $originSysroot2) {
        return $originSysroot2
    }

    throw "Derived sysroot not found. Checked:`n$originSysroot1`n$originSysroot2"
}

function Resolve-MakePath {
    if ($MakePath -and $MakePath.Trim() -ne "") {
        return $MakePath
    }

    $fromEnv = Resolve-PathFromEnv -varNames @("MAKE_PATH", "CMAKE_MAKE_PROGRAM", "MAKE")
    if ($fromEnv -and $fromEnv.Trim() -ne "" -and (Test-Path $fromEnv)) {
        return $fromEnv
    }

    $makeCmd = Get-Command "make.exe" -ErrorAction SilentlyContinue
    if ($null -ne $makeCmd) {
        $resolved = Get-CommandResolvedPath $makeCmd
        if ($resolved -and $resolved.Trim() -ne "") {
            return $resolved
        }
    }

    $mingwMakeCmd = Get-Command "mingw32-make.exe" -ErrorAction SilentlyContinue
    if ($null -ne $mingwMakeCmd) {
        $resolved = Get-CommandResolvedPath $mingwMakeCmd
        if ($resolved -and $resolved.Trim() -ne "") {
            return $resolved
        }
    }

    throw "make.exe not found. Provide -MakePath or set MAKE_PATH. Also tried mingw32-make.exe."
}

function Ensure-Tool([string] $toolName, [string] $toolchainBin) {
    $toolPath = Join-Path $toolchainBin $toolName
    if (Test-Path $toolPath) {
        return $toolPath
    }
    $cmd = Get-Command $toolName -ErrorAction SilentlyContinue
    if ($null -ne $cmd) {
        $resolved = Get-CommandResolvedPath $cmd
        if ($resolved -and $resolved.Trim() -ne "") {
            return $resolved
        }
    }
    throw "Tool not found: $toolName. Searched: $toolPath and PATH."
}

function Ensure-ToolAvailable([string] $toolName) {
    $cmd = Get-Command $toolName -ErrorAction SilentlyContinue
    if ($null -ne $cmd) {
        return
    }
    throw "Tool not found in PATH: $toolName"
}

function Clean-Build([string] $projectRoot) {
    $buildRoot = Join-Path $projectRoot "build"
    if (Test-Path $buildRoot) {
        Remove-Item -Recurse -Force $buildRoot
    }
    $gdbUploaded = Join-Path $projectRoot ".vscode\.gdbserver_uploaded"
    if (Test-Path $gdbUploaded) {
        Write-Host "Cleaning .vscode\.gdbserver_uploaded..."
        Remove-Item -Force $gdbUploaded
    }
}

function Configure-And-Build([string] $projectRoot, [ValidateSet("Debug", "Release")] [string] $config) {
    $resolvedToolchainBin = Resolve-ToolchainBinDir
    $resolvedSysroot = Resolve-OriginSysroot
    $resolvedMakePath = Resolve-MakePath
    if (-not (Test-Path $resolvedSysroot)) {
        throw "Sysroot not found: $resolvedSysroot"
    }

    $arm64Libs = Get-DefaultArm64LibsPrefix -toolchainBin $resolvedToolchainBin -sysroot $resolvedSysroot
    $env:ARM64_LIBS_PREFIX = $arm64Libs

    if ($resolvedToolchainBin -and (Test-Path $resolvedToolchainBin)) {
        $env:PATH = ($resolvedToolchainBin + ";" + $env:PATH)
    }

    $gcc = "aarch64-none-linux-gnu-gcc"
    $gpp = "aarch64-none-linux-gnu-g++"
    Ensure-ToolAvailable -toolName $gcc
    Ensure-ToolAvailable -toolName $gpp
    $gccCmd = Get-Command $gcc -ErrorAction SilentlyContinue
    $gppCmd = Get-Command $gpp -ErrorAction SilentlyContinue
    $gccPathResolved = Get-CommandResolvedPath $gccCmd
    $gppPathResolved = Get-CommandResolvedPath $gppCmd

    $generator = "Unix Makefiles"
    $makeForCmake = $resolvedMakePath

    $buildDir = Join-Path $projectRoot (Join-Path "build\aarch64" $config)
    $compileDbPath = Join-Path $buildDir "compile_commands.json"

    Write-Host "==== Configure-And-Build Diagnostics ===="
    Write-Host ("ProjectRoot: " + $projectRoot)
    Write-Host ("BuildConfig: " + $config)
    Write-Host ("ProjectName: " + $ProjectName)
    Write-Host ("ToolchainBin: " + $resolvedToolchainBin)
    Write-Host ("Sysroot: " + $resolvedSysroot)
    Write-Host ("MakeProgram: " + $resolvedMakePath)
    Write-Host ("Generator: " + $generator)
    Write-Host ("ARM64_LIBS_PREFIX: " + $arm64Libs)
    Write-Host ("GCC Path: " + $gccPathResolved)
    Write-Host ("G++ Path: " + $gppPathResolved)
    Write-Host ("BuildDir: " + $buildDir)
    Write-Host ("CompileCommands: " + $compileDbPath)

    $cmakeArgs = @(
        "-S", $projectRoot,
        "-B", $buildDir,
        "-G", $generator,
        "-DCMAKE_MAKE_PROGRAM=$makeForCmake",
        "-DPROJECT_NAME_OVERRIDE=$ProjectName",
        "-DCMAKE_BUILD_TYPE=$config",
        "-DCMAKE_SYSTEM_NAME=Linux",
        "-DCMAKE_SYSTEM_PROCESSOR=aarch64",
        "-DCMAKE_SYSROOT=$resolvedSysroot",
        "-DCMAKE_C_COMPILER=$gcc",
        "-DCMAKE_CXX_COMPILER=$gpp",
        "-DCROSS_COMPILE=ON",
        "-DCROSS_SYSROOT=$resolvedSysroot",
        "-DCROSS_C_COMPILER=$gcc",
        "-DCROSS_CXX_COMPILER=$gpp"
    )
    $cmakeArgsString = ($cmakeArgs -join " ")
    Write-Host ("CMake Configure Args: " + $cmakeArgsString)

    cmake @cmakeArgs
    if ($LASTEXITCODE -ne 0) {
        throw "CMake configure failed with exit code $LASTEXITCODE"
    }
    cmake --build $buildDir -j $Jobs
    if ($LASTEXITCODE -ne 0) {
        throw "CMake build failed with exit code $LASTEXITCODE"
    }

    $vsCodeDir = Join-Path $projectRoot ".vscode"
    if (-not (Test-Path $vsCodeDir)) {
        New-Item -ItemType Directory -Path $vsCodeDir -Force | Out-Null
    }

    $compileDbSourcePath = $compileDbPath
    if (-not (Test-Path $compileDbSourcePath)) {
        $foundCompileDb = Get-ChildItem -Path $buildDir -Recurse -ErrorAction SilentlyContinue |
            Where-Object { -not $_.PSIsContainer -and $_.Name -ieq "compile_commands.json" } |
            Select-Object -First 1
        if ($foundCompileDb) {
            $compileDbSourcePath = $foundCompileDb.FullName
        }
    }

    if (Test-Path $compileDbSourcePath) {
        $compileDbDestPath = Join-Path $vsCodeDir "compile_commands.json"
        Copy-Item -Path $compileDbSourcePath -Destination $compileDbDestPath -Force
        Write-Host "Copied compile_commands.json to: $compileDbDestPath"
    }
    else {
        Write-Host "compile_commands.json not found under build directory: $buildDir"
    }

    $targetBaseName = $ProjectName
    $candidates = @(
        (Join-Path $buildDir $targetBaseName),
        (Join-Path $buildDir ($targetBaseName + ".exe")),
        (Join-Path (Join-Path $buildDir "bin") $targetBaseName),
        (Join-Path (Join-Path $buildDir "bin") ($targetBaseName + ".exe")),
        (Join-Path (Join-Path $buildDir $config) $targetBaseName),
        (Join-Path (Join-Path $buildDir $config) ($targetBaseName + ".exe"))
    )

    $exePath = ""
    foreach ($candidate in $candidates) {
        if ($candidate -and (Test-Path $candidate)) {
            $exePath = $candidate
            break
        }
    }

    if (-not $exePath -or $exePath.Trim() -eq "") {
        $found = Get-ChildItem -Path $buildDir -Recurse -ErrorAction SilentlyContinue |
            Where-Object { -not $_.PSIsContainer -and ($_.Name -eq $targetBaseName -or $_.Name -eq ($targetBaseName + ".exe")) } |
            Select-Object -First 1
        if ($found) {
            $exePath = $found.FullName
        }
    }

    if (-not $exePath -or $exePath.Trim() -eq "" -or -not (Test-Path $exePath)) {
        $checkedList = ($candidates -join "`n")
        throw "Build completed but executable not found. Checked:`n$checkedList`nSearched under: $buildDir"
    }
    Write-Host "Built: $exePath"
}

$projectRoot = Resolve-ProjectRoot

switch ($Action) {
    "help" {
        Show-Help
        exit 0
    }
    "clean" {
        Clean-Build -projectRoot $projectRoot
        Write-Host "Cleaned build outputs."
        exit 0
    }
    "debug" {
        Configure-And-Build -projectRoot $projectRoot -config "Debug"
        exit 0
    }
    "release" {
        Configure-And-Build -projectRoot $projectRoot -config "Release"
        exit 0
    }
}
