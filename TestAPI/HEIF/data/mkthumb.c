// Test-data generator for TestAPI/HEIF (see mkseqdata.sh): an image sequence whose thumbnail
// track comes first, with the lower track ID - the case where libheif's heif_context_get_track(ctx, 0)
// picks the thumbnail. Written by an external libheif (>= 1.21, with x265). Not part of the library.
// usage: mkthumb out.heif [still]   ("still": also a primary image item, the first frame)
#define LIBHEIF_STATIC_BUILD
#include <libheif/heif.h>
#include <libheif/heif_sequences.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void check(heif_error e, const char *what) {
	if (e.code) { fprintf(stderr, "%s: %s\n", what, e.message); exit(1); }
}

static heif_image *frame(int w, int h, int f, int scale) {
	heif_image *img = NULL;
	check(heif_image_create(w, h, heif_colorspace_RGB, heif_chroma_interleaved_RGB, &img), "create");
	check(heif_image_add_plane(img, heif_channel_interleaved, w, h, 8), "plane");
	size_t stride = 0;
	uint8_t *p = heif_image_get_plane2(img, heif_channel_interleaved, &stride);
	for (int y = 0; y < h; y++) {
		for (int x = 0; x < w; x++) {
			const int X = x * scale, Y = y * scale;
			uint8_t *px = p + y * stride + x * 3;
			px[0] = (uint8_t)((X * 4 + f * 40) & 255);
			px[1] = (uint8_t)((Y * 4 + f * 20) & 255);
			px[2] = (uint8_t)(((X + Y) * 2 + f * 60) & 255);
			if (X < 8 * (f + 1) && Y < 8) px[0] = px[1] = px[2] = 255;
		}
	}
	return img;
}

int main(int argc, char **argv) {
	if (argc < 2) return 2;
	const int still = (argc > 2) && !strcmp(argv[2], "still");
	const int frames = 4;
	heif_context *ctx = heif_context_alloc();
	heif_context_set_sequence_timescale(ctx, 1000);

	heif_encoder *enc_thumb = NULL, *enc_main = NULL;
	check(heif_context_get_encoder_for_format(ctx, heif_compression_HEVC, &enc_thumb), "encoder");
	check(heif_context_get_encoder_for_format(ctx, heif_compression_HEVC, &enc_main), "encoder");
	check(heif_encoder_set_lossy_quality(enc_thumb, 90), "q");
	check(heif_encoder_set_lossy_quality(enc_main, 90), "q");

	heif_track_options *topt = heif_track_options_alloc();
	heif_track_options_set_timescale(topt, 1000);
	heif_sequence_encoding_options *sopt = heif_sequence_encoding_options_alloc();
	sopt->gop_structure = heif_sequence_gop_structure_lowdelay;

	// the thumbnail track first: it gets track ID 1
	heif_track *thumb = NULL, *main_track = NULL;
	check(heif_context_add_visual_sequence_track(ctx, 32, 32, heif_track_type_image_sequence, topt, sopt, &thumb), "thumb track");
	check(heif_context_add_visual_sequence_track(ctx, 64, 64, heif_track_type_image_sequence, topt, sopt, &main_track), "main track");
	heif_track_add_reference_to_track(thumb, heif_track_reference_type_thumbnails, main_track);

	for (int f = 0; f < frames; f++) {
		heif_image *small = frame(32, 32, f, 2), *big = frame(64, 64, f, 1);
		heif_image_set_duration(small, 250);
		heif_image_set_duration(big, 250);
		check(heif_track_encode_sequence_image(thumb, small, enc_thumb, sopt), "encode thumb");
		check(heif_track_encode_sequence_image(main_track, big, enc_main, sopt), "encode main");
		heif_image_release(small);
		heif_image_release(big);
	}
	check(heif_track_encode_end_of_sequence(thumb, enc_thumb), "end thumb");
	check(heif_track_encode_end_of_sequence(main_track, enc_main), "end main");

	if (still) {
		heif_image *cover = frame(64, 64, 0, 1);
		heif_encoder *enc = NULL;
		check(heif_context_get_encoder_for_format(ctx, heif_compression_HEVC, &enc), "encoder");
		check(heif_encoder_set_lossy_quality(enc, 90), "q");
		check(heif_context_encode_image(ctx, cover, enc, NULL, NULL), "still");
		heif_encoder_release(enc);
		heif_image_release(cover);
	}
	check(heif_context_write_to_file(ctx, argv[1]), "write");
	printf("%s: thumbnail track ID %u, main track ID %u%s\n", argv[1], heif_track_get_id(thumb), heif_track_get_id(main_track), still ? ", plus a still image" : "");
	heif_track_release(thumb);
	heif_track_release(main_track);
	heif_encoder_release(enc_thumb);
	heif_encoder_release(enc_main);
	heif_sequence_encoding_options_release(sopt);
	heif_track_options_release(topt);
	heif_context_free(ctx);
	return 0;
}
