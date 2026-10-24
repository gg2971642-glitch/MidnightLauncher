$ErrorActionPreference = "Stop"

$root = Split-Path -Parent $MyInvocation.MyCommand.Path

# CMake/Ninja: из PATH, иначе из Python-окружения (pip-пакеты)
$py = (Get-Command py -ErrorAction SilentlyContinue).Source
if (-not $py) { $py = (Get-Command python -ErrorAction SilentlyContinue).Source }
if ($py) {
    $pyScripts = & $py -c "import sysconfig; print(sysconfig.get_path('scripts'))" 2>$null
    if ($pyScripts -and (Test-Path $pyScripts)) { $env:PATH = "$pyScripts;$env:PATH" }
}

# MinGW-w64: локальный тулчейн, иначе g++ из PATH
$mingw = Join-Path $env:LOCALAPPDATA "opencode-tools\mingw64\bin"
if (-not (Test-Path (Join-Path $mingw "g++.exe"))) {
    $g = Get-Command g++ -ErrorAction SilentlyContinue
    if ($g) { $mingw = Split-Path -Parent $g.Source }
}
if (-not (Test-Path (Join-Path $mingw "g++.exe"))) {
    throw "MinGW-w64 not found: install it or add g++.exe to PATH"
}

$env:PATH = "$mingw;$env:PATH"

$cmake = (Get-Command cmake -ErrorAction SilentlyContinue).Source
if (-not $cmake -and -not $py) {
    throw "CMake not found: install CMake or Python with the cmake package"
}
if (-not (Get-Command ninja -ErrorAction SilentlyContinue)) {
    throw "Ninja not found: install Ninja or add ninja.exe to PATH"
}

$cmakeArgs = @(
    "-S", $root,
    "-B", "$root\build-cmake",
    "-G", "Ninja",
    "-DCMAKE_BUILD_TYPE=Release",
    "-DCMAKE_CXX_COMPILER=$mingw\g++.exe"
)
if ($cmake) { & $cmake @cmakeArgs } else { & $py -m cmake @cmakeArgs }
if ($LASTEXITCODE -ne 0) { throw "cmake configure failed" }

if ($cmake) { & $cmake --build "$root\build-cmake" } else { & $py -m cmake --build "$root\build-cmake" }
if ($LASTEXITCODE -ne 0) { throw "build failed" }

New-Item -ItemType Directory -Force "$root\build" | Out-Null
Copy-Item "$root\build-cmake\MidnightL.exe" "$root\build\MidnightL.exe" -Force

# ресурсы рядом с exe: папка resources главнее
$res = "$root\build\resources"
New-Item -ItemType Directory -Force $res | Out-Null
foreach ($item in @("fonts", "midnight.ico", "midnight.png")) {
    $src = Join-Path (Split-Path -Parent $root) $item
    if (Test-Path $src) {
        $dst = Join-Path $res $item
        if (Test-Path $dst) { Remove-Item $dst -Recurse -Force }
        Copy-Item $src $dst -Recurse -Force
    }
}

Get-Item "$root\build\MidnightL.exe" | Select-Object Length, LastWriteTime | Format-Table -AutoSize
