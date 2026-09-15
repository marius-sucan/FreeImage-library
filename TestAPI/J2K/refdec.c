/*
 * refdec - decode a JPEG 2000 file with the reference libopenjp2 2.5.4 and
 * write every component as planar samples, without any colour conversion,
 * in the convention FreeImage's J2KHelper uses (unsigned; signed components
 * offset by 2^(prec-1); 1 byte for prec <= 8, else 2 bytes little-endian).
 *
 *   refdec [-l] [-t N] in.j2k out.comps
 *     -l    lenient: opj_decoder_set_strict_mode(FALSE)
 *     -t N  opj_codec_set_threads(N)
 *
 * out.comps: line "OPJCOMPS <n>\n", then per component a line
 * "<w> <h> <prec> <sgnd> <dx> <dy>\n" followed by the samples.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "openjpeg.h"

static void quiet(const char *msg, void *d) { (void)msg; (void)d; }
static void loud(const char *msg, void *d) { (void)d; fprintf(stderr, "%s", msg); }

int main(int argc, char **argv) {
    int lenient = 0, threads = 0, i;
    const char *in, *out;
    opj_dparameters_t params;
    opj_codec_t *codec;
    opj_stream_t *stream;
    opj_image_t *image = NULL;
    OPJ_CODEC_FORMAT fmt;
    FILE *f;
    unsigned char sig[12];

    for (i = 1; i < argc && argv[i][0] == '-'; i++) {
        if (!strcmp(argv[i], "-l")) lenient = 1;
        else if (!strcmp(argv[i], "-t") && i + 1 < argc) threads = atoi(argv[++i]);
    }
    if (argc - i != 2) { fprintf(stderr, "usage: refdec [-l] [-t N] in out\n"); return 2; }
    in = argv[i]; out = argv[i + 1];

    f = fopen(in, "rb");
    if (!f || fread(sig, 1, 12, f) < 2) { fprintf(stderr, "cannot read %s\n", in); return 2; }
    fclose(f);
    fmt = (sig[0] == 0xFF && sig[1] == 0x4F) ? OPJ_CODEC_J2K : OPJ_CODEC_JP2;

    opj_set_default_decoder_parameters(&params);
    codec = opj_create_decompress(fmt);
    opj_set_info_handler(codec, quiet, NULL);
    opj_set_warning_handler(codec, quiet, NULL);
    opj_set_error_handler(codec, loud, NULL);
    if (!opj_setup_decoder(codec, &params)) return 3;
    if (lenient) opj_decoder_set_strict_mode(codec, OPJ_FALSE);
    if (threads > 0) opj_codec_set_threads(codec, threads);
    stream = opj_stream_create_default_file_stream(in, OPJ_TRUE);
    if (!stream) return 3;
    if (!opj_read_header(stream, codec, &image)) { fprintf(stderr, "header failed\n"); return 4; }
    if (!opj_decode(codec, stream, image) || !opj_end_decompress(codec, stream)) {
        fprintf(stderr, "decode failed\n");
        return 5;
    }
    opj_stream_destroy(stream);
    opj_destroy_codec(codec);

    f = fopen(out, "wb");
    if (!f) return 2;
    fprintf(f, "OPJCOMPS %u\n", image->numcomps);
    for (i = 0; i < (int)image->numcomps; i++) {
        opj_image_comp_t *c = &image->comps[i];
        size_t n = (size_t)c->w * c->h, k;
        int off = c->sgnd ? 1 << (c->prec - 1) : 0;
        fprintf(f, "%u %u %u %u %u %u\n", c->w, c->h, c->prec, c->sgnd, c->dx, c->dy);
        if (!c->data) { fprintf(stderr, "component %d has no data\n", i); return 6; }
        if (c->prec <= 8) {
            for (k = 0; k < n; k++) { unsigned char v = (unsigned char)(c->data[k] + off); fwrite(&v, 1, 1, f); }
        } else {
            for (k = 0; k < n; k++) { unsigned v = (unsigned)(c->data[k] + off); unsigned char b[2] = { (unsigned char)v, (unsigned char)(v >> 8) }; fwrite(b, 1, 2, f); }
        }
    }
    fclose(f);
    opj_image_destroy(image);
    return 0;
}
