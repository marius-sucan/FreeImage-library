// Test-data generator for TestAPI/HEIF (see mkseqdata.sh): an HEVC image sequence written
// by an external libheif (>= 1.21, with x265), not the bundled one. Not part of the library.
//
// usage: mkseq out w h durations(comma list, ticks) timescale gop(intra|lowdelay|unrestricted)
//              alpha(0|1) depth(8|10) mono(0|1) reps(-1 = no edit list, 0 = forever, n) [lossless]
#define LIBHEIF_STATIC_BUILD
#include <libheif/heif.h>
#include <libheif/heif_sequences.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void check(heif_error e, const char *what) {
	if (e.code) {
		fprintf(stderr, "%s: %s\n", what, e.message);
		exit(1);
	}
}

int main(int argc, char **argv) {
	if (argc < 11) {
		fprintf(stderr, "usage: mkseq out w h durations timescale gop alpha depth mono reps [lossless]\n");
		return 2;
	}
	const char *out = argv[1];
	const int w = atoi(argv[2]), h = atoi(argv[3]);
	int durations[4096], n = 0;
	for (char *p = strtok(argv[4], ","); p && n < 4096; p = strtok(NULL, ",")) durations[n++] = atoi(p);
	const unsigned timescale = (unsigned)atoi(argv[5]);
	const char *gop = argv[6];
	const int alpha = atoi(argv[7]), depth = atoi(argv[8]), mono = atoi(argv[9]), reps = atoi(argv[10]);
	const int lossless = (argc > 11) && !strcmp(argv[11], "lossless");

	heif_context *ctx = heif_context_alloc();
	heif_encoder *enc = NULL;
	check(heif_context_get_encoder_for_format(ctx, heif_compression_HEVC, &enc), "encoder");
	if (lossless) {
		check(heif_encoder_set_lossless(enc, 1), "lossless");
	} else {
		check(heif_encoder_set_lossy_quality(enc, 90), "quality");
	}
	heif_context_set_sequence_timescale(ctx, timescale);
	if (reps >= 0) heif_context_set_number_of_sequence_repetitions(ctx, (uint32_t)reps);

	heif_track_options *topt = heif_track_options_alloc();
	heif_track_options_set_timescale(topt, timescale);
	heif_sequence_encoding_options *sopt = heif_sequence_encoding_options_alloc();
	sopt->gop_structure = !strcmp(gop, "intra") ? heif_sequence_gop_structure_intra_only :
	                      !strcmp(gop, "lowdelay") ? heif_sequence_gop_structure_lowdelay :
	                      heif_sequence_gop_structure_unrestricted;
	sopt->save_alpha_channel = alpha;
	sopt->keyframe_distance_max = 8;

	heif_track *track = NULL;
	check(heif_context_add_visual_sequence_track(ctx, (uint16_t)w, (uint16_t)h, heif_track_type_image_sequence, topt, sopt, &track), "add track");

	for (int f = 0; f < n; f++) {
		heif_image *img = NULL;
		if (mono) {
			check(heif_image_create(w, h, heif_colorspace_monochrome, heif_chroma_monochrome, &img), "create");
			check(heif_image_add_plane(img, heif_channel_Y, w, h, depth), "plane Y");
			if (alpha) check(heif_image_add_plane(img, heif_channel_Alpha, w, h, depth), "plane A");
		} else {
			heif_chroma chroma = (depth > 8) ? (alpha ? heif_chroma_interleaved_RRGGBBAA_LE : heif_chroma_interleaved_RRGGBB_LE)
			                                 : (alpha ? heif_chroma_interleaved_RGBA : heif_chroma_interleaved_RGB);
			check(heif_image_create(w, h, heif_colorspace_RGB, chroma, &img), "create");
			check(heif_image_add_plane(img, heif_channel_interleaved, w, h, depth), "plane");
		}
		const int max = (1 << depth) - 1;
		for (int c = 0; c < (mono ? (alpha ? 2 : 1) : 1); c++) {
			heif_channel ch = mono ? (c ? heif_channel_Alpha : heif_channel_Y) : heif_channel_interleaved;
			size_t stride = 0;
			uint8_t *plane = heif_image_get_plane2(img, ch, &stride);
			for (int y = 0; y < h; y++) {
				for (int x = 0; x < w; x++) {
					// a diagonal gradient that moves with the frame, and a block marking the frame number
					int v[4];
					v[0] = ((x + f * 7) * max / (w + 1)) % (max + 1);
					v[1] = ((y + f * 5) * max / (h + 1)) % (max + 1);
					v[2] = (((x + y) / 2 + f * 11) * max / ((w + h) / 2 + 1)) % (max + 1);
					v[3] = alpha ? ((x * 2 + f * 13) % (w + 1)) * max / w : max;
					if (x < 8 * (f % 16 + 1) && y < 8) { v[0] = v[1] = v[2] = max; v[3] = max; }
					if (mono) {
						int s = c ? v[3] : v[2];
						if (depth > 8) ((uint16_t *)(plane + y * stride))[x] = (uint16_t)s;
						else plane[y * stride + x] = (uint8_t)s;
					} else {
						int k = alpha ? 4 : 3;
						for (int i = 0; i < k; i++) {
							if (depth > 8) ((uint16_t *)(plane + y * stride))[x * k + i] = (uint16_t)v[i];
							else plane[y * stride + x * k + i] = (uint8_t)v[i];
						}
					}
				}
			}
		}
		heif_image_set_duration(img, (uint32_t)durations[f]);
		check(heif_track_encode_sequence_image(track, img, enc, sopt), "encode");
		heif_image_release(img);
	}
	check(heif_track_encode_end_of_sequence(track, enc), "end");
	check(heif_context_write_to_file(ctx, out), "write");
	heif_track_release(track);
	heif_encoder_release(enc);
	heif_sequence_encoding_options_release(sopt);
	heif_track_options_release(topt);
	heif_context_free(ctx);
	printf("%s: %d frames %dx%d\n", out, n, w, h);
	return 0;
}
