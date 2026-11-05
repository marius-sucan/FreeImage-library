# Quantizer tests

Standalone programs covering `FIQ_MCQUANT` (`Source/FreeImage/MedianCutQuantizer.cpp`). Each prints a report
and exits non-zero on failure.

| test | what it covers |
|---|---|
| `reference` | 1626 synthetic images against a plain reimplementation of the algorithm below: 15 sizes from 1x1 to 300x300 and 1031x517, 24- and 32-bit, six kinds of content (noise, color ramps, stripes, flat grey, a checkerboard, grey ramps), palettes of 2 to 256 colors, reserved colors too. The palette and every index must be equal. |

## Running

Build the library first (`make -f Makefile.gnu dist` in the repo root), then:

    make run            # reference
    make asan-run       # MedianCutQuantizer.cpp rebuilt with AddressSanitizer and
                        # UndefinedBehaviorSanitizer, linked ahead of the library

## How FIQ_MCQUANT works

1. A 32x32x32 histogram of the top 5 bits of R, G and B, every cell starting at 1.
2. Median cut, from one box holding every cell. The box split next has the largest population times longest
   side (the first one on ties); a one-cell box, whose side is 0, is never split. It is cut along its longest
   side (R, then G, then B on ties), after the floored centroid level on that axis, kept below the box's last
   level. The upper half stays in the box's slot and the lower half is appended.
3. The boxes are sorted by population, largest first, keeping the order of equal ones. Each color is the box's
   truncated mean level, expanded to 8 bits as round(level * 255 / 31). The counts include the starting ones.
4. A lookup of 4096 cells by the top 4 bits of R, G and B gives the palette entry nearest to the cell's color,
   c * 17 per channel (squared distance, the lowest index on ties).
5. Floyd-Steinberg (7, 3, 5 and 1 sixteenths) in serpentine order, the top row from right to left. A pixel's
   errors are summed in sixteenths and floored once: v = clamp(pixel + floor(sum / 16), 0, 255). The lookup
   cell is v's top 4 bits, and the error passed on is v minus the palette color. Errors that leave the image are
   dropped.

`FIQ_MCQUANT` always dithers. Reserved colors (`FreeImage_ColorQuantizeEx`) follow the computed ones, and the
lookup chooses among both.
