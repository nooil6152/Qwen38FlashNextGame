param([string]$Exe = "bin\Town3D.exe", [int]$Seconds = 7)
$ErrorActionPreference = "Stop"
$p = Start-Process -FilePath $Exe -ArgumentList @("--showlog","--trace") -WorkingDirectory (Get-Location) -PassThru
Start-Sleep -Seconds $Seconds
if (-not $p.HasExited) {
    $p.Refresh()
    "ALIVE after ${Seconds}s  CPU=$([math]::Round($p.CPU,2))s  WS=$([math]::Round($p.WorkingSet64/1MB,1))MB"
    Stop-Process -Id $p.Id -Force
    "killed cleanly"
} else {
    "EXITED early, code=$($p.ExitCode)"
}
