# Compile & jalankan semua test host fbd_core / fbd_graph, TANPA idf.py /
# ESP-IDF. Pakai MSVC cl.exe (esp-clang di PATH proyek ini cross-compile
# ke Xtensa/RISC-V target ESP, tidak bisa jalan sebagai binary native PC).
# Lihat specs/01-fbdvalue-core.md dan specs/02-topo-sort-runtime.md.
$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
Set-Location $root

$vcvars = "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat"
if (-not (Test-Path $vcvars)) {
    throw "vcvars64.bat tidak ditemukan di $vcvars - sesuaikan path Visual Studio kamu."
}

function Run-HostTest($name, $sources) {
    Write-Host "`n=== $name ===" -ForegroundColor Cyan
    $srcList = $sources -join " "
    $cmd = "`"$vcvars`" && cl /nologo /std:c11 /Icomponents\fbd_core\include " +
           "$srcList /Fe:test_host\$name.exe /Fo:test_host\ && test_host\$name.exe"
    cmd /c $cmd
    if ($LASTEXITCODE -ne 0) {
        throw "$name gagal (exit code $LASTEXITCODE)"
    }
}

Run-HostTest "fbd_core_test" @(
    "test_host\fbd_core_test.c"
    "components\fbd_core\fbd_value.c"
    "components\fbd_core\fbd_nodes.c"
)

Run-HostTest "fbd_graph_test" @(
    "test_host\fbd_graph_test.c"
    "components\fbd_core\fbd_value.c"
    "components\fbd_core\fbd_nodes.c"
    "components\fbd_core\fbd_graph.c"
)
