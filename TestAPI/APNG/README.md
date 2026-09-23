# APNG regression tests

Two standalone programs covering the APNG plugin
(`Source/FreeImage/PluginAPNG.cpp`). Each prints a report and exits non-zero on
failure. Neither needs any data files: everything they read, they write first,
either through the plugin or - in `robust` - chunk by chunk by hand.

| test | what it covers |
|---|---|
| `regress` | Round-trips animations through `FreeImage_OpenMultiBitmap` + `AppendPage`, to a file and to a memory stream, and requires every composited frame to come back identical to what went in - including read in reverse, which is what exercises the playback cache's rewind. Asserts that the writer stores a frame as exactly the rectangle that changed and collapses a repeated frame to 1x1; that a single page is written as a plain PNG with its palette and bit depth intact; that the animation metadata (delay, placement, disposal, blending, loop count, canvas) survives; that `InsertPage`/`DeletePage` produce the order asked for; that 32-, 24- and 8-bit frames all become one animation; that alpha survives, a wholly transparent frame included; and that `FIF_LOAD_NOPIXELS` gives geometry without pixels. |
| `robust` | Eighteen files built wrong on purpose, one per rule the format states - a gap in the sequence numbers, a repeated `acTL`, an `acTL` after `IDAT`, a frame outside the canvas, a zero-width frame, an `fdAT` that claims more than the file holds, a critical chunk nobody knows - each with a documented outcome: the animation is abandoned and the default image is still served, or the file is refused. Then a valid animation damaged every way a file gets damaged: truncated at every length, single bytes flipped to four values, 64-byte regions wiped, and every chunk length field rewritten to nonsense - 3165 damaged inputs. They may load or be refused; they may not crash. |

## Running

Build the library first (`make -f Makefile.gnu dist` in the repo root), then:

    make run            # both
    make asan-run       # rebuild PluginAPNG.cpp, PluginPNG.cpp and the whole of
                        # Source/LibPNG with AddressSanitizer, and run both with
                        # those objects linked ahead of the library

Scratch files are written to `$APNG_TEST_TMP`, or the current directory.
`make clean` removes them along with the ASan objects.

## What was checked once, by hand, that these cannot check

The plugin was also settled against two decoders that are not it, over the APNG
conformance suite (the 61 files of <https://philip.html5.org/tests/apng/>, which
is not redistributed here):

- a compositor written straight from PNG Third Edition 11.3.4 in floating
  point. Every valid file in the suite composites to within one level of 255 of
  it, and 37 of the 39 are exact; the two that are not are the ones that blend
  sixteen nearly-transparent frames over each other, where a single level is
  what 8-bit integer arithmetic costs.
- Pillow 12.1. It agrees on 33 of the 39 and disagrees on six, all of them
  cases where its own `OVER` blend decays the alpha towards transparent - the
  suite's stated expectations ("this should be solid grey", "solid dark blue")
  are what this plugin produces and not what Pillow does. Worth knowing before
  using Pillow as an oracle again.

Both decoders were also pointed at files this plugin *wrote*, covering every
path of the writer - placed frames on a larger canvas, explicit disposal and
blending, a one-frame animation, transparency, and a run of identical frames.
The spec-exact compositor agrees with the plugin's own `APNG_PLAYBACK` output on
all of them.
