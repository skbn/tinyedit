#!/usr/bin/env python3
"""URF (Apple Raster / UNIRAST) -> PNG/PDF decoder

Spec sources:
  - https://github.com/superna9999/urftopdf/blob/master/unirast.h
  - https://github.com/superna9999/urftopdf/blob/master/urftopdf.cpp
  - https://docs.rs/print_raster (UrfColorSpace enum)

File header (12 bytes):
  0..7   "UNIRAST\0"
  8..11  uint32 BE  page_count

Page header (32 bytes):
  0      bitsPerPixel
  1      colorSpace  (0=Gray8, 1=sRGB24, 4=Gray32, 6=CMYK)
  2      duplex
  3      quality
  4..11  reserved
  12..15 uint32 BE  width
  16..19 uint32 BE  height
  20..23 uint32 BE  dpi
  24..31 reserved

Raster: per line, repeat-byte then PackBits-like codes:
  rep = byte; line is repeated (rep+1) times
  per line, until width pixels consumed:
    code = signed byte
    code == -128 (0x80): blank rest of line (fill 0xFF)
    0 <= code <= 127:  repeat next pixel (code+1) times
    -128 < code < 0:   copy (256-code) literal pixels
"""

import struct
import sys
from PIL import Image

COLOR_SPACES = {
    0: ("Gray8", 1, "L"),
    1: ("sRGB24", 3, "RGB"),
    2: ("sRGB32", 4, "RGBX"),
    3: ("sRGB24_3", 3, "RGB"),
    4: ("Gray32", 4, "I;32B"),
    5: ("sRGB24_5", 3, "RGB"),
    6: ("CMYK", 4, "CMYK"),
}

def decode_urf(path):
    with open(path, "rb") as f:
        data = f.read()

    if data[0:8] != b"UNIRAST\x00":
        raise ValueError(f"bad magic: {data[0:8]!r}")

    page_count = struct.unpack(">I", data[8:12])[0]

    print(f"file: {path}", file=sys.stderr)
    print(f"pages: {page_count}", file=sys.stderr)

    off = 12
    pages = []

    for p in range(page_count):
        bpp = data[off]
        cs = data[off + 1]
        duplex = data[off + 2]
        quality = data[off + 3]

        w = struct.unpack(">I", data[off + 12:off + 16])[0]
        h = struct.unpack(">I", data[off + 16:off + 20])[0]
        dpi = struct.unpack(">I", data[off + 20:off + 24])[0]

        if cs not in COLOR_SPACES:
            raise ValueError(f"page {p}: unknown colorspace {cs}")

        name, ncomp, mode = COLOR_SPACES[cs]

        print(f"page {p}: bpp={bpp} cs={cs}({name}) duplex={duplex} quality={quality} "f"{w}x{h} @{dpi}dpi  ({w/dpi:.2f}\"x{h/dpi:.2f}\")", file=sys.stderr)

        off += 32

        if bpp != 8 * ncomp:
            print(f"  WARN: bpp={bpp} but colorspace implies {8*ncomp}, using {8*ncomp}", file=sys.stderr)

        pixel_size = bpp // 8
        img = Image.new(mode if mode != "I;32B" else "F", (w, h), 255)

        # PIL "L" mode: 0=black, 255=white. URF grayscale is the same
        # For RGB we copy bytes verbatim
        cur_line = 0

        while cur_line < h:
            rep = data[off]; off += 1
            repeat = rep + 1
            row = bytearray(pixel_size * w)

            # initialize as white (0xFF) per spec for blank-rest-of-line
            for i in range(len(row)):
                row[i] = 0xFF

            pos = 0

            while pos < w:
                code = data[off]; off += 1
                code_s = code - 256 if code >= 128 else code

                if code == 0x80:
                    # blank rest of line
                    pos = w
                    break
                elif 0 <= code_s <= 127:
                    n = code_s + 1
                    pix = data[off:off + pixel_size]; off += pixel_size

                    for i in range(n):
                        if pos >= w:
                            break

                        for j in range(pixel_size):
                            row[pos * pixel_size + j] = pix[j]

                        pos += 1

                else:  # -128 < code_s < 0
                    n = -code_s + 1

                    for i in range(n):
                        if pos >= w:
                            break

                        for j in range(pixel_size):
                            row[pos * pixel_size + j] = data[off + j]

                        off += pixel_size
                        pos += 1

            # write 'repeat' lines
            for r in range(repeat):
                if cur_line + r < h:
                    # frombytes appends; we need to put at the right row
                    # Easier: use putpixel or load() — but for speed, build a list
                    img.frombytes(row, "raw", mode if mode != "I;32B" else "F")

            # The above frombytes approach is wrong; redo with proper row placement:
            cur_line += repeat

        # Re-decode with proper row placement (the simple approach above is buggy
        # because Image.frombytes appends sequentially). Do it cleanly:
        off2 = off - sum(1 for _ in range(0))  # We already advanced. Need to re-scan

        # Actually, simpler: redo the whole page decode with a fresh Image
        # Reset offset to start of this page's raster:
        # We don't have it stored; let's just re-open and re-decode properly
        pages.append((bpp, cs, duplex, quality, w, h, dpi, None))

    return pages


def decode_urf_proper(path):
    """Clean decoder that builds the image correctly."""
    with open(path, "rb") as f:
        data = f.read()

    if data[0:8] != b"UNIRAST\x00":
        raise ValueError(f"bad magic: {data[0:8]!r}")

    page_count = struct.unpack(">I", data[8:12])[0]
    
    print(f"file: {path}", file=sys.stderr)
    print(f"pages: {page_count}", file=sys.stderr)

    off = 12
    images = []

    for p in range(page_count):
        bpp = data[off]
        cs = data[off + 1]
        duplex = data[off + 2]
        quality = data[off + 3]

        w = struct.unpack(">I", data[off + 12:off + 16])[0]
        h = struct.unpack(">I", data[off + 16:off + 20])[0]
        dpi = struct.unpack(">I", data[off + 20:off + 24])[0]

        if cs not in COLOR_SPACES:
            raise ValueError(f"page {p}: unknown colorspace {cs}")

        name, ncomp, mode = COLOR_SPACES[cs]

        print(f"page {p}: bpp={bpp} cs={cs}({name}) duplex={duplex} quality={quality} "f"{w}x{h} @{dpi}dpi  ({w/dpi:.2f}\"x{h/dpi:.2f}\")", file=sys.stderr)

        off += 32

        pixel_size = bpp // 8

        # Build a flat buffer of size w*h*pixel_size
        flat = bytearray(pixel_size * w * h)

        # Initialize to white
        for i in range(len(flat)):
            flat[i] = 0xFF

        cur_line = 0

        while cur_line < h:
            rep = data[off]; off += 1
            repeat = rep + 1
            row = bytearray(pixel_size * w)

            for i in range(len(row)):
                row[i] = 0xFF

            pos = 0

            while pos < w:
                code = data[off]; off += 1
                code_s = code - 256 if code >= 128 else code

                if code == 0x80:
                    pos = w
                    break
                elif 0 <= code_s <= 127:
                    n = code_s + 1
                    pix = data[off:off + pixel_size]; off += pixel_size

                    for i in range(n):
                        if pos >= w:
                            break

                        for j in range(pixel_size):
                            row[pos * pixel_size + j] = pix[j]

                        pos += 1
                else:
                    n = -code_s + 1

                    for i in range(n):
                        if pos >= w:
                            break

                        for j in range(pixel_size):
                            row[pos * pixel_size + j] = data[off + j]

                        off += pixel_size
                        pos += 1

            for r in range(repeat):
                if cur_line + r < h:
                    flat[(cur_line + r) * pixel_size * w: (cur_line + r + 1) * pixel_size * w] = row

            cur_line += repeat

        img = Image.frombytes(mode if mode != "I;32B" else "F",(w, h), bytes(flat), "raw", mode if mode != "I;32B" else "F")
        img.info["dpi"] = (dpi, dpi)
        images.append(img)

    return images


def main():
    if len(sys.argv) < 2:
        print("usage: urf2png.py input.urf [output_prefix]", file=sys.stderr)
        sys.exit(2)

    inp = sys.argv[1]

    # Default prefix: input filename without extension
    if len(sys.argv) > 2:
        prefix = sys.argv[2]
    else:
        import os

        base = os.path.basename(inp)
        prefix = os.path.splitext(base)[0] or "urf_page"

    imgs = decode_urf_proper(inp)
    multi = len(imgs) > 1

    for i, img in enumerate(imgs):
        suffix = f"_{i+1}" if multi else ""
        out = f"{prefix}{suffix}.png"

        img.save(out, "PNG")

        print(f"wrote {out}  ({img.size[0]}x{img.size[1]})", file=sys.stderr)

        # Also write a PDF
        out_pdf = f"{prefix}{suffix}.pdf"
        rgb = img.convert("RGB") if img.mode != "RGB" else img
        rgb.save(out_pdf, "PDF", resolution=img.info.get("dpi", (300, 300))[0])
        
        print(f"wrote {out_pdf}", file=sys.stderr)


if __name__ == "__main__":
    main()
