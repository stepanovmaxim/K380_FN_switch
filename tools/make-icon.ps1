# Rebuilds icon.ico from the high-resolution master as a multi-resolution icon,
# so the shell picks the exact frame it needs instead of down-scaling a single
# 128x128 image (which is what the notification area used to do).
#
#   powershell -ExecutionPolicy Bypass -File tools\make-icon.ps1
param(
    [string]$Source = "$PSScriptRoot\icon-master.ico",
    [string]$Output = "$PSScriptRoot\..\icon.ico"
)

Add-Type -AssemblyName System.Drawing

$sizes = 16, 20, 24, 32, 40, 48, 64, 96, 128, 256

$src = [System.Drawing.Image]::FromFile((Resolve-Path $Source))
try {
    $frames = New-Object System.Collections.Generic.List[byte[]]
    $dims = New-Object System.Collections.Generic.List[int]

    foreach ($s in $sizes) {
        if ($s -gt [Math]::Max($src.Width, $src.Height)) { continue }

        $bmp = New-Object System.Drawing.Bitmap($s, $s, [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
        $g = [System.Drawing.Graphics]::FromImage($bmp)
        $g.InterpolationMode = [System.Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
        $g.PixelOffsetMode  = [System.Drawing.Drawing2D.PixelOffsetMode]::HighQuality
        $g.SmoothingMode    = [System.Drawing.Drawing2D.SmoothingMode]::HighQuality
        $g.CompositingQuality = [System.Drawing.Drawing2D.CompositingQuality]::HighQuality
        $g.Clear([System.Drawing.Color]::Transparent)
        $g.DrawImage($src, (New-Object System.Drawing.Rectangle(0, 0, $s, $s)))
        $g.Dispose()

        $ms = New-Object System.IO.MemoryStream
        $bmp.Save($ms, [System.Drawing.Imaging.ImageFormat]::Png)
        $frames.Add($ms.ToArray())
        $dims.Add($s)
        $ms.Dispose(); $bmp.Dispose()
    }
} finally { $src.Dispose() }

# ICONDIR + ICONDIRENTRY[n] + PNG payloads (PNG-in-ICO, supported since Vista)
$out = New-Object System.IO.MemoryStream
$w = New-Object System.IO.BinaryWriter($out)
$w.Write([uint16]0); $w.Write([uint16]1); $w.Write([uint16]$frames.Count)

$offset = 6 + 16 * $frames.Count
for ($i = 0; $i -lt $frames.Count; $i++) {
    $d = $dims[$i]
    $w.Write([byte]($(if ($d -ge 256) { 0 } else { $d })))   # width  (0 == 256)
    $w.Write([byte]($(if ($d -ge 256) { 0 } else { $d })))   # height
    $w.Write([byte]0)                                        # palette
    $w.Write([byte]0)                                        # reserved
    $w.Write([uint16]1)                                      # planes
    $w.Write([uint16]32)                                     # bpp
    $w.Write([uint32]$frames[$i].Length)
    $w.Write([uint32]$offset)
    $offset += $frames[$i].Length
}
foreach ($f in $frames) { $w.Write($f) }
$w.Flush()

[System.IO.File]::WriteAllBytes((New-Item -ItemType File -Path $Output -Force).FullName, $out.ToArray())
$w.Dispose(); $out.Dispose()

"Wrote $Output : $($frames.Count) frames ($($dims -join ', ')) - $((Get-Item $Output).Length) bytes"
