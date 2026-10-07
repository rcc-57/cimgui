# Run on Windows. Build only: does not open COM ports or launch the GUI.
$ErrorActionPreference = 'Stop'
if (-not (Get-Command cmake -ErrorAction SilentlyContinue)) {
    throw 'Install CMake >= 3.21 and add it to PATH.'
}
$repoRoot = Split-Path -Parent $PSScriptRoot
$buildDir = Join-Path $repoRoot 'build/measurement-windows'
if (-not (Test-Path (Join-Path $repoRoot 'imgui/imgui.cpp'))) {
    throw 'Dear ImGui missing. Run git submodule update --init --recursive in the repository root.'
}
& cmake -S $PSScriptRoot -B $buildDir -G 'Visual Studio 17 2022' -A x64 -DBUILD_TESTING=ON
if ($LASTEXITCODE -ne 0) { throw 'CMake configuration failed.' }
& cmake --build $buildDir --config Release --parallel
if ($LASTEXITCODE -ne 0) { throw 'Build failed.' }
& ctest --test-dir $buildDir -C Release --output-on-failure
if ($LASTEXITCODE -ne 0) { throw 'Protocol tests failed.' }
Write-Host ('Built: ' + (Join-Path $buildDir 'Release/measurement_control.exe'))
