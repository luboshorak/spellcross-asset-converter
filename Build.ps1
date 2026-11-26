param(
    [switch]$InstallDependencies,
    [switch]$Clean,
    [switch]$Run,
    [string]$MSYS2 = "C:\msys64"
)

$ErrorActionPreference = "Stop"

$Root = $PSScriptRoot
$BuildDir = Join-Path $Root "build\ucrt64"
$Exe = Join-Path $BuildDir "SpellcrossConverter.exe"
$Bash = Join-Path $MSYS2 "usr\bin\bash.exe"

if (!(Test-Path $Bash)) {
    throw "MSYS2 not found: $MSYS2"
}

$env:MSYSTEM = "UCRT64"
$env:CHERE_INVOKING = "1"
$env:SC_CONVERTER_ROOT = $Root

function Invoke-MSYS2 {
    param([string]$Command)

    & $Bash -lc $Command

    if ($LASTEXITCODE -ne 0) {
        throw "Command failed: $Command"
    }
}

Write-Host ""
Write-Host "========================================" -ForegroundColor DarkGreen
Write-Host "   SPELLCROSS ASSET CONVERTER BUILD" -ForegroundColor Green
Write-Host "========================================" -ForegroundColor DarkGreen

if ($InstallDependencies) {
    Write-Host "`nInstalling dependencies..." -ForegroundColor Cyan

    Invoke-MSYS2 @'
pacman -S --needed --noconfirm \
    mingw-w64-ucrt-x86_64-gcc \
    mingw-w64-ucrt-x86_64-cmake \
    mingw-w64-ucrt-x86_64-ninja \
    mingw-w64-ucrt-x86_64-wxwidgets3.2-msw
'@
}

if ($Clean -and (Test-Path $BuildDir)) {
    Write-Host "`nCleaning previous build..." -ForegroundColor Yellow
    Remove-Item $BuildDir -Recurse -Force
}

Write-Host "`n[1/3] Configuring project..." -ForegroundColor Cyan

Invoke-MSYS2 @'
root="$(cygpath -u "$SC_CONVERTER_ROOT")"

cmake -S "$root" \
      -B "$root/build/ucrt64" \
      -G Ninja \
      -DCMAKE_BUILD_TYPE=Release \
      -DSC_CONVERTER_GUI=ON \
      -DBUILD_TESTING=ON
'@

Write-Host "`n[2/3] Building application..." -ForegroundColor Cyan

Invoke-MSYS2 @'
root="$(cygpath -u "$SC_CONVERTER_ROOT")"
cmake --build "$root/build/ucrt64" --parallel
'@

Write-Host "`n[3/3] Running regression tests..." -ForegroundColor Cyan

Invoke-MSYS2 @'
root="$(cygpath -u "$SC_CONVERTER_ROOT")"
ctest --test-dir "$root/build/ucrt64" --output-on-failure
'@

if (!(Test-Path $Exe)) {
    throw "Executable not found: $Exe"
}

Write-Host ""
Write-Host "========================================" -ForegroundColor Green
Write-Host "          BUILD SUCCESSFUL" -ForegroundColor Green
Write-Host "========================================" -ForegroundColor Green
Write-Host $Exe

if ($Run) {
    $env:PATH = "$MSYS2\ucrt64\bin;$env:PATH"
    Start-Process -FilePath $Exe -WorkingDirectory $BuildDir
}