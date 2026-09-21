#!/bin/sh
# How the image-sequence test files (seq-*) were made. Kept for the record: the files are
# committed, and 'sequence' pins their checksums, so they must not be regenerated casually -
# a different libheif or x265 writes different bytes.
#
# The encoder is a libheif OTHER than the bundled one: the libheif 1.23.3 + x265 4.2 that
# pillow-heif 1.7.0 ships as a Python wheel. So the files are not written by the code that
# reads them. To rebuild:
#
#   python3 -m venv venv && venv/bin/pip install pillow-heif==1.7.0
#   L=$(echo venv/lib/python3*/site-packages/pillow_heif.libs)
#   cc -I../../../Source/LibHEIF/libheif/api mkseq.c   -o mkseq   $L/libheif-*.so.* -Wl,-rpath,$L
#   cc -I../../../Source/LibHEIF/libheif/api mkthumb.c -o mkthumb $L/libheif-*.so.* -Wl,-rpath,$L
#   sh mkseqdata.sh
#
# mkseq out w h durations timescale gop alpha depth mono reps: durations in ticks of
# 'timescale', gop intra/lowdelay/unrestricted, reps -1 = no edit list, 0 = forever, n = n plays.
set -e
cd "$(dirname "$0")"

# five frames of 200, 600, 1000, 1200 and 2000 ticks of 30000 Hz (7, 20, 33, 40, 67 ms): the
# durations libheif gets wrong, attaching each to a later frame; no edit list, so one play
./mkseq seq-vardelay.heics 64 64 200,600,1000,1200,2000 30000 lowdelay 0 8 0 -1
# B-frames (frames decoded in another order than they are shown, a 'ctts' box), looping forever
./mkseq seq-bframes.heics 64 64 3000,3000,3000,3000,3000,3000,3000,3000,3000,3000 90000 unrestricted 0 8 0 0
# an auxiliary alpha track ('auxv' + 'auxl'), looping forever
./mkseq seq-alpha.heics 64 64 100,100,100,100,100,100 1000 lowdelay 1 8 0 0
# 10 bits, played three times
./mkseq seq-10bit.heics 64 64 50,50,50,50 1000 lowdelay 0 10 0 3
# monochrome (4:0:0), all intra
./mkseq seq-mono.heics 64 64 40,40,40,40 1000 intra 0 8 1 1
# sizes x265 has to pad (to 64 x 64): the real size is in the sample entry alone
./mkseq seq-crop.heics 64 48 100,100,100 1000 lowdelay 1 8 0 0
./mkseq seq-odd.heics 33 17 100,100,100 1000 lowdelay 0 8 0 0
# a thumbnail track written before the main one, so it has the lower track ID
./mkthumb seq-thumbfirst.heic
# the same with a still image in the file as well; the major brand libheif writes is 'hevc'
./mkthumb seq-with-still.heic still

python3 seqcraft.py icc seq-vardelay.heics seq-icc.heics
python3 seqcraft.py brand seq-with-still.heic seq-with-still-heic.heic
python3 seqcraft.py corrupt seq-alpha.heics seq-corrupt.heics
python3 seqcraft.py limit seq-mono.heics seq-frames-limit.heics
