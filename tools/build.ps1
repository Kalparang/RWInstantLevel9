[CmdletBinding()]
param(
    [string]$CMake = 'cmake',
    [string]$Python = 'python',
    [string]$Generator = 'Visual Studio 17 2022',
    [string]$BuildDirectory,
    [string]$DistDirectory
)
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
Push-Location $root
try {
    if (-not $BuildDirectory) { $BuildDirectory = Join-Path $root 'build' }
    if (-not $DistDirectory) { $DistDirectory = Join-Path $root 'dist' }
    $BuildDirectory = [IO.Path]::GetFullPath($BuildDirectory)
    $DistDirectory = [IO.Path]::GetFullPath($DistDirectory)
    $cmakePath = (Get-Command $CMake -CommandType Application -ErrorAction Stop | Select-Object -First 1).Source
    $ctestPath = Join-Path (Split-Path $cmakePath) 'ctest.exe'
    if (-not (Test-Path -LiteralPath $ctestPath -PathType Leaf)) {
        $ctestPath = (Get-Command ctest -CommandType Application -ErrorAction Stop | Select-Object -First 1).Source
    }
    $pythonPath = (Get-Command $Python -CommandType Application -ErrorAction Stop | Select-Object -First 1).Source
    & $cmakePath -S $root -B $BuildDirectory -G $Generator -A x64 -DBUILD_TESTING=ON
    if ($LASTEXITCODE) { throw 'CMake configuration failed.' }
    & $cmakePath --build $BuildDirectory --config Release
    if ($LASTEXITCODE) { throw 'Native build failed.' }
    & $ctestPath --test-dir $BuildDirectory -C Release --output-on-failure
    if ($LASTEXITCODE) { throw 'Native tests failed.' }
    $testTemp = Join-Path $BuildDirectory 'test-temp'
    New-Item -ItemType Directory -Path $testTemp -Force | Out-Null
    $previousTemp = $env:TEMP
    $previousTmp = $env:TMP
    $previousBytecode = $env:PYTHONDONTWRITEBYTECODE
    try {
        $env:TEMP = $testTemp
        $env:TMP = $testTemp
        $env:PYTHONDONTWRITEBYTECODE = '1'
        & $pythonPath -B -m unittest discover -s tests -v
        if ($LASTEXITCODE) { throw 'Python profile tests failed.' }
    } finally {
        $env:TEMP = $previousTemp
        $env:TMP = $previousTmp
        $env:PYTHONDONTWRITEBYTECODE = $previousBytecode
    }
    New-Item -ItemType Directory -Path $DistDirectory -Force | Out-Null
    $builtDll = Join-Path $DistDirectory 'winhttp.dll'
    Copy-Item -LiteralPath (Join-Path $BuildDirectory 'Release\winhttp.dll') -Destination $builtDll -Force
    $hash = (Get-FileHash -LiteralPath $builtDll -Algorithm SHA256).Hash
    "$hash  winhttp.dll" | Set-Content -LiteralPath (Join-Path $DistDirectory 'SHA256SUMS.txt') -Encoding ascii
    Write-Output "Built and tested $builtDll ($hash). The repository's release DLL was not replaced."
} finally { Pop-Location }
