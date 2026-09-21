# Third-party HEIF samples

Files that other HEIF writers produced, for `thirdparty` (see `../thirdparty.c`): Nokia's
example image sequences and image collections, and the image-sequence files of the MPEG
file-format conformance suite. They are the independent check on the image-sequence
support - everything in `../data/` was written either by libheif or from its test corpus.

The files themselves are not in the repository (`../.gitignore` keeps them out); fetch
them with

    sh ../fetch_samples.sh      # or: make thirdparty-run, from TestAPI/HEIF

which downloads each one from its source and checks it against the MD5 sum the test's
expectations were recorded with. A file whose source has changed is reported, not used.

## Where they come from, and on what terms

- `nokia/`: the example files of Nokia's HEIF web site
  (<https://nokiatech.github.io/heif/examples.html>, the `gh-pages` branch of
  <https://github.com/nokiatech/heif>). Nokia's README says: "All the example media files
  (*.heic, *.png, *.jpg, *.gif) in this repository are under copyright © Nokia
  Technologies 2015-2025", "All rights reserved", and its license (the Nokia HEIF License
  2.1) grants use, modification and copying for non-commercial evaluation, testing and
  research only. It grants no right to redistribute - which is why the files are
  downloaded rather than committed.
- `mpeg/`: published conformance files for ISO/IEC 23008-12 from
  <https://github.com/MPEGGroup/FileFormatConformance>
  (`data/file_features/published/heif/`, stored with Git LFS), which that repository
  distributes under the Clear BSD License ("Copyright (c) 2023, Apple Inc."). They were
  contributed by Nokia (MPEG document m42431) and first published in
  <https://github.com/nokiatech/heif_conformance>, which carries no license of its own.
  Each file's description is in the `.json` file of the same name in the MPEG repository.

## What each one exercises

| file | what it is | what the plugin must make of it |
|---|---|---|
| `nokia/starfield_animation.heic`, `nokia/sea1_animation.heic` | 120 frames of 256 x 144 at 25 fps, P-frames between keyframes every 16, and a cover image | major brand `msf1`: the 120 frames, not the cover image |
| `nokia/bird_burst.heic`, `nokia/rally_burst.heic` | a burst: 4 still images, a 640 x 360 track (90 / 60 frames) and a 128 x 72 thumbnail track | the main track's frames, not the thumbnails' and not the stills |
| `nokia/random_collection_1440x960.heic` | 4 still images | 4 pages, no animation tags |
| `nokia/stereo_1200x800.heic` | a stereo pair of still images | 2 pages, no animation tags |
| `mpeg/C001.heic` | an image item and an image sequence, the item primary | major brand `msf1`: the 8 frames |
| `mpeg/C026.heic` | an image sequence | 8 frames, 20 ms each |
| `mpeg/C027.heic`, `mpeg/C028.heic` | inter prediction, without and with intra prediction in the inter frames | 16 frames each |
| `mpeg/C029.heic` | an edit list playing the first 5 samples twice | every sample once (8 frames), as libheif decodes it; ffmpeg plays 13 |
| `mpeg/C030.heic` | an edit list with a pause at the beginning | the 8 frames, the pause not played |
| `mpeg/C031.heic` | an image sequence and a video track as alternatives | the image sequence track |
| `mpeg/C032.heic` | an image sequence with a thumbnail track | the main track |
| `mpeg/C036.heic`, `C037.heic`, `C038.heic` | an edit list in repeat mode: 3 times, 1.5 times, forever | "Loop" 3, 1 (libheif rounds down), 0 |
| `mpeg/C041.heic` | a sample marked not for display (composition offset -2^31), then 8 inter frames | 9 frames, as libheif decodes it - the hidden one is the same picture as the fifth; ffmpeg shows 8 |

## How the expectations were checked

Before they were pinned, every file was decoded by ffmpeg 8 through PyAV as well - a
decoder that shares nothing with libheif and libde265 - and compared page by page: the
same frame counts, frame order and durations (except for C029 and C041, above), and the
same pictures. The decoded luma planes are bit-identical on all four Nokia sequences, on
C001, C026, C027 and C028, and on C041's eight displayed frames; the pages of the two still
collections match ffmpeg's decode of each image item, in item order, within the rounding of
the colour conversion. The nine MPEG files built on the same eight-frame bitstream (C001,
C026, C029-C032, C036-C038) also share a checksum here: however the container around it is
arranged, the frames are the same.
