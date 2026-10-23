# One-time: a pristine copy of the game data for rhost — never the user's saves or the
# earlier harness's leftovers (port builds, captures, probe dumps, junk subdirs).
#   C:\ol2-rhost\game\  <- C:\GOG Games\Lords of the Realm II
$ErrorActionPreference = 'Stop'
$src = 'C:\GOG Games\Lords of the Realm II'
$dst = 'C:\ol2-rhost\game'
New-Item -ItemType Directory -Force $dst | Out-Null
$skip = '^(openlords2.*\.exe|unins000\..*|.*\.sav|.*\.json|.*\.jsonl|after_click.*|before_click.*|frame_\d+.*|port_.*|status\.txt|.*\.lnk|goggame-.*|gog\.ico)$'
$n = 0
Get-ChildItem $src -File | Where-Object { $_.Name -notmatch $skip } | ForEach-Object {
    $d = Join-Path $dst $_.Name
    if (!(Test-Path $d) -or (Get-Item $d).Length -ne $_.Length) { Copy-Item $_.FullName $d; $n++ }
}
"staged $n file(s); $((Get-ChildItem $dst -File).Count) in $dst"
(Get-FileHash "$dst\Lords2.exe").Hash
