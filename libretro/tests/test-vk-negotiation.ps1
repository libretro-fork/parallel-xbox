# Native Windows entry-point regression: requires the built harness and ROM system dir.
param(
    [Parameter(Mandatory=$true)][string]$Core,
    [Parameter(Mandatory=$true)][string]$Harness,
    [Parameter(Mandatory=$true)][string]$SystemDir
)
$ErrorActionPreference = 'Stop'
$base = @($Core, $SystemDir, '--opt', 'xemu_renderer=vulkan',
    '--opt', 'xemu_deterministic=enabled', '--opt', 'xemu_use_dsp=disabled',
    '--frames', '10', '--strict')
$refused = (& $Harness @base --vulkan-refuse-negotiation 2>&1 | Out-String)
if ($LASTEXITCODE -ne 1 -or $refused -notmatch 'device negotiation was refused') {
    throw "Refused negotiation did not fail loading explicitly: $refused"
}
$mismatch = (& $Harness @base --vulkan-mismatch-device 2>&1 | Out-String)
if ($LASTEXITCODE -ne 0 -or $mismatch -notmatch 'requires the negotiated frontend device' -or
    $mismatch -notmatch 'video_new=0 video_dupe=10 runs_without_video=0' -or
    $mismatch -notmatch 'audio_total=8008 expected=8008' -or
    $mismatch -match 'The machine is running') {
    throw "Mismatched context did not stay idle and shut down safely: $mismatch"
}
foreach ($mode in @('--vulkan', '--vulkan-v1')) {
    $normal = (& $Harness @base $mode 2>&1 | Out-String)
    if ($LASTEXITCODE -ne 0 -or $normal -notmatch 'audio_total=8008 expected=8008' -or
        $normal -notmatch 'runs_without_video=0') {
        throw "Negotiation $mode failed: $normal"
    }
}
'PASS: negotiation v1/v2, explicit refusal, mismatched-device idle cadence and teardown'
