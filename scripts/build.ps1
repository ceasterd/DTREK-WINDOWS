<#
.SYNOPSIS
Builds the modern DTREK native core, managed wrapper, console smoke test,
and WPF front-end.

.PARAMETER Configuration
Debug or Release (default Release).

.PARAMETER SkipNative
Skip the CMake build. The native DLL must already exist in artifacts\native.

.PARAMETER Test
Run native CTest and the managed console smoke test after building.

.PARAMETER Package
Create a self-contained win-x64 publish, a portable ZIP, and an Inno Setup
installer under dist\. Requires Inno Setup 6.

.PARAMETER Run
Launch the WPF app after building (the script waits until it closes).

.EXAMPLE
.\scripts\build.ps1 -Test

.EXAMPLE
.\scripts\build.ps1 -Test -Package

.EXAMPLE
.\scripts\build.ps1 -Run
#>
[CmdletBinding()]
param(
    [ValidateSet('Debug', 'Release')]
    [string]$Configuration = 'Release',

    [switch]$SkipNative,

    [switch]$Test,

    [switch]$Package,

    [switch]$Run
)

$ErrorActionPreference = 'Stop'

$root = Split-Path -Parent $PSScriptRoot
$nativeDir = Join-Path $root 'native'
$buildDir = Join-Path $nativeDir 'build'
$artifactDll = Join-Path $root 'artifacts\native\dtrek_core.dll'
$distDir = Join-Path $root 'dist'

$versionNode = Select-Xml -Path (Join-Path $root 'Directory.Build.props') `
    -XPath '/Project/PropertyGroup/DtrekVersion' | Select-Object -First 1
if (-not $versionNode) {
    throw 'Directory.Build.props does not define DtrekVersion.'
}
$version = $versionNode.Node.InnerText.Trim()

if ($Package -and $Configuration -ne 'Release') {
    throw 'Packaging requires Release configuration.'
}

function Invoke-Checked {
    param(
        [Parameter(Mandatory)][string]$FilePath,
        [Parameter()][string[]]$Arguments = @()
    )

    & $FilePath @Arguments
    if ($LASTEXITCODE -ne 0) {
        throw "'$FilePath' failed with exit code $LASTEXITCODE."
    }
}

function Find-CMake {
    $command = Get-Command cmake -ErrorAction SilentlyContinue
    if ($command) {
        return $command.Source
    }

    $candidates = @(
        (Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\2022\BuildTools\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'),
        (Join-Path $env:ProgramFiles 'Microsoft Visual Studio\2022\BuildTools\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'),
        (Join-Path $env:ProgramFiles 'Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'),
        (Join-Path $env:ProgramFiles 'Microsoft Visual Studio\2022\Professional\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe')
    )

    foreach ($candidate in $candidates) {
        if (Test-Path -LiteralPath $candidate) {
            return $candidate
        }
    }

    throw 'CMake was not found. Install Visual Studio 2022 C++ tools or CMake 3.20+ and retry.'
}

function Find-Dotnet {
    $command = Get-Command dotnet -ErrorAction SilentlyContinue
    if ($command) {
        return $command.Source
    }

    $candidate = Join-Path $env:ProgramFiles 'dotnet\dotnet.exe'
    if (Test-Path -LiteralPath $candidate) {
        return $candidate
    }

    throw '.NET SDK was not found. Install the .NET 10 SDK and retry.'
}

function Find-InnoSetup {
    $command = Get-Command ISCC.exe -ErrorAction SilentlyContinue
    if ($command) {
        return $command.Source
    }

    $candidates = @(
        (Join-Path $env:LOCALAPPDATA 'Programs\Inno Setup 6\ISCC.exe'),
        (Join-Path ${env:ProgramFiles(x86)} 'Inno Setup 6\ISCC.exe'),
        (Join-Path $env:ProgramFiles 'Inno Setup 6\ISCC.exe')
    )

    foreach ($candidate in $candidates) {
        if (Test-Path -LiteralPath $candidate) {
            return $candidate
        }
    }

    throw 'Inno Setup 6 was not found. Install it and retry, for example: winget install JRSoftware.InnoSetup'
}

$cmake = Find-CMake
$dotnet = Find-Dotnet
$ctest = Join-Path (Split-Path -Parent $cmake) 'ctest.exe'

Write-Host "DTREK modern build" -ForegroundColor Cyan
Write-Host "  configuration : $Configuration"
Write-Host "  version       : $version"
Write-Host "  root          : $root"
Write-Host "  cmake         : $cmake"
Write-Host "  dotnet        : $dotnet"
Write-Host

if (-not $SkipNative) {
    Write-Host '[1/3] Configuring native core' -ForegroundColor Yellow
    Invoke-Checked $cmake @(
        '-S', $nativeDir,
        '-B', $buildDir,
        '-G', 'Visual Studio 17 2022',
        '-A', 'x64'
    )

    Write-Host '[2/3] Building native core' -ForegroundColor Yellow
    Invoke-Checked $cmake @(
        '--build', $buildDir,
        '--config', $Configuration,
        '--parallel'
    )
}
else {
    Write-Host '[1/3] Skipping native build (-SkipNative)' -ForegroundColor DarkYellow
    Write-Host '[2/3] Skipping native build (-SkipNative)' -ForegroundColor DarkYellow
    if (-not (Test-Path -LiteralPath $artifactDll)) {
        throw "Native DLL not found at '$artifactDll'. Run without -SkipNative first."
    }
}

Write-Host '[3/3] Building managed solution' -ForegroundColor Yellow
Invoke-Checked $dotnet @(
    'build', (Join-Path $root 'Dtrek.sln'),
    '-c', $Configuration,
    '-v', 'minimal'
)

$consoleExe = Join-Path $root "src\Dtrek.Console\bin\$Configuration\net10.0\Dtrek.Console.exe"
$appExe = Join-Path $root "src\Dtrek.App\bin\$Configuration\net10.0-windows\Dtrek.App.exe"

if ($Test) {
    Write-Host
    Write-Host '[test] Native core checks' -ForegroundColor Yellow
    Invoke-Checked $ctest @(
        '--test-dir', $buildDir,
        '-C', $Configuration,
        '--output-on-failure'
    )

    Write-Host
    Write-Host '[test] Managed interop smoke test' -ForegroundColor Yellow
    Invoke-Checked $consoleExe
}

if ($Package) {
    if (-not (Test-Path -LiteralPath $artifactDll)) {
        throw "Native DLL not found at '$artifactDll'. Run without -SkipNative first."
    }

    $iscc = Find-InnoSetup
    $publishDir = Join-Path $distDir "DTREK-$version-win-x64"
    $zipPath = "$publishDir.zip"
    $installerPath = Join-Path $distDir "DTREK-$version-win-x64-setup.exe"

    Write-Host
    Write-Host '[package] Publishing self-contained win-x64 app' -ForegroundColor Yellow
    if (Test-Path -LiteralPath $publishDir) {
        Remove-Item -LiteralPath $publishDir -Recurse -Force
    }
    if (Test-Path -LiteralPath $zipPath) {
        Remove-Item -LiteralPath $zipPath -Force
    }
    if (Test-Path -LiteralPath $installerPath) {
        Remove-Item -LiteralPath $installerPath -Force
    }
    New-Item -ItemType Directory -Path $distDir -Force | Out-Null

    Invoke-Checked $dotnet @(
        'publish', (Join-Path $root 'src\Dtrek.App\Dtrek.App.csproj'),
        '-c', $Configuration,
        '-r', 'win-x64',
        '--self-contained', 'true',
        '-p:PublishSingleFile=false',
        '-p:PublishTrimmed=false',
        '-p:DebugType=None',
        '-p:DebugSymbols=false',
        '-o', $publishDir,
        '-v', 'minimal'
    )

    $publishedDll = Join-Path $publishDir 'dtrek_core.dll'
    if (-not (Test-Path -LiteralPath $publishedDll)) {
        throw "Published app is missing dtrek_core.dll at '$publishedDll'."
    }

    Write-Host '[package] Creating portable ZIP' -ForegroundColor Yellow
    Compress-Archive -Path $publishDir -DestinationPath $zipPath -CompressionLevel Optimal

    Write-Host '[package] Building Inno Setup installer' -ForegroundColor Yellow
    Invoke-Checked $iscc @(
        "/DMyAppVersion=$version",
        (Join-Path $root 'installer\dtrek.iss')
    )

    if (-not (Test-Path -LiteralPath $installerPath)) {
        throw "Installer was not created at '$installerPath'."
    }
}

Write-Host
Write-Host 'Build complete.' -ForegroundColor Green
Write-Host "  app     : $appExe"
Write-Host "  console : $consoleExe"
if ($Package) {
    Write-Host "  setup   : $installerPath"
    Write-Host "  portable: $zipPath"
}
Write-Host '  run     : .\scripts\build.ps1 -Run'

if ($Run) {
    if (-not (Test-Path -LiteralPath $appExe)) {
        throw "App executable not found at '$appExe'."
    }

    Write-Host
    Write-Host 'Launching DTREK...' -ForegroundColor Cyan
    & $appExe
}
