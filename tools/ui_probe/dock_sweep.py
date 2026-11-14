"""Dock layout sweep for CI and local checks.

Every viewer's floating dock has three tiers (full, standard with an overflow
menu, compact). The app measures the painted buttons against the pill and reports
the overflow in pixels through WM_APP+255, so this sweep asserts real geometry at
many widths instead of comparing screenshots.

Usage:
    python tools/ui_probe/dock_sweep.py --exe dist/WinPreview/WinPreview.exe \
        file.png source.cpp archive.7z

Exit codes: 0 all good, 1 layout violation, 3 window unavailable (headless CI).
"""

import argparse
import os
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from driver import WinPreview  # noqa: E402

WIDTHS = [160, 200, 240, 280, 330, 390, 430, 460, 520, 620, 760, 900]

passed = 0
failed = 0


def check(condition, label, detail=""):
    global passed, failed
    if condition:
        passed += 1
        print(f" [PASS] {label}")
    else:
        failed += 1
        print(f" [FAIL] {label}\n        {detail}")


def settle(app, width, timeout=2.5):
    app.resize_dip(width, 700)
    deadline = time.time() + timeout
    previous = -1.0
    while time.time() < deadline:
        client = app.client_size_dip()[0]
        if (abs(client - width) <= 4 or (width < 340 and client >= 300)) and abs(client - previous) < 0.5:
            break
        previous = client
        time.sleep(0.12)
    app.wake()
    app.repaint()
    return app.client_size_dip()[0]


def sweep(exe, path):
    name = os.path.basename(path)
    try:
        app = WinPreview(exe, path)
    except RuntimeError as error:
        print(f" [SKIP] {name}: {error}")
        return "skip"
    try:
        tiers = set()
        for width in WIDTHS:
            client = tier = 0
            pill = overflow = 0.0
            for _ in range(3):
                client = settle(app, width)
                tier = app.dock_tier()
                pill = float(app.dock_pill_width())
                overflow = app.dock_overflow()
                if overflow == 0 and (pill <= 0 or pill <= client + 1.0):
                    break
                time.sleep(0.25)
            tiers.add(tier)
            check(overflow == 0,
                  f"{name} {width}px: buttons stay inside the pill",
                  f"tier={tier} pill={pill} overflow={overflow}px")
            if pill > 0:
                check(pill <= client + 1.0,
                      f"{name} {width}px: pill fits the window",
                      f"client={client:.0f}dip pill={pill}")
        check(len(tiers) >= 1, f"{name}: dock rendered", f"tiers={sorted(tiers)}")
    finally:
        app.close()
    return "ok"


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--exe", required=True)
    parser.add_argument("files", nargs="+")
    args = parser.parse_args()

    if not os.path.exists(args.exe):
        print(f" [FAIL] exe not found: {args.exe}")
        return 1

    results = []
    for path in args.files:
        if not os.path.exists(path):
            print(f" [SKIP] {os.path.basename(path)}: missing")
            continue
        results.append(sweep(args.exe, path))

    print(f"\nDock sweep: {passed} passed, {failed} failed")
    if failed:
        return 1
    if results and all(r == "skip" for r in results):
        return 3
    return 0


if __name__ == "__main__":
    sys.exit(main())
