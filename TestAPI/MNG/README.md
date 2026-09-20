# MNG tests

Two standalone programs covering `Source/FreeImage/PluginMNG.cpp`. Each prints
a report and exits non-zero on failure.

```
make -f Makefile.gnu dist      # from the repository root, first
cd TestAPI/MNG
make run                       # build and run both
make asan-run                  # and again with AddressSanitizer
```

Scratch files go to `$MNG_TEST_TMP`, or the current directory.

## There is no data directory

Every other suite here keeps a corpus of files some encoder produced, and is
careful never to regenerate it. This one cannot: MNG has no writer, in this
library or in much else that is still maintained, so there is nothing to make a
corpus with and nothing a regenerated one could be compared against.

So the tests build their datastreams themselves, from the chunk layouts in the
specification, and `mngbuild.h` holds the builders. That turns out to be the
better arrangement for a container format: the file each test needs is written
in the test, next to the expectation it exists to check, and a reader can see
both at once.

The frames inside those files are made with FreeImage's own PNG writer and
stripped of their 8-byte signature, which is exactly what a MNG encoder does
with them. Using FreeImage to make them does not make the tests circular: the
subject is the container - which images are pages, when they are shown, where
they are drawn - and any valid PNG serves as the payload. That the right frame
comes back out is checked by its colour.

## decode

What a conforming reader must report, taken from the specification rather than
from the plugin:

- **MNG-VLC**, a bare sequence of images with no FRAM: one page per image, and
  a delay of one tick, because "when ticks_per_second is nonzero, and there is
  no other information available about interframe delay, viewers should display
  the sequence of frames at the rate of one frame per tick".
- **Framing modes.** Mode 1 puts the interframe delay on every foreground
  layer; mode 2 puts it on the last layer of each subframe and zero on the
  others. A one-shot delay lasts until the next FRAM; an empty FRAM is "just a
  subframe delimiter" and is not an image.
- **An infinite tick.** `ticks_per_second = 0` means no delay can be defined,
  however hard a FRAM tries.
- **Loop counts.** LOOP and TERM both say how many times the animation plays,
  and `0x7fffffff` means forever, which FreeImage spells 0. The images are in
  the file once and are pages once: a loop does not repeat them, which is what
  the spec expects of "MNG editors that extract a series of PNG or JNG files".
  A LOOP whose iteration count is zero runs its body no times at all, so the
  images inside it are not pages.
- **Placement.** DEFI puts an image on the canvas; a raw page is the rectangle
  the file stores, tagged with where it goes, and `MNG_PLAYBACK` is the canvas a
  viewer would show - composited, 32-bit, on the BACK colour.
- **DEFI's omitted fields.** The spec's defaults fill them only "when an object
  with the same object_id has not been previously defined"; for an id already
  defined, its own attributes stand.
- **SHOW.** An image defined with `do_not_show` is not a page, and becomes one
  each time a SHOW displays it - the object reuse a MNG-LC file is built on.
- **Header-only loads**, which give the size and the timing and no pixels.
- **A canvas the file cannot justify.** MHDR may ask for 65535x65535 next to a
  single 16x16 image; composing that is seventeen gigabytes, so it is refused,
  and the images stay readable without `MNG_PLAYBACK`.

## robust

Damaged files, on the principle that refusing one is a fine answer and the only
wrong answers are dying and lying. Every case is opened three ways - raw,
`MNG_PLAYBACK` and `FIF_LOAD_NOPIXELS` - and every page it claims is locked and
touched.

Covered: every truncation of a valid file; chunk lengths that cannot be;
a FRAM cut short at each of the lengths its optional groups can end at; a DEFI
of every length from 0 to 28, including the ones the spec does not allow; an
embedded image whose IEND never arrives; a MEND before any image; an ENDL with
no LOOP and a LOOP with no ENDL; 250 nested loops; SHOW naming objects that do
not exist, ranges the wrong way round, and modes the spec does not define; a bad
CRC on each chunk that steers the parser; impossible canvases; images placed far
off the canvas in both directions, including at `INT_MIN`; and four kilobytes of
garbage behind a valid signature.

`make asan-run` is the one that matters here. The plugin parses the container by
hand - chunk headers, lengths, the extent of each embedded datastream, the
variable-length bodies of FRAM and DEFI - and a sanitized run over deliberately
damaged input is what catches a mistake in that. It rebuilds `PluginMNG.cpp`,
`MNGHelper.cpp`, `PluginJNG.cpp`, `PluginPNG.cpp`, `MultiPage.cpp` and the
bundled libpng with AddressSanitizer into a private copy of the library.
