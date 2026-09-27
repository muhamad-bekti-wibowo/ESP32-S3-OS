# Compile & jalankan test host fbd_core, TANPA idf.py / ESP-IDF.
# Pakai MSVC cl.exe (esp-clang di PATH proyek ini cross-compile ke
# Xtensa/RISC-V target ESP, tidak bisa jalan sebagai binary native PC).
# Lihat specs/01-fbdvalue-core.md.
$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
Set-Location $root

$vcvars = "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat"
if (-not (Test-Path $vcvars)) {
    throw "vcvars64.bat tidak ditemukan di $vcvars - sesuaikan path Visual Studio kamu."
}

$cmd = "`"$vcvars`" && cl /nologo /std:c11 /Icomponents\fbd_core\include " +
       "test_host\fbd_core_test.c components\fbd_core\fbd_value.c components\fbd_core\fbd_nodes.c " +
       "/Fe:test_host\fbd_core_test.exe /Fo:test_host\ && test_host\fbd_core_test.exe"

cmd /c $cmd
exit $LASTEXITCODE
