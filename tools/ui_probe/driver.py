"""DPI-aware driver for WinPreview UI tests.

Why this exists: PowerShell 5.1 is DPI-unaware on scaled displays, so synthetic
mouse coordinates get virtualised by 1.25x (or worse) before the app sees them,
and Add-Type/PrintWindow intermittently fails to autoload modules. This module
talks to user32 directly with ctypes, declares per-monitor-v2 awareness up front,
and returns the app's own WM_APP diagnostics so assertions never depend on OCR or
pixel reading.

Usage:
    from ui_driver import WinPreview
    app = WinPreview(exe, file)
    app.resize_dip(430, 700)
    assert app.dock_overflow() == 0
    app.shot("out.bmp")
    app.close()
"""

import ctypes
import ctypes.wintypes as wt
import os
import struct
import subprocess
import time

user32 = ctypes.WinDLL("user32", use_last_error=True)
gdi32 = ctypes.WinDLL("gdi32", use_last_error=True)

# LRESULT is 64-bit on x64; ctypes defaults to c_int, which silently truncates
# packed diagnostics such as the 4x16-bit button rects.
user32.SendMessageW.restype = ctypes.c_ssize_t
user32.SendMessageW.argtypes = [wt.HWND, wt.UINT, wt.WPARAM, wt.LPARAM]

WM_LBUTTONDOWN = 0x0201
WM_LBUTTONUP = 0x0202
WM_LBUTTONDBLCLK = 0x0203
WM_KEYDOWN = 0x0100
WM_KEYUP = 0x0101
WM_APP = 0x8000

# Diagnostics implemented by the app (see app_window.cpp).
DIAG_DOCTYPE = WM_APP + 237
DIAG_ARCHIVE_STATE = WM_APP + 250
DIAG_ARCHIVE_DEPTH = WM_APP + 251
DIAG_ARCHIVE_ROWS = WM_APP + 252
DIAG_ARCHIVE_SEL = WM_APP + 253
DIAG_DOCK = WM_APP + 254
DIAG_DOCK_OVERFLOW = WM_APP + 255


def _enable_dpi_awareness():
    try:
        # DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2
        user32.SetProcessDpiAwarenessContext(ctypes.c_void_p(-4))
        return "per-monitor-v2"
    except Exception:
        pass
    try:
        ctypes.WinDLL("shcore").SetProcessDpiAwareness(2)
        return "per-monitor"
    except Exception:
        try:
            user32.SetProcessDPIAware()
            return "system"
        except Exception:
            return "unaware"


DPI_MODE = _enable_dpi_awareness()


class RECT(ctypes.Structure):
    _fields_ = [("left", ctypes.c_long), ("top", ctypes.c_long),
                ("right", ctypes.c_long), ("bottom", ctypes.c_long)]


class BITMAPINFOHEADER(ctypes.Structure):
    _fields_ = [("biSize", wt.DWORD), ("biWidth", ctypes.c_long), ("biHeight", ctypes.c_long),
                ("biPlanes", wt.WORD), ("biBitCount", wt.WORD), ("biCompression", wt.DWORD),
                ("biSizeImage", wt.DWORD), ("biXPelsPerMeter", ctypes.c_long),
                ("biYPelsPerMeter", ctypes.c_long), ("biClrUsed", wt.DWORD),
                ("biClrImportant", wt.DWORD)]


class BITMAPINFO(ctypes.Structure):
    _fields_ = [("bmiHeader", BITMAPINFOHEADER), ("bmiColors", wt.DWORD * 3)]


def _enum_windows_for(pid):
    found = []

    @ctypes.WINFUNCTYPE(wt.BOOL, wt.HWND, wt.LPARAM)
    def cb(hwnd, _):
        owner = wt.DWORD()
        user32.GetWindowThreadProcessId(hwnd, ctypes.byref(owner))
        if owner.value == pid and user32.IsWindowVisible(hwnd):
            rc = RECT()
            user32.GetWindowRect(hwnd, ctypes.byref(rc))
            if rc.right - rc.left > 120 and rc.bottom - rc.top > 120:
                found.append(hwnd)
        return True

    user32.EnumWindows(cb, 0)
    return found[0] if found else 0


class WinPreview:
    def __init__(self, exe, document=None, args=(), timeout=15.0):
        self.exe = exe
        self.document = document
        self.proc = None
        self.hwnd = 0
        self.pid = 0
        self._start(args, timeout)

    # -- lifecycle ---------------------------------------------------------
    def _start(self, args, timeout):
        argv = [self.exe]
        if self.document:
            argv.append(self.document)
        argv.extend(args)
        self.proc = subprocess.Popen(argv)
        self.pid = self.proc.pid
        deadline = time.time() + timeout
        while time.time() < deadline:
            self.hwnd = _enum_windows_for(self.pid)
            if self.hwnd:
                time.sleep(0.6)  # let the first frame paint
                return
            if self.proc.poll() is not None:
                raise RuntimeError(f"WinPreview exited during start (code {self.proc.returncode})")
            time.sleep(0.15)
        raise RuntimeError("WinPreview window did not appear")

    def close(self):
        if self.proc and self.proc.poll() is None:
            self.proc.terminate()
            try:
                self.proc.wait(3)
            except subprocess.TimeoutExpired:
                self.proc.kill()

    def __enter__(self):
        return self

    def __exit__(self, *exc):
        self.close()

    # -- geometry ----------------------------------------------------------
    def dpi_scale(self):
        try:
            dpi = user32.GetDpiForWindow(self.hwnd)
        except Exception:
            dpi = 96
        return dpi / 96.0

    def client_size_dip(self):
        rc = RECT()
        user32.GetClientRect(self.hwnd, ctypes.byref(rc))
        scale = self.dpi_scale()
        return ((rc.right - rc.left) / scale, (rc.bottom - rc.top) / scale)

    def resize_dip(self, width, height):
        scale = self.dpi_scale()
        user32.SetWindowPos(self.hwnd, 0, 40, 40, int(width * scale), int(height * scale), 0x0014)
        self.repaint()

    def wake(self):
        """Move the pointer inside the window so an auto-hidden dock comes back."""
        scale = self.dpi_scale()
        width, height = self.client_size_dip()
        x, y = int(width * scale / 2), int(height * scale / 2)
        lp = (y << 16) | (x & 0xFFFF)
        self._send(0x0200, 0, lp)  # WM_MOUSEMOVE
        time.sleep(0.3)

    def repaint(self):
        """Force WM_PAINT now; the app parks its animation timer when idle, so a
        resize alone does not guarantee the dock has re-laid-out when we query."""
        RDW_INVALIDATE, RDW_ERASE, RDW_UPDATENOW = 0x0001, 0x0004, 0x0100
        user32.RedrawWindow(self.hwnd, None, None, RDW_INVALIDATE | RDW_ERASE | RDW_UPDATENOW)
        user32.UpdateWindow(self.hwnd)
        time.sleep(0.12)

    # -- input -------------------------------------------------------------
    def _send(self, msg, wparam, lparam):
        return user32.SendMessageW(self.hwnd, msg, wt.WPARAM(wparam), wt.LPARAM(lparam))

    def click(self, x_dip, y_dip):
        scale = self.dpi_scale()
        x, y = int(x_dip * scale), int(y_dip * scale)
        lp = (y << 16) | (x & 0xFFFF)
        self._send(WM_LBUTTONDOWN, 1, lp)
        self._send(WM_LBUTTONUP, 0, lp)
        time.sleep(0.15)

    def double_click(self, x_dip, y_dip):
        scale = self.dpi_scale()
        x, y = int(x_dip * scale), int(y_dip * scale)
        lp = (y << 16) | (x & 0xFFFF)
        self._send(WM_LBUTTONDOWN, 1, lp)
        self._send(WM_LBUTTONUP, 0, lp)
        self._send(WM_LBUTTONDBLCLK, 1, lp)
        self._send(WM_LBUTTONUP, 0, lp)
        time.sleep(0.2)

    def key(self, vk):
        self._send(WM_KEYDOWN, vk, 0)
        self._send(WM_KEYUP, vk, 0)
        time.sleep(0.15)

    # -- diagnostics -------------------------------------------------------
    def query(self, message):
        return self._send(message, 0, 0)

    def dock_tier(self):
        return self.query(DIAG_DOCK) & 0xFF

    def dock_pill_width(self):
        return (self.query(DIAG_DOCK) >> 8) & 0xFFFF

    def dock_overflow(self):
        return self.query(DIAG_DOCK_OVERFLOW)

    def archive_state(self):
        return self.query(DIAG_ARCHIVE_STATE)

    def archive_depth(self):
        return self.query(DIAG_ARCHIVE_DEPTH)

    def archive_rows(self):
        return self.query(DIAG_ARCHIVE_ROWS)

    def archive_selection(self):
        return self.query(DIAG_ARCHIVE_SEL)

    # -- capture -----------------------------------------------------------
    def shot(self, path):
        """PrintWindow capture saved as PNG (no external imaging libraries)."""
        import zlib

        rc = RECT()
        user32.GetWindowRect(self.hwnd, ctypes.byref(rc))
        w, h = rc.right - rc.left, rc.bottom - rc.top
        if w <= 0 or h <= 0:
            raise RuntimeError("window has no size")

        hdc = user32.GetWindowDC(self.hwnd)
        mem = gdi32.CreateCompatibleDC(hdc)
        bmp = gdi32.CreateCompatibleBitmap(hdc, w, h)
        gdi32.SelectObject(mem, bmp)
        user32.PrintWindow(self.hwnd, mem, 2)

        bmi = BITMAPINFO()
        bmi.bmiHeader.biSize = ctypes.sizeof(BITMAPINFOHEADER)
        bmi.bmiHeader.biWidth = w
        bmi.bmiHeader.biHeight = -h  # top-down rows
        bmi.bmiHeader.biPlanes = 1
        bmi.bmiHeader.biBitCount = 32
        bmi.bmiHeader.biCompression = 0
        stride = w * 4
        buf = ctypes.create_string_buffer(stride * h)
        gdi32.GetDIBits(mem, bmp, 0, h, buf, ctypes.byref(bmi), 0)

        raw = buf.raw
        rows = bytearray()
        for y in range(h):
            row = bytearray(raw[y * stride:(y + 1) * stride])
            # BGRA -> RGBA
            row[0::4], row[2::4] = row[2::4], row[0::4]
            rows.append(0)  # filter type: none
            rows.extend(row)

        def chunk(tag, data):
            return (struct.pack(">I", len(data)) + tag + data +
                    struct.pack(">I", zlib.crc32(tag + data) & 0xFFFFFFFF))

        png = b"\x89PNG\r\n\x1a\n"
        png += chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 6, 0, 0, 0))
        png += chunk(b"IDAT", zlib.compress(bytes(rows), 6))
        png += chunk(b"IEND", b"")
        with open(path, "wb") as fh:
            fh.write(png)

        gdi32.DeleteObject(bmp)
        gdi32.DeleteDC(mem)
        user32.ReleaseDC(self.hwnd, hdc)
        return path
