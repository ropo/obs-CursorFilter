[CmdletBinding(SupportsShouldProcess = $true)]
param(
    [string]$PluginRoot = (Join-Path $env:ProgramData 'obs-studio\plugins')
)

$ErrorActionPreference = 'Stop'
$name = 'obs-cursor-filter'
$candidates = @(
    (Join-Path $PSScriptRoot 'build\Release\obs-cursor-filter.dll'),
    (Join-Path $PSScriptRoot 'out\obs-cursor-filter\obs-cursor-filter.dll'),
    (Join-Path $PSScriptRoot 'out\obs-plugins\64bit\obs-cursor-filter.dll')
)
$source = $candidates | Where-Object { Test-Path -LiteralPath $_ -PathType Leaf } | Select-Object -First 1
if (-not $source) {
    throw 'DLL が見つかりません。先に Release ビルドを実行してください。'
}

$destination = Join-Path $PluginRoot $name
$target = Join-Path $destination "$name.dll"
if ($PSCmdlet.ShouldProcess($target, "インストール: $source")) {
    try {
        New-Item -ItemType Directory -Force -Path $destination | Out-Null
        Copy-Item -LiteralPath $source -Destination $target -Force
        Write-Host "インストールしました: $target"
        Write-Host 'OBS を再起動してください。実行中は先に終了してからインストールしてください。'
    } catch {
        throw "インストールできませんでした。OBS を終了し、必要に応じて PowerShell を管理者として実行してください。詳細: $_"
    }
}
