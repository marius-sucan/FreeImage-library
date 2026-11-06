# Quantizer tests

Standalone programs covering `FIQ_MCQUANT` (`Source/FreeImage/MedianCutQuantizer.cpp`). Each prints a report
and exits non-zero on failure.

| test | what it covers |
|---|---|
| `reference` | 1631 synthetic images against a plain reimplementation of the algorithm below: 15 sizes from 1x1 to 300x300 and 1031x517, 24- and 32-bit, six kinds of content (noise, color ramps, stripes, flat grey, a checkerboard, grey ramps), palettes of 2 to 256 colors, reserved colors too; stripes and reserved colors that decide cells by the inside rule, and one color over 17 million pixels. The palette and every index must be equal. |

## Running

Build the library first (`make -f Makefile.gnu dist` in the repo root), then:

    make run            # reference
    make asan-run       # MedianCutQuantizer.cpp rebuilt with AddressSanitizer and
                        # UndefinedBehaviorSanitizer, linked ahead of the library

## How FIQ_MCQUANT works

1. A 32x32x32 histogram of the top 5 bits of R, G and B, with each cell's pixel count and color sums.
2. Median cut, from the box of the occupied cells. The box split next has the largest population times longest
   side (the first one on ties); a one-cell box, whose side is 0, is never split. It is cut along its longest
   side (R, then G, then B on ties), after the floored centroid level on that axis, kept below the box's last
   level, and both halves shrink to the cells that hold pixels. The upper half stays in the box's slot and the
   lower half is appended.
3. The boxes are sorted by population, largest first, keeping the order of equal ones. A one-cell box's color
   is the mean color of its pixels, rounded half up; a larger box's is its truncated mean level, expanded to 8 bits as
   round(level * 255 / 31).
4. A lookup of 32768 cells by the top 5 bits of R, G and B: a palette color inside the cell wins it, the one
   nearest the cell's center; otherwise the color nearest the center, 8 * level + 3.5 per channel. Distances
   are squared, and ties go to the lowest index.
5. Floyd-Steinberg (7, 3, 5 and 1 sixteenths) in serpentine order, the top row from right to left. A pixel's
   errors are summed in sixteenths and floored once: v = clamp(pixel + floor(sum / 16), 0, 255). The lookup
   cell is v's top 5 bits, and the error passed on is v minus the palette color. Errors that leave the image are
   dropped.

`FIQ_MCQUANT` always dithers. Reserved colors (`FreeImage_ColorQuantizeEx`) take the last entries, and the
lookup chooses among them and the computed ones; when the image has fewer occupied cells than the entries before
them, copies of the first color fill the gap, and the lookup never prefers a copy to it.
