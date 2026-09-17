#!/usr/bin/env python3
"""Make and inspect animated WebP files with Pillow, as an oracle for PluginWebP.

Pillow's WebP support is libwebp driven but an entirely separate encoder and decoder
from FreeImage's, so a file it writes is a fair test of the reader, and a file it can
read back is a fair test of the writer.

    python3 webpanim.py make out.webp            # 4 frames, known durations
    python3 webpanim.py show file.webp           # frame count, durations, sizes
"""
import sys

from PIL import Image, ImageSequence

# frame colour and duration, in order
FRAMES = [((220, 30, 30), 120), ((30, 200, 30), 240), ((30, 30, 210), 360), ((230, 210, 20), 480)]
SIZE = (32, 24)


def make(path):
    images = [Image.new("RGBA", SIZE, colour + (255,)) for colour, _ in FRAMES]
    durations = [d for _, d in FRAMES]
    images[0].save(
        path,
        save_all=True,
        append_images=images[1:],
        duration=durations,
        loop=3,
        lossless=True,
    )
    print("wrote %s: %d frames %dx%d, durations %s, loop 3"
          % (path, len(images), SIZE[0], SIZE[1], durations))


def show(path):
    with Image.open(path) as im:
        n = getattr(im, "n_frames", 1)
        print("%s: %d frame(s), canvas %dx%d, loop=%s"
              % (path, n, im.size[0], im.size[1], im.info.get("loop")))
        for i, frame in enumerate(ImageSequence.Iterator(im)):
            rgb = frame.convert("RGB")
            print("  frame %d: %dx%d duration=%s topleft=%s"
                  % (i, frame.size[0], frame.size[1],
                     frame.info.get("duration"), rgb.getpixel((0, 0))))


if __name__ == "__main__":
    if len(sys.argv) != 3:
        raise SystemExit(__doc__)
    {"make": make, "show": show}[sys.argv[1]](sys.argv[2])
