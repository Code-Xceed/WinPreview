"""Generated-fixture format matrix for CI.

The full sample corpus is deliberately local (it includes third-party files), so
CI builds its own fixtures here and opens each one in the shipped executable:
does the process survive, does the document type match the extension family,
does the window actually draw, and do paging keys leave it intact.

Run: python tools/ci_probe.py [--exe path\\to\\WinPreview.exe]
Exit code 0 = every case passed.
"""

import base64
import os
import shutil
import struct
import subprocess
import sys
import tempfile
import time
import zipfile
import zlib

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
import json

EXE = os.path.join(ROOT, "dist", "WinPreview", "WinPreview.exe")
if not os.path.exists(EXE):
    EXE = os.path.join(ROOT, "src", "WinPreview.exe")

BG = (13, 15, 18)
IMAGE, TEXT, SHEET, OFFICE, SVG = 1, 4, 5, 6, 7
FONT, FOLDER, MARKDOWN, BINARY, UNSUPPORTED = 8, 9, 10, 12, 15
OFFICE_DOC, COMIC, RTF = 16, 17, 18

passed = 0
failed = 0
failures = []
work_dir = ""


def check(condition, label, detail=""):
    global passed, failed
    if condition:
        passed += 1
    else:
        failed += 1
        failures.append((label, detail))
        # GitHub shows these as check annotations, so a red matrix names the
        # exact fixture and reason even without authenticated log access.
        if os.environ.get("GITHUB_ACTIONS") == "true":
            cleaned = str(detail).replace("\n", " ")[:400]
            print(f"::error title=Format matrix::{label} -- {cleaned}", flush=True)
    return condition


# --------------------------------------------------------------------------
# Fixture generation
# --------------------------------------------------------------------------

def png_bytes(width=96, height=64, rgb=(40, 120, 200)):
    raw = b"".join(b"\x00" + bytes(rgb) * width for _ in range(height))

    def chunk(tag, data):
        return (struct.pack(">I", len(data)) + tag + data +
                struct.pack(">I", zlib.crc32(tag + data) & 0xFFFFFFFF))

    header = struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0)
    return (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", header) +
            chunk(b"IDAT", zlib.compress(raw, 9)) + chunk(b"IEND", b""))


def bmp_bytes(width=4, height=4, rgb=(200, 90, 40)):
    row = bytes((rgb[2], rgb[1], rgb[0])) * width
    pixel_data = b"".join(row for _ in range(height))
    header = struct.pack("<2sIHHI", b"BM", 14 + 40 + len(pixel_data), 0, 0, 14 + 40)
    info = struct.pack("<IiiHHIIiiII", 40, width, height, 1, 24, 0, len(pixel_data),
                       2835, 2835, 0, 0)
    return header + info + pixel_data


GIF_ONE_PIXEL = base64.b64decode("R0lGODlhAQABAIAAAAAAAP///yH5BAEAAAAALAAAAAABAAEAAAIBRAA7")

# 64x48 lossless WebP, generated once from a gradient. Decoded by the vendored
# libwebp, so this also verifies the build has no dependency on a Windows codec.
# 64x48 JPEG XL, encoded once with the official cjxl 0.13.0. Decoded by the
# vendored libjxl runtime.
JXL_FIXTURE = base64.b64decode(
    "/wrLBgAUCAMAFALbUOqSRJFxbJJO4DBJQbED8tNUAHZ0dAwGGRmDBWc+p92+GaQ/xMeqTsGSlv8F"
    "44zTz9MjSFruyu/BNq4KdmlGcWDEajleYLTbNu3O4+qeARW8wya1AICTXqBo0f9LEwEA9sxSDcB7"
    "cjJJkAihLGcGQGnNzAAoMzMAMDMzAGZmBsDMzAwA")
JXL_BROKEN = (b"\x00\x00\x00\x0cJXL \x0d\x0a\x87\x0a" + bytes(64))

WEBP_FIXTURE = base64.b64decode(
    "UklGRpQAAABXRUJQVlA4IIgAAADwBACdASpAADAAPm02l0kkIqIhIqgAgA2JYwC7NFrCmArJXZE/"
    "7WuX+H//8eP//DAA/u84J7XzP/wQW2G5k9//gMG/BqkNmgS1fS77VwWFrHrKX1WuAq97OCjmc"
    "k0/ONjLdMEyW2KguzG28qTnS81G1+tMgENxFSrLsQ19zLZxFON4znVJ/gAA")



def write(path, data):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    mode = "wb" if isinstance(data, (bytes, bytearray)) else "w"
    with open(path, mode, **({} if mode == "wb" else {"encoding": "utf-8", "newline": "\n"})) as fh:
        fh.write(data)


def build_office_packages(path):
    content_types = (
        '<?xml version="1.0" encoding="UTF-8" standalone="yes"?>'
        '<Types xmlns="http://schemas.openxmlformats.org/package/2006/content-types">'
        '<Default Extension="rels" ContentType="application/vnd.openxmlformats-package.relationships+xml"/>'
        '<Default Extension="xml" ContentType="application/xml"/>'
        '{overrides}</Types>'
    )
    rels = (
        '<?xml version="1.0" encoding="UTF-8" standalone="yes"?>'
        '<Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships">'
        '<Relationship Id="rId1" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/officeDocument" Target="{target}"/>'
        '</Relationships>'
    )

    # DOCX
    docx = os.path.join(path, "generated.docx")
    with zipfile.ZipFile(docx, "w", zipfile.ZIP_DEFLATED) as z:
        z.writestr("[Content_Types].xml", content_types.format(overrides=(
            '<Override PartName="/word/document.xml" ContentType="application/vnd.openxmlformats-officedocument.wordprocessingml.document.main+xml"/>')))
        z.writestr("_rels/.rels", rels.format(target="word/document.xml"))
        z.writestr("word/document.xml",
                   '<?xml version="1.0" encoding="UTF-8" standalone="yes"?>'
                   '<w:document xmlns:w="http://schemas.openxmlformats.org/wordprocessingml/2006/main">'
                   '<w:body><w:p><w:r><w:t>Generated Word document</w:t></w:r></w:p>'
                   '<w:p><w:r><w:t>CI opens this fixture to exercise the Office text view.</w:t></w:r></w:p>'
                   '</w:body></w:document>')

    # XLSX
    xlsx = os.path.join(path, "generated.xlsx")
    with zipfile.ZipFile(xlsx, "w", zipfile.ZIP_DEFLATED) as z:
        z.writestr("[Content_Types].xml", content_types.format(overrides=(
            '<Override PartName="/xl/workbook.xml" ContentType="application/vnd.openxmlformats-officedocument.spreadsheetml.sheet.main+xml"/>'
            '<Override PartName="/xl/worksheets/sheet1.xml" ContentType="application/vnd.openxmlformats-officedocument.spreadsheetml.worksheet+xml"/>')))
        z.writestr("_rels/.rels", rels.format(target="xl/workbook.xml"))
        z.writestr("xl/workbook.xml",
                   '<?xml version="1.0" encoding="UTF-8" standalone="yes"?>'
                   '<workbook xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main" '
                   'xmlns:r="http://schemas.openxmlformats.org/officeDocument/2006/relationships">'
                   '<sheets><sheet name="Generated" sheetId="1" r:id="rId1"/></sheets></workbook>')
        z.writestr("xl/_rels/workbook.xml.rels",
                   '<?xml version="1.0" encoding="UTF-8" standalone="yes"?>'
                   '<Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships">'
                   '<Relationship Id="rId1" '
                   'Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/worksheet" '
                   'Target="worksheets/sheet1.xml"/></Relationships>')
        z.writestr("xl/worksheets/sheet1.xml",
                   '<?xml version="1.0" encoding="UTF-8" standalone="yes"?>'
                   '<worksheet xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main">'
                   '<sheetData>'
                   '<row r="1"><c r="A1" t="inlineStr"><is><t>Item</t></is></c>'
                   '<c r="B1" t="inlineStr"><is><t>Count</t></is></c></row>'
                   '<row r="2"><c r="A2" t="inlineStr"><is><t>Rope</t></is></c><c r="B2"><v>3</v></c></row>'
                   '</sheetData></worksheet>')

    # PPTX
    pptx = os.path.join(path, "generated.pptx")
    with zipfile.ZipFile(pptx, "w", zipfile.ZIP_DEFLATED) as z:
        z.writestr("[Content_Types].xml", content_types.format(overrides=(
            '<Override PartName="/ppt/presentation.xml" ContentType="application/vnd.openxmlformats-officedocument.presentationml.presentation.main+xml"/>'
            '<Override PartName="/ppt/slides/slide1.xml" ContentType="application/vnd.openxmlformats-officedocument.presentationml.slide+xml"/>')))
        z.writestr("_rels/.rels", rels.format(target="ppt/presentation.xml"))
        z.writestr("ppt/presentation.xml",
                   '<?xml version="1.0" encoding="UTF-8" standalone="yes"?>'
                   '<p:presentation xmlns:a="http://schemas.openxmlformats.org/drawingml/2006/main" '
                   'xmlns:p="http://schemas.openxmlformats.org/presentationml/2006/main" '
                   'xmlns:r="http://schemas.openxmlformats.org/officeDocument/2006/relationships">'
                   '<p:sldIdLst><p:sldId id="255" r:id="rId1"/></p:sldIdLst></p:presentation>')
        z.writestr("ppt/_rels/presentation.xml.rels",
                   '<?xml version="1.0" encoding="UTF-8" standalone="yes"?>'
                   '<Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships">'
                   '<Relationship Id="rId1" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/slide" '
                   'Target="slides/slide1.xml"/></Relationships>')
        z.writestr("ppt/slides/slide1.xml",
                   '<?xml version="1.0" encoding="UTF-8" standalone="yes"?>'
                   '<p:sld xmlns:a="http://schemas.openxmlformats.org/drawingml/2006/main" '
                   'xmlns:p="http://schemas.openxmlformats.org/presentationml/2006/main">'
                   '<p:cSld><p:spTree><p:sp><p:nvSpPr><p:cNvPr id="1" name="s"/><p:cNvSpPr/>'
                   '<p:nvPr><p:ph type="title"/></p:nvPr></p:nvSpPr><p:txBody><a:bodyPr/>'
                   '<a:p><a:r><a:t>Generated slide</a:t></a:r></a:p></p:txBody></p:sp>'
                   '</p:spTree></p:cSld></p:sld>')

    # EPUB
    epub = os.path.join(path, "generated.epub")
    with zipfile.ZipFile(epub, "w") as z:
        z.writestr("mimetype", "application/epub+zip", compress_type=zipfile.ZIP_STORED)
        z.writestr("META-INF/container.xml",
                   '<?xml version="1.0" encoding="UTF-8"?>'
                   '<container version="1.0" xmlns="urn:oasis:names:tc:opendocument:xmlns:container">'
                   '<rootfiles><rootfile full-path="OEBPS/content.opf" '
                   'media-type="application/oebps-package+xml"/></rootfiles></container>')
        z.writestr("OEBPS/content.opf",
                   '<?xml version="1.0" encoding="UTF-8"?>'
                   '<package xmlns="http://www.idpf.org/2007/opf" version="3.0" unique-identifier="id">'
                   '<metadata xmlns:dc="http://purl.org/dc/elements/1.1/">'
                   '<dc:title>Generated Book</dc:title><dc:language>en</dc:language></metadata>'
                   '<manifest><item id="ch1" href="text/ch1.xhtml" media-type="application/xhtml+xml"/>'
                   '<item id="ch2" href="text/ch2.xhtml" media-type="application/xhtml+xml"/></manifest>'
                   '<spine><itemref idref="ch1"/><itemref idref="ch2"/></spine></package>')
        for index, title in ((1, "First chapter"), (2, "Second chapter")):
            z.writestr(f"OEBPS/text/ch{index}.xhtml",
                       '<?xml version="1.0" encoding="UTF-8"?>'
                       '<html xmlns="http://www.w3.org/1999/xhtml"><head><title>Generated Book</title></head>'
                       f'<body><h1>{title}</h1><p>Generated text for the CI reader check.</p>'
                       '<ul><li>First item</li><li>Second item</li></ul></body></html>')

    # CBZ
    cbz = os.path.join(path, "generated.cbz")
    with zipfile.ZipFile(cbz, "w", zipfile.ZIP_DEFLATED) as z:
        z.writestr("page-01.png", png_bytes(120, 180, (180, 60, 60)))
        z.writestr("page-02.png", png_bytes(120, 180, (60, 160, 90)))

    return [docx, xlsx, pptx, epub, cbz]


def build_archives(path):
    bundle = os.path.join(path, "generated.zip")
    with zipfile.ZipFile(bundle, "w", zipfile.ZIP_DEFLATED) as z:
        z.writestr("readme.txt", "Generated archive fixture.\n")
        z.writestr("docs/notes.md", "# Notes\n\nArchive preview check.\n")

    seven = None
    zseven = os.path.join(ROOT, "dist", "WinPreview", "7z.exe")
    if os.path.exists(zseven):
        target = os.path.join(path, "generated.7z")
        source = os.path.join(path, "inside.txt")
        write(source, "Generated 7z entry.\n")
        subprocess.run([zseven, "a", "-t7z", target, source],
                       capture_output=True, timeout=120)
        if os.path.exists(target):
            seven = target
        capture = target + ".txt"
        if os.path.exists(capture):
            os.remove(capture)
    return bundle, seven


HTML = """<!doctype html>
<html><head><title>Generated Page</title>
<style>body{color:#123}</style>
<script>window.SCRIPT_MARKER = "must not appear";</script>
</head><body>
<h1>Generated Page</h1>
<p>A <strong>reader</strong> fixture with an &amp; entity and a
<a href="https://example.org/method">link</a>.</p>
<ul><li>One</li><li>Two</li></ul>
<table><tr><th>Key</th><th>Value</th></tr><tr><td>a</td><td>1</td></tr></table>
</body></html>
"""

RTF_FIXTURE = r"""{\rtf1\ansi\deff0{\fonttbl{\f0\fswiss Segoe UI;}}
\fs28\b Generated Rich Text\b0\par
\fs24 This fixture checks the formatted RTF reader.\par
\pard\fi-284\li568\'95\tab First bullet\par
\pard\fi-284\li568\'95\tab Second bullet\par
}
"""


def build_fixtures(work):
    files = []
    files.append((os.path.join(work, "pixel.png"), png_bytes(), {IMAGE}))
    files.append((os.path.join(work, "pixel.bmp"), bmp_bytes(), {IMAGE}))
    files.append((os.path.join(work, "pixel.gif"), GIF_ONE_PIXEL, {IMAGE}))
    files.append((os.path.join(work, "pixel.webp"), WEBP_FIXTURE, {IMAGE}))
    files.append((os.path.join(work, "pixel.jxl"), JXL_FIXTURE, {IMAGE}))
    files.append((os.path.join(work, "broken.jxl"), JXL_BROKEN, {UNSUPPORTED}))
    files.append((os.path.join(work, "vector.svg"),
                  '<svg xmlns="http://www.w3.org/2000/svg" width="160" height="90">'
                  '<rect width="160" height="90" fill="#123a5a"/>'
                  '<circle cx="80" cy="45" r="30" fill="#e8a33d"/></svg>', {SVG}))
    files.append((os.path.join(work, "readme.txt"), "Generated text fixture.\n" * 20, {TEXT}))
    files.append((os.path.join(work, "notes.md"), "# Generated\n\n- one\n- two\n", {MARKDOWN, TEXT}))
    files.append((os.path.join(work, "roster.csv"), "name,count\nRope,3\nCook,2\n", {SHEET}))
    files.append((os.path.join(work, "data.json"), '{"generated": true, "items": [1, 2, 3]}\n', {TEXT}))
    files.append((os.path.join(work, "page.html"), HTML, {MARKDOWN, TEXT}))
    files.append((os.path.join(work, "note.rtf"), RTF_FIXTURE, {RTF, TEXT}))
    files.append((os.path.join(work, "empty.png"), b"", set(range(0, 19))))

    bundle, seven = build_archives(work)
    files.append((bundle, None, {FOLDER}))
    if seven:
        files.append((seven, None, {FOLDER}))

    for path in build_office_packages(work):
        family = COMIC if path.endswith(".cbz") else OFFICE_DOC
        files.append((path, None, {family}))

    # Corrupt containers: alive, honest type, never a crash.
    write(os.path.join(work, "broken.docx"), b"not a package" * 64)
    files.append((os.path.join(work, "broken.docx"), None, {OFFICE_DOC, OFFICE, UNSUPPORTED}))
    write(os.path.join(work, "package.msi"), b"not an installer" * 64)
    files.append((os.path.join(work, "package.msi"), None, {BINARY, UNSUPPORTED}))

    collection = r"C:\Windows\Fonts\cambria.ttc"
    if os.path.exists(collection):
        target = os.path.join(work, "collection.ttc")
        shutil.copyfile(collection, target)
        files.append((target, None, {FONT}))

    # Literal fixtures carry their bytes; write them now.
    for path, data, _expected in files:
        if data is not None:
            write(path, data)

    return files


# --------------------------------------------------------------------------
# Checks
# --------------------------------------------------------------------------

def read_png(path):
    data = open(path, "rb").read()
    if data[:8] != b"\x89PNG\r\n\x1a\n":
        return 0, 0, b""
    i = 8
    width = height = 0
    idat = b""
    while i < len(data):
        length = struct.unpack(">I", data[i:i + 4])[0]
        tag = data[i + 4:i + 8]
        payload = data[i + 8:i + 8 + length]
        if tag == b"IHDR":
            width, height = struct.unpack(">II", payload[:8])
        elif tag == b"IDAT":
            idat += payload
        i += 12 + length
    try:
        return width, height, zlib.decompress(idat)
    except Exception:
        return width, height, b""


def ink_ratio(path):
    width, height, raw = read_png(path)
    if width == 0 or not raw:
        return 0.0
    stride = width * 4
    prev = bytearray(stride)
    pos = 0
    total = lit = 0
    for y in range(height):
        ftype = raw[pos]
        pos += 1
        line = bytearray(raw[pos:pos + stride])
        pos += stride
        if ftype == 1:
            for x in range(4, stride):
                line[x] = (line[x] + line[x - 4]) & 0xFF
        elif ftype == 2:
            for x in range(stride):
                line[x] = (line[x] + prev[x]) & 0xFF
        elif ftype == 3:
            for x in range(stride):
                left = line[x - 4] if x >= 4 else 0
                line[x] = (line[x] + ((left + prev[x]) >> 1)) & 0xFF
        elif ftype == 4:
            for x in range(stride):
                a = line[x - 4] if x >= 4 else 0
                b = prev[x]
                c = prev[x - 4] if x >= 4 else 0
                pr = a + b - c
                pa, pb, pc = abs(pr - a), abs(pr - b), abs(pr - c)
                pred = a if (pa <= pb and pa <= pc) else (b if pb <= pc else c)
                line[x] = (line[x] + pred) & 0xFF
        prev = line
        if y % 2:
            continue
        for x in range(0, width, 2):
            r, g, b = line[x * 4], line[x * 4 + 1], line[x * 4 + 2]
            total += 1
            if abs(r - BG[0]) + abs(g - BG[1]) + abs(b - BG[2]) > 40:
                lit += 1
    return lit / total if total else 0.0


def run_case(path, expected):
    name = os.path.relpath(path, work_dir)
    shot = os.path.join(work_dir, "shot.png")
    diag = os.path.join(work_dir, "diag.json")
    for stale in (shot, diag):
        if os.path.exists(stale):
            os.remove(stale)

    try:
        proc = subprocess.run(
            [EXE, path, "--screenshot", shot, "--width", "1000", "--height", "720",
             "--diagnostics", diag],
            capture_output=True, timeout=120)
    except subprocess.TimeoutExpired:
        check(False, f"{name}: finishes", "timeout")
        return

    check(proc.returncode == 0, f"{name}: exits cleanly", f"exit={proc.returncode}")
    info = None
    if os.path.exists(diag):
        try:
            info = json.load(open(diag, encoding="utf-8"))
        except (ValueError, OSError) as error:
            check(False, f"{name}: diagnostics readable", str(error))
    check(info is not None, f"{name}: writes diagnostics")
    if info:
        doc_type = info.get("docType", -1)
        check(doc_type in expected, f"{name}: docType {doc_type} in {sorted(expected)}")
        if os.path.splitext(name)[0] in ("generated.docx", "generated.xlsx", "generated.pptx"):
            check(info.get("docType") == OFFICE_DOC, f"{name}: office reader", str(info))
        if name.endswith(".epub"):
            check(info.get("officeChapters", 0) > 0, f"{name}: chapters found", str(info))
        if name.endswith(".xlsx"):
            check(info.get("officeSheets", 0) > 0, f"{name}: sheets found", str(info))
        if name.endswith(".pptx"):
            check(info.get("officeSlides", 0) > 0, f"{name}: slides found", str(info))
        if name.endswith(".jxl") and not name.startswith("broken"):
            check(doc_type == IMAGE, f"{name}: bundled JXL decoder", str(info))
        if name.endswith(".webp") and not name.startswith("broken"):
            check(doc_type == IMAGE, f"{name}: bundled WebP decoder", str(info))
        if name.endswith(".html"):
            check(info.get("docType") == MARKDOWN, f"{name}: html reader/browser", str(info))
        if name.endswith(".rtf"):
            check(info.get("rtfPages", 0) >= 1, f"{name}: RTF paginated", str(info))

    if os.path.exists(shot) and os.path.getsize(shot) > 0:
        check(os.path.getsize(shot) > 1000, f"{name}: screenshot has content",
              f"{os.path.getsize(shot)} bytes")
        ratio = ink_ratio(shot)
        check(ratio > 0.001, f"{name}: draws content", f"ratio={ratio:.4f}")
    else:
        check(False, f"{name}: screenshot written")


def main():
    global EXE, work_dir
    args = sys.argv[1:]
    if "--exe" in args:
        EXE = args[args.index("--exe") + 1]
    if not os.path.exists(EXE):
        print(f" [FAIL] exe not found: {EXE}")
        return 1

    work_dir = os.path.join(os.environ.get("TEMP", tempfile.gettempdir()),
                            "winpreview-ci-" + str(os.getpid()))
    shutil.rmtree(work_dir, ignore_errors=True)
    os.makedirs(work_dir, exist_ok=True)
    try:
        cases = build_fixtures(work_dir)
        print(f"Opening {len(cases)} generated fixtures with {os.path.basename(EXE)}...")
        for case_path, _data, expected in cases:
            print(f"[case] {os.path.relpath(case_path, work_dir)}", flush=True)
            run_case(case_path, expected)
    finally:
        shutil.rmtree(work_dir, ignore_errors=True)

    print(f"\nGenerated format matrix: {passed} passed, {failed} failed")
    for label, detail in failures:
        print(f"  FAIL {label}" + (f" ({detail})" if detail else ""))
    return 0 if failed == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
