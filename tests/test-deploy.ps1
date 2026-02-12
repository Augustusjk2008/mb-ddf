#Requires -Version 5.1
<#
.SYNOPSIS
    构建并部署MB_DDF测试到目标板执行

.DESCRIPTION
    1. 构建测试二进制（交叉编译到AArch64）
    2. 部署到目标板
    3. 执行测试并输出结果

.PARAMETER RemoteHost
    目标板IP地址，默认192.168.1.29

.PARAMETER RemoteDir
    目标板测试目录，默认/home/sast8/user_tests

.PARAMETER TestFilter
    gtest测试过滤器，默认*（全部）

.PARAMETER BuildOnly
    仅构建不上传执行

.EXAMPLE
    .\test-deploy.ps1
    .\test-deploy.ps1 -TestFilter "Message*"
    .\test-deploy.ps1 -RemoteHost 192.168.1.29
#>
param(
    [string]$RemoteHost = "192.168.1.29",
    [string]$RemoteDir = "/home/sast8/user_tests",
    [string]$TestFilter = "*",
    [switch]$BuildOnly
)

$ErrorActionPreference = "Stop"
$ProjectRoot = Split-Path -Parent $PSScriptRoot

# 颜色输出
function Write-Info($msg) { Write-Host "[INFO] $msg" -ForegroundColor Green }
function Write-Step($msg) { Write-Host "`n[STEP] $msg" -ForegroundColor Cyan }
function Write-Error($msg) { Write-Host "[ERROR] $msg" -ForegroundColor Red }

Write-Step "MB_DDF Test Deploy Script"
Write-Info "Remote: $RemoteHost"
Write-Info "Target: $RemoteDir"
Write-Info "Filter: $TestFilter"

# ==============================
# 1. 构建测试
# ==============================
Write-Step "Building tests..."

# 通过环境变量传递ENABLE_TESTS给build.ps1
$env:ENABLE_TESTS = "ON"

$BuildScript = Join-Path $ProjectRoot "build.bat"
if (-not (Test-Path $BuildScript)) {
    Write-Error "build.bat not found at $BuildScript"
    exit 1
}

# 调用build.bat构建测试版本
& $BuildScript debug

if ($LASTEXITCODE -ne 0) {
    Write-Error "Build failed with exit code $LASTEXITCODE"
    exit 1
}

Write-Info "Build completed"

# ==============================
# 2. 查找测试二进制
# ==============================
Write-Step "Locating test binary..."

$TestBinary = Get-ChildItem -Path "$ProjectRoot\build\aarch64" -Name "MB_DDF_Tests" -Recurse -ErrorAction SilentlyContinue |
    Select-Object -First 1

if (-not $TestBinary) {
    Write-Error "Test binary MB_DDF_Tests not found in build output"
    exit 1
}

$TestBinaryPath = Join-Path "$ProjectRoot\build\aarch64" $TestBinary
Write-Info "Found: $TestBinaryPath"

if ($BuildOnly) {
    Write-Info "Build-only mode, skipping deploy"
    exit 0
}

# ==============================
# 3. 检查目标板连通性
# ==============================
Write-Step "Checking target connectivity..."

$pingResult = Test-Connection -ComputerName $RemoteHost -Count 1 -Quiet -ErrorAction SilentlyContinue
if (-not $pingResult) {
    Write-Error "Target $RemoteHost is not reachable"
    exit 1
}

Write-Info "Target is online"

# ==============================
# 4. 部署测试二进制
# ==============================
Write-Step "Deploying to target..."

# 创建远程目录
ssh root@$RemoteHost "mkdir -p $RemoteDir" 2>$null
if ($LASTEXITCODE -ne 0) {
    Write-Error "Failed to create remote directory. Check SSH key authentication."
    exit 1
}

# 上传测试文件
scp $TestBinaryPath root@${RemoteHost}:${RemoteDir}/MB_DDF_Tests
if ($LASTEXITCODE -ne 0) {
    Write-Error "Failed to upload test binary"
    exit 1
}

# 设置执行权限
ssh root@$RemoteHost "chmod +x $RemoteDir/MB_DDF_Tests" 2>$null

Write-Info "Deploy completed"

# ==============================
# 5. 执行测试
# ==============================
Write-Step "Running tests on target..."
Write-Host "----------------------------------------"

$testCmd = "cd $RemoteDir && ./MB_DDF_Tests --gtest_filter='$TestFilter' --gtest_color=yes 2>&1"
ssh root@$RemoteHost $testCmd

$testExitCode = $LASTEXITCODE
Write-Host "----------------------------------------"

# ==============================
# 6. 结果汇总
# ==============================
Write-Step "Test Summary"

if ($testExitCode -eq 0) {
    Write-Info "All tests PASSED"
} else {
    Write-Error "Some tests FAILED (exit code: $testExitCode)"
}

exit $testExitCode
