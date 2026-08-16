# Copyright (c) 2026 DSH ESP32 dial project.
# SPDX-License-Identifier: MIT
#
# Generate the dial's Chinese font.
#
# WHY THIS EXISTS: LVGL's built-in lv_font_simsun_16_cjk covers only 1166 CJK
# codepoints, and a quarter of the characters this firmware actually displays are
# not among them — 允 拒 绝 许 轮 调 网 设 identify as missing, so the dial drew
# boxes. Rather than rewrite every label into the subset (which would constrain
# wording forever and break the moment a new string appears), this script builds
# a font containing exactly the glyphs the project uses.
#
# The glyph list is derived from the source, not maintained by hand: run this
# after adding Chinese text and the new characters are picked up automatically.

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$conv = Join-Path $PSScriptRoot 'node_modules\.bin\lv_font_conv.cmd'

if (-not (Test-Path $conv)) {
    Write-Error "lv_font_conv not installed. Run: npm install lv_font_conv --no-save --prefix tools"
}

# ── 1. Collect every CJK character the firmware or bridge can put on screen ──
# Console.cpp is excluded on purpose: its Chinese goes to the serial terminal,
# which uses the host's fonts, not LVGL's.
$sources = @(
    (Join-Path $root 'firmware\src\DialUi.cpp'),
    (Join-Path $root 'firmware\src\main.cpp'),
    (Join-Path $root 'firmware\src\Provision.cpp'),
    (Join-Path $root 'firmware\include\Provision.h'),
    (Join-Path $root 'bridge\bridge.js')
)

$text = ''
foreach ($src in $sources) {
    if (Test-Path $src) { $text += Get-Content $src -Raw -Encoding UTF8 }
}

$cjk = [regex]::Matches($text, '[\u4e00-\u9fff]') |
    ForEach-Object { $_.Value } |
    Sort-Object -Unique

Write-Host "Collected $($cjk.Count) distinct CJK characters from source"
Write-Host ($cjk -join '')

# Characters the bridge may send that no source file literal contains yet, plus
# the punctuation the status line needs. Kept explicit so a formatting change on
# the bridge side cannot silently reintroduce boxes.
#
# Full-width punctuation beyond the basics: the approval title ends with a
# full-width question mark ("允许 read 运行？") and the fallback question
# buttons are 是/否 — none of those appear as literals in the source, so they
# have to be listed here.
$extra = '·、。：（）％？！；，是否文件'
$symbols = ($extra.ToCharArray() | Sort-Object -Unique) -join ''

# ── 2. Build the codepoint range argument ─────────────────────────────────
# ASCII 0x20-0x7E covers digits, latin, and the separators the ticker uses.
$cjkList = ($cjk -join '') + $symbols

$fontFile = 'C:\Windows\Fonts\simhei.ttf'   # SimHei: clean at 16px, unlike SimSun
if (-not (Test-Path $fontFile)) { Write-Error "Font not found: $fontFile" }

$outDir = Join-Path $root 'firmware\src\fonts'
New-Item -ItemType Directory -Force -Path $outDir | Out-Null
$outFile = Join-Path $outDir 'dsh_font_cjk_16.c'

Write-Host ''
Write-Host "Generating $outFile"
Write-Host "  source : $fontFile"
Write-Host "  glyphs : ASCII 0x20-0x7E + $($cjkList.Length) CJK/punctuation"

# --no-compress keeps the decoder simple and the flash cost is acceptable for
# this glyph count; bpp 4 matches the antialiasing of the built-in fonts.
& $conv --font $fontFile --size 16 --bpp 4 --format lvgl --no-compress --lv-include lvgl.h -o $outFile --range 0x20-0x7E --symbols $cjkList

if ($LASTEXITCODE -ne 0) { Write-Error "lv_font_conv failed with exit code $LASTEXITCODE" }

$size = (Get-Item $outFile).Length
Write-Host ''
Write-Host "Done: $outFile ($([math]::Round($size/1KB)) KB)"
Write-Host "Declare it with:  LV_FONT_DECLARE(dsh_font_cjk_16);"
