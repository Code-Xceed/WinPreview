# ui_probe

DPI-aware harness for WinPreview UI checks. It talks to `user32` directly with
`ctypes`, declares per-monitor-v2 awareness before touching any window, and reads
the app's own `WM_APP` diagnostics — so tests assert geometry and state, not
pixels.

Why not PowerShell: Windows PowerShell 5.1 is DPI-unaware on scaled displays, so
synthetic mouse coordinates are silently scaled (1.25x at 125%) and `Add-Type` /
`PrintWindow` can fail to autoload modules. This driver avoids both problems.

## Driver

```python
import sys
sys.path.insert(0, "tools/ui_probe")
from driver import WinPreview

app = WinPreview("dist/WinPreview/WinPreview.exe", "samples/demo-archive.7z")
app.resize_dip(430, 700)        # sizes in DIPs, converted with the window's DPI
app.wake()                      # move the pointer so an auto-hidden dock returns
app.choice = app.dock_tier()    # 0 compact, 1 standard + overflow, 2 full
app.shot("out.png")             # PrintWindow capture, PNG written without PIL
app.close()
```

Available diagnostics (see `app_window.cpp`):

| Message | Meaning |
|---|---|
| `WM_APP + 237` | document type |
| `WM_APP + 250` | archive state (0 none, 1 listing, 2 password, 3 unreadable) |
| `WM_APP + 251` | archive folder depth |
| `WM_APP + 252` | rows in the current archive folder |
| `WM_APP + 253` | selected archive row |
| `WM_APP + 254` | dock tier (low byte) + pill width in DIPs (high bits) |
| `WM_APP + 255` | painted dock pixels past the pill (0 = clean layout) |

## Dock sweep

```bash
python tools/ui_probe/dock_sweep.py --exe dist/WinPreview/WinPreview.exe \
    file.png source.cpp archive.zip
```

Sweeps the window from 160 to 900 DIPs, resizing and repainting for every file,
and fails if any painted dock button escapes the pill. Exit code `3` means no
window could be created (headless session) and the caller should skip.
