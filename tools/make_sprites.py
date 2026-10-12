#!/usr/bin/env python3
"""Turn the drawings in assets/src into the pictures the screensaver builds in,
under assets/art.

    tools/make_sprites.py

parts.jpeg is a sheet: eight drawings on one flat magenta background, not touching.
It is cut into its separate drawings, read off in rows, left to right; plate.jpeg
is the pusher plate on its own. blasts.jpeg is three pictures of light on black, side by side; they stay on black, because the
program adds them to what is behind. space.jpeg is the background. Needs
ImageMagick and numpy.
"""
import subprocess
import numpy as np

SRC, OUT = "assets/src", "assets/art"
ROWS = 2
PARTS = [(None, 0), (None, 0), ("orb", 300), ("hub", 300), ("charge", 120), ("cyl", 110), ("rod", 80), (None, 0)]  # name, pixels tall

def load(path):
    w, h = map(int, subprocess.check_output(["magick", "identify", "-format", "%w %h", path]).split())
    raw = subprocess.check_output(["magick", path, "-depth", "8", "RGB:-"])
    return np.frombuffer(raw, np.uint8).reshape(h, w, 3).astype(np.float32)

def key(img):
    """Magenta background to alpha, and take its colour cast off the edges."""
    r, g, b = img[..., 0].copy(), img[..., 1].copy(), img[..., 2].copy()
    spill = np.minimum(r, b) - g
    cut = np.clip(spill, 0, None) * 0.92
    r, b = r - cut, b - cut
    alpha = np.clip(1.0 - (spill - 24) / 70.0, 0, 1)
    return np.dstack([r, g, b, alpha * 255]).clip(0, 255).astype(np.uint8)

def runs(on, gap):
    """Stretches where `on` is true, joining ones separated by less than `gap`."""
    out, start, last = [], None, None
    for i, v in enumerate(on):
        if not v: continue
        if start is None: start = i
        elif i - last > gap: out.append((start, last + 1)); start = i
        last = i
    if start is not None: out.append((start, last + 1))
    return [r for r in out if r[1] - r[0] > 30]

def pieces(a, rows):
    """The separate drawings on a sheet, in reading order: split into rows by the
    empty bands between them, then each row into drawings by the empty gaps."""
    solid = a[..., 3] > 128
    bands = runs(solid.sum(axis=1) > 2, 40)
    if len(bands) != rows:
        raise SystemExit(f"found {len(bands)} rows of drawings, expected {rows}")
    out = []
    for y0, y1 in bands:
        for x0, x1 in runs(solid[y0:y1].sum(axis=0) > 2, 40):
            out.append(a[y0:y1, x0:x1])
    return out

def trim(a):
    solid = a[..., 3] > 128
    rows = np.where(solid.sum(axis=1) > 2)[0]; cols = np.where(solid.sum(axis=0) > 2)[0]
    return a[rows.min():rows.max() + 1, cols.min():cols.max() + 1]

def save(rgba, w, h, path):
    subprocess.run(["magick", "-size", f"{rgba.shape[1]}x{rgba.shape[0]}", "-depth", "8", "RGBA:-",
                    "-filter", "Lanczos", "-resize", f"{w}x{h}!", "-define", "png:compression-level=9", path],
                   input=np.ascontiguousarray(rgba).tobytes(), check=True)

def main():
    subprocess.run(["mkdir", "-p", OUT], check=True)
    got = [trim(p) for p in pieces(key(load(f"{SRC}/parts.jpeg")), ROWS)]
    if len(got) != len(PARTS):
        raise SystemExit(f"parts: found {len(got)} drawings, expected {len(PARTS)}")
    for (name, tall), p in zip(PARTS, got):
        if not name: continue  # the plate has a drawing to itself
        tall = min(tall, p.shape[0])
        w = max(1, round(p.shape[1] * tall / p.shape[0]))
        save(p, w, tall, f"{OUT}/{name}.png")
        print(name, w, tall)
    p = trim(key(load(f"{SRC}/plate.jpeg")))
    save(p, round(p.shape[1] * 700 / p.shape[0]), 700, f"{OUT}/plate.png")
    print("plate")
    # the frame: the joint at each corner, and the gravel skin as a square of texture
    got = pieces(key(load(f"{SRC}/frame.jpeg")), 1)
    j = trim(got[0])
    save(j, round(j.shape[1] * 260 / j.shape[0]), 260, f"{OUT}/joint.png")
    g = trim(got[1]); n = min(g.shape[:2]); m = n // 6
    g = g[m:n - m, m:n - m, :3]
    subprocess.run(["magick", "-size", f"{g.shape[1]}x{g.shape[0]}", "-depth", "8", "RGB:-", "-resize", "512x512!",
                    "-brightness-contrast", "12x-6", "-quality", "90", f"{OUT}/gravel.jpg"], input=np.ascontiguousarray(g).tobytes(), check=True)
    print("joint, gravel")
    # the blasts: split where the picture is dark all the way down
    img = load(f"{SRC}/blasts.jpeg")
    lit = img.max(axis=2).max(axis=0) > 40
    cols = runs(lit, 30)
    if len(cols) != 3:
        raise SystemExit(f"blasts: found {len(cols)} pictures, expected 3")
    for name, (x0, x1) in zip(["flash", "plasma", "vapour"], cols):
        part = img[:, max(0, x0 - 20):x1 + 20]
        rows = np.where(part.max(axis=2).max(axis=1) > 40)[0]
        part = part[max(0, rows.min() - 20):rows.max() + 20]
        if name == "flash":  # square, with the brightest part in the middle
            n = max(part.shape[:2]); sq = np.zeros((n, n, 3), np.float32)
            y, x = (n - part.shape[0]) // 2, (n - part.shape[1]) // 2
            sq[y:y + part.shape[0], x:x + part.shape[1]] = part; part = sq
        part = np.clip((part - 10) * 255 / 245, 0, 255).astype(np.uint8)  # true black, so nothing shows where there is no light
        subprocess.run(["magick", "-size", f"{part.shape[1]}x{part.shape[0]}", "-depth", "8", "RGB:-", "-resize", "600x600>",
                        "-quality", "92", f"{OUT}/{name}.jpg"], input=np.ascontiguousarray(part).tobytes(), check=True)
        print(name, part.shape[1], part.shape[0])
    subprocess.run(["magick", f"{SRC}/space.jpeg", "-resize", "2200x1238!", "-quality", "90", f"{OUT}/space.jpg"], check=True)
    print("space")

if __name__ == "__main__":
    main()
