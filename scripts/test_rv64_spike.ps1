param(
    [ValidateSet("debug", "release")]
    [string]$Preset = "debug",
    [string]$Distribution = "Ubuntu-24.04",
    [string]$Spike = "/home/yzl/riscv/spike/bin/spike"
)

$ErrorActionPreference = "Stop"

$projectRoot = Split-Path -Parent $PSScriptRoot
$drive = [System.IO.Path]::GetPathRoot($projectRoot).Substring(0, 1).ToLower()
$relativeRoot = $projectRoot.Substring(3).Replace("\", "/")
$linuxRoot = "/mnt/$drive/$relativeRoot"
$windowsRoot = $projectRoot.Replace("\", "/")

$testCases = @(
    "rv64uf-fadd",
    "rv64uf-fcmp",
    "rv64uf-fcvt",
    "rv64uf-fmadd",
    "rv64uf-fmin",
    "rv64uf-recoding",
    "rv64ud-fadd",
    "rv64ud-fcmp",
    "rv64ud-fcvt",
    "rv64ud-fmadd",
    "rv64ud-fmin",
    "rv64ud-structural"
)

foreach ($testCase in $testCases) {
    Write-Host "==== $testCase ===="
    $linuxBinary =
        "$linuxRoot/build/$Preset/architecture64/$testCase.bin"
    $windowsBinary =
        "$windowsRoot/build/$Preset/architecture64/$testCase.bin"
    $elf = "$linuxRoot/build/$Preset/architecture64/$testCase.elf"

    & wsl.exe -d $Distribution -- python3 `
        "$linuxRoot/tests/differential/compare_spike.py" `
        --dut "$linuxRoot/build/$Preset/rv64_architecture_runner.exe" `
        --binary $linuxBinary `
        --dut-binary $windowsBinary `
        --elf $elf `
        --spike $Spike `
        --xlen 64 `
        --isa rv64imafdc_zicntr_zicsr_zifencei `
        --allow-spike-tail

    if ($LASTEXITCODE -ne 0) {
        throw "Spike differential test failed: $testCase"
    }
}
