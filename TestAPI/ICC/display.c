/* FIF_LOAD_DISPLAY_ICC: every loader returns the image converted to the display's profile */
/* ./display -x11 tests the X11 detection against xcbstub.c (make x11) */
#include "common.h"
#include "../../Source/LibTIFF4/tiffio.h"
#include <pthread.h>
#include <stdatomic.h>
#include <unistd.h>
#ifndef _WIN32
#include <dlfcn.h>
#endif

#define FLAGS_0x101 (FICMS_INTENT_RELATIVE_COLORIMETRIC | FICMS_BLACKPOINT_COMPENSATION)
#define W 160
#define H 120

static const char *scratch(const char *name) {
    static char buf[8][512];
    static int next = 0;
    const char *dir = getenv("ICC_TEST_TMP");
    char *p = buf[next++ & 7];
    snprintf(p, 512, "%s/%s", (dir && *dir) ? dir : ".", name);
    return p;
}

static int write_file(const char *path, const void *data, size_t size) {
    FILE *f = fopen(path, "wb");
    int ok = f && fwrite(data, 1, size, f) == size;
    if (f) fclose(f);
    return ok;
}

static Bytes copy_bytes(const void *data, DWORD size) {
    Bytes b;
    b.data = (BYTE *)malloc(size ? size : 1);
    memcpy(b.data, data, size);
    b.size = size;
    return b;
}

static Bytes current_display(void) {
    Bytes b;
    b.size = FreeImage_GetDisplayICCProfile(NULL, 0);
    b.data = (BYTE *)malloc(b.size ? b.size : 1);
    FreeImage_GetDisplayICCProfile(b.data, b.size);
    return b;
}

static int same_bytes(const Bytes *a, const void *data, DWORD size) {
    return a->size == size && a->data && data && !memcmp(a->data, data, size);
}

static Bytes attached(FIBITMAP *dib) {
    FIICCPROFILE *icc = FreeImage_GetICCProfile(dib);
    return copy_bytes(icc->data, icc->size);
}

static unsigned long long thumb_digest(FIBITMAP *dib) {
    FIBITMAP *t = FreeImage_GetThumbnail(dib);
    return t ? digest(t) : 0;
}

/* pixels only */
static unsigned long long pixel_digest(FIBITMAP *dib) {
    unsigned long long h = 1469598103934665603ULL;
    unsigned y;
    for (y = 0; y < FreeImage_GetHeight(dib); y++) h = fnv(h, FreeImage_GetScanLine(dib, y), FreeImage_GetLine(dib));
    return h;
}

/* what a header-only load shows */
static unsigned long long head_digest(FIBITMAP *dib) {
    unsigned long long h = 1469598103934665603ULL;
    FIICCPROFILE *icc = FreeImage_GetICCProfile(dib);
    unsigned v[6];
    int model;
    v[0] = FreeImage_GetWidth(dib); v[1] = FreeImage_GetHeight(dib); v[2] = FreeImage_GetBPP(dib);
    v[3] = FreeImage_GetImageType(dib); v[4] = FreeImage_GetColorType(dib); v[5] = FreeImage_HasPixels(dib);
    h = fnv(h, v, sizeof(v));
    if (FreeImage_GetPalette(dib)) h = fnv(h, FreeImage_GetPalette(dib), FreeImage_GetColorsUsed(dib) * sizeof(RGBQUAD));
    if (icc->data) h = fnv(h, icc->data, icc->size);
    h = fnv(h, &icc->flags, sizeof(icc->flags));
    for (model = FIMD_COMMENTS; model <= FIMD_EXIF_RAW; model++) {
        unsigned n = FreeImage_GetMetadataCount((FREE_IMAGE_MDMODEL)model, dib);
        h = fnv(h, &n, sizeof(n));
    }
    if (FreeImage_GetThumbnail(dib)) {
        unsigned long long t = digest(FreeImage_GetThumbnail(dib));
        h = fnv(h, &t, sizeof(t));
    }
    return h;
}

/* ------------------------------------------------------------------ images and files */

static FIBITMAP *noise(int bpp) {
    FIBITMAP *dib = FreeImage_Allocate(W, H, bpp, FI_RGBA_RED_MASK, FI_RGBA_GREEN_MASK, FI_RGBA_BLUE_MASK);
    unsigned y, x;
    for (y = 0; y < H; y++) {
        BYTE *p = FreeImage_GetScanLine(dib, y);
        for (x = 0; x < FreeImage_GetLine(dib); x++) p[x] = (BYTE)((rnd() & 63) + ((x * 3 + y * 2) & 191));
    }
    return dib;
}

static Bytes save_memory(FREE_IMAGE_FORMAT fif, FIBITMAP *dib, int flags) {
    Bytes b = { NULL, 0 };
    FIMEMORY *m = FreeImage_OpenMemory(NULL, 0);
    BYTE *data;
    DWORD size;
    if (FreeImage_SaveToMemory(fif, dib, m, flags) && FreeImage_AcquireMemory(m, &data, &size)) b = copy_bytes(data, size);
    FreeImage_CloseMemory(m);
    return b;
}

static void put16(BYTE *p, unsigned v) { p[0] = (BYTE)(v >> 8); p[1] = (BYTE)v; }
static void put32(BYTE *p, DWORD v) { p[0] = (BYTE)(v >> 24); p[1] = (BYTE)(v >> 16); p[2] = (BYTE)(v >> 8); p[3] = (BYTE)v; }

/* a raw 8-bit 32x24 PSD of the colour mode and channels */
static Bytes make_psd(int mode, int channels) {
    Bytes b;
    BYTE *p;
    int i;
    b.size = 26 + 4 + 4 + 4 + 2 + channels * 32 * 24;
    b.data = p = (BYTE *)calloc(1, b.size);
    memcpy(p, "8BPS", 4); put16(p + 4, 1); put16(p + 12, channels); put32(p + 14, 24); put32(p + 18, 32); put16(p + 22, 8); put16(p + 24, mode);
    p += 26 + 4 + 4 + 4 + 2;
    for (i = 0; i < channels * 32 * 24; i++) p[i] = (BYTE)(i * 13 + 1);
    return b;
}

/* an icon holding one PNG */
static int write_ico(const char *path, const Bytes *png) {
    BYTE head[22] = { 0, 0, 1, 0, 1, 0, 32, 32, 0, 0, 1, 0, 32, 0 };
    FILE *f = fopen(path, "wb");
    int ok;
    head[14] = (BYTE)png->size; head[15] = (BYTE)(png->size >> 8); head[16] = (BYTE)(png->size >> 16); head[17] = (BYTE)(png->size >> 24);
    head[18] = 22;
    ok = f && fwrite(head, 1, 22, f) == 22 && fwrite(png->data, 1, png->size, f) == png->size;
    if (f) fclose(f);
    return ok;
}

/* an RGB display profile Little CMS opens but cannot convert with */
static Bytes make_unlinkable_rgb(void) {
    cmsHPROFILE h = cmsCreateProfilePlaceholder(NULL);
    cmsMLU *mlu = cmsMLUalloc(NULL, 1);
    Bytes b;
    cmsSetProfileVersion(h, 4.3);
    cmsSetDeviceClass(h, cmsSigDisplayClass);
    cmsSetColorSpace(h, cmsSigRgbData);
    cmsSetPCS(h, cmsSigXYZData);
    cmsMLUsetASCII(mlu, "en", "US", "No tables");
    cmsWriteTag(h, cmsSigProfileDescriptionTag, mlu);
    cmsWriteTag(h, cmsSigMediaWhitePointTag, cmsD50_XYZ());
    b = save_profile(h);
    cmsMLUfree(mlu);
    cmsCloseProfile(h);
    return b;
}

static tmsize_t t_read(thandle_t h, void *b, tmsize_t n) { return (tmsize_t)fread(b, 1, (size_t)n, (FILE *)h); }
static tmsize_t t_write(thandle_t h, void *b, tmsize_t n) { return (tmsize_t)fwrite(b, 1, (size_t)n, (FILE *)h); }
static toff_t t_seek(thandle_t h, toff_t o, int w) { fseek((FILE *)h, (long)o, w); return (toff_t)ftell((FILE *)h); }
static int t_close(thandle_t h) { return fclose((FILE *)h); }
static toff_t t_size(thandle_t h) { long p = ftell((FILE *)h), s; fseek((FILE *)h, 0, SEEK_END); s = ftell((FILE *)h); fseek((FILE *)h, p, SEEK_SET); return (toff_t)s; }
static int t_map(thandle_t h, void **b, toff_t *s) { (void)h; (void)b; (void)s; return 0; }
static void t_unmap(thandle_t h, void *b, toff_t s) { (void)h; (void)b; (void)s; }

/* CMYK with the press profile, then RGB with Adobe RGB; FreeImage's page cache would store the CMYK page as RGB */
static int write_two_page_tiff(const char *path, const Bytes *cmyk_icc, const Bytes *rgb_icc) {
    static BYTE row[W * 4];
    FILE *f = fopen(path, "w+b");
    TIFF *t = f ? TIFFClientOpen(path, "w", (thandle_t)f, t_read, t_write, t_seek, t_close, t_size, t_map, t_unmap) : NULL;
    int page, y, x;
    if (!t) { if (f) fclose(f); return 0; }
    for (page = 0; page < 2; page++) {
        const int spp = page ? 3 : 4;
        const Bytes *icc = page ? rgb_icc : cmyk_icc;
        TIFFSetField(t, TIFFTAG_SUBFILETYPE, FILETYPE_PAGE); TIFFSetField(t, TIFFTAG_PAGENUMBER, page, 2);
        TIFFSetField(t, TIFFTAG_IMAGEWIDTH, W); TIFFSetField(t, TIFFTAG_IMAGELENGTH, H);
        TIFFSetField(t, TIFFTAG_BITSPERSAMPLE, 8); TIFFSetField(t, TIFFTAG_SAMPLESPERPIXEL, spp);
        TIFFSetField(t, TIFFTAG_PHOTOMETRIC, page ? PHOTOMETRIC_RGB : PHOTOMETRIC_SEPARATED);
        if (!page) TIFFSetField(t, TIFFTAG_INKSET, INKSET_CMYK);
        TIFFSetField(t, TIFFTAG_PLANARCONFIG, PLANARCONFIG_CONTIG); TIFFSetField(t, TIFFTAG_ROWSPERSTRIP, 16);
        TIFFSetField(t, TIFFTAG_ICCPROFILE, (uint32_t)icc->size, icc->data);
        for (y = 0; y < H; y++) {
            for (x = 0; x < W * spp; x++) row[x] = (BYTE)(rnd() & 255);
            TIFFWriteScanline(t, row, y, 0);
        }
        TIFFWriteDirectory(t);
    }
    TIFFClose(t);
    return 1;
}

static int save_pages(FREE_IMAGE_FORMAT fif, const char *path, FIBITMAP **pages, int count) {
    FIMULTIBITMAP *mb = FreeImage_OpenMultiBitmap(fif, path, TRUE, FALSE, TRUE, 0);
    int i;
    if (!mb) return 0;
    for (i = 0; i < count; i++) FreeImage_AppendPage(mb, pages[i]);
    return FreeImage_CloseMultiBitmap(mb, 0);
}

typedef struct {
    const char *name;
    FREE_IMAGE_FORMAT fif;
    int flags;
    int pages;
    const char *path;
} Fixture;

#define MAX_FIXTURES 40
static Fixture fixtures[MAX_FIXTURES];
static int fixture_count = 0;

static void add(const char *name, FREE_IMAGE_FORMAT fif, int flags, int pages, const char *file) {
    Fixture *f = &fixtures[fixture_count++];
    f->name = name; f->fif = fif; f->flags = flags; f->pages = pages; f->path = strdup(file);
}

static void save_fixture(const char *name, FREE_IMAGE_FORMAT fif, FIBITMAP *dib, int save_flags, int load_flags, const char *file) {
    const char *path = scratch(file);
    if (!FreeImage_Save(fif, dib, path, save_flags)) { fail("%s: fixture not saved", name); return; }
    add(name, fif, load_flags, 1, path);
}

static Bytes press, grey18, unlinkable;

static void make_fixtures(void) {
    Bytes adobe = builtin(FICMS_PROFILE_ADOBE_RGB), prophoto = builtin(FICMS_PROFILE_PROPHOTO_RGB);
    FIBITMAP *rgb = noise(24), *rgba = noise(32), *thumb = FreeImage_Rescale(rgb, 40, 30, FILTER_BILINEAR);
    FIBITMAP *adobe24 = FreeImage_ConvertToICCProfile(rgb, adobe.data, adobe.size, 0);
    FIBITMAP *cmyk = FreeImage_ConvertToCMYK(rgb, press.data, press.size, 0);
    FIBITMAP *rgb16 = FreeImage_ConvertToRGB16(rgb), *cmyk16 = FreeImage_ConvertToCMYK(rgb16, press.data, press.size, 0);
    FIBITMAP *adobe16 = FreeImage_ConvertToICCProfile(rgb16, adobe.data, adobe.size, 0);
    FIBITMAP *grey = FreeImage_ConvertToGreyscale(rgb), *g18 = FreeImage_ConvertToICCProfile(grey, grey18.data, grey18.size, 0);
    FIBITMAP *u16 = FreeImage_ConvertToUINT16(grey), *u16g18 = FreeImage_ConvertToICCProfile(u16, grey18.data, grey18.size, 0);
    FIBITMAP *pro = FreeImage_ConvertToICCProfile(rgba, prophoto.data, prophoto.size, 0);
    FIBITMAP *pal = FreeImage_ColorQuantize(rgb, FIQ_WUQUANT), *bw = FreeImage_Threshold(grey, 128);
    FIBITMAP *rgbf = FreeImage_ConvertToRGBF(rgb), *c555 = FreeImage_ConvertTo16Bits555(rgb), *c565 = FreeImage_ConvertTo16Bits565(rgb);
    FIBITMAP *pal4 = FreeImage_ConvertTo4Bits(rgb), *dcmyk = FreeImage_Clone(cmyk), *broken = FreeImage_Clone(rgb);
    FIBITMAP *pages[2];
    BYTE trns[256];
    Bytes b;
    int i;

    FreeImage_DestroyICCProfile(dcmyk);
    FreeImage_CreateICCProfile(broken, unlinkable.data, unlinkable.size);
    for (i = 0; i < 256; i++) trns[i] = (BYTE)(i * 7);
    FreeImage_SetTransparencyTable(pal, trns, 256);

    FreeImage_SetThumbnail(rgb, thumb);
    save_fixture("RGB JPEG + thumbnail", FIF_JPEG, rgb, JPEG_QUALITYSUPERB, 0, "dsp_rgb.jpg");
    FreeImage_SetThumbnail(rgb, NULL);
    save_fixture("Adobe RGB JPEG", FIF_JPEG, adobe24, JPEG_QUALITYSUPERB, 0, "dsp_adobe.jpg");
    save_fixture("CMYK JPEG", FIF_JPEG, cmyk, JPEG_QUALITYSUPERB, 0, "dsp_cmyk.jpg");
    save_fixture("untagged CMYK JPEG, rotated", FIF_JPEG, dcmyk, JPEG_QUALITYSUPERB, JPEG_EXIFROTATE, "dsp_dcmyk.jpg");
    save_fixture("grey 1.8 JPEG", FIF_JPEG, g18, JPEG_QUALITYSUPERB, 0, "dsp_grey.jpg");
    save_fixture("Adobe RGB JPEG as grey", FIF_JPEG, adobe24, JPEG_QUALITYSUPERB, JPEG_GREYSCALE, "dsp_adobe_g.jpg");
    save_fixture("CMYK TIFF", FIF_TIFF, cmyk, TIFF_DEFAULT, 0, "dsp_cmyk.tif");
    save_fixture("16-bit CMYK TIFF", FIF_TIFF, cmyk16, TIFF_DEFAULT, 0, "dsp_cmyk16.tif");
    save_fixture("Adobe RGB16 TIFF", FIF_TIFF, adobe16, TIFF_DEFAULT, 0, "dsp_rgb16.tif");
    save_fixture("grey 1.8 UINT16 TIFF", FIF_TIFF, u16g18, TIFF_DEFAULT, 0, "dsp_u16.tif");
    save_fixture("ProPhoto RGBA TIFF", FIF_TIFF, pro, TIFF_DEFAULT, 0, "dsp_rgba.tif");
    save_fixture("float TIFF", FIF_TIFF, rgbf, TIFF_DEFAULT, 0, "dsp_float.tif");
    save_fixture("unlinkable profile TIFF", FIF_TIFF, broken, TIFF_DEFAULT, 0, "dsp_broken.tif");
    if (write_two_page_tiff(scratch("dsp_2p.tif"), &press, &adobe)) add("2-page TIFF", FIF_TIFF, 0, 2, scratch("dsp_2p.tif"));
    else fail("2-page TIFF: fixture not saved");
    save_fixture("palette PNG with tRNS", FIF_PNG, pal, PNG_DEFAULT, 0, "dsp_pal.png");
    save_fixture("grey 1.8 PNG", FIF_PNG, g18, PNG_DEFAULT, 0, "dsp_grey.png");
    save_fixture("RGB16 PNG", FIF_PNG, rgb16, PNG_DEFAULT, 0, "dsp_rgb16.png");
    save_fixture("1-bit PNG", FIF_PNG, bw, PNG_DEFAULT, 0, "dsp_bw.png");
    save_fixture("555 BMP", FIF_BMP, c555, BMP_DEFAULT, 0, "dsp_555.bmp");
    save_fixture("565 BMP", FIF_BMP, c565, BMP_DEFAULT, 0, "dsp_565.bmp");
    save_fixture("4-bit BMP", FIF_BMP, pal4, BMP_DEFAULT, 0, "dsp_4.bmp");
    FreeImage_SetThumbnail(cmyk, thumb);
    save_fixture("CMYK PSD + thumbnail", FIF_PSD, cmyk, PSD_DEFAULT, 0, "dsp_cmyk.psd");
    FreeImage_SetThumbnail(cmyk, NULL);
    save_fixture("Adobe RGB WebP", FIF_WEBP, adobe24, WEBP_LOSSLESS, 0, "dsp_adobe.webp");
    save_fixture("float PFM", FIF_PFM, rgbf, PFM_DEFAULT, 0, "dsp_float.pfm");
    b = make_psd(9, 3);
    if (write_file(scratch("dsp_lab.psd"), b.data, b.size)) add("Lab PSD", FIF_PSD, 0, 1, scratch("dsp_lab.psd"));
    free(b.data);

    /* animations: the frames are loaded through a second plugin */
    pages[0] = FreeImage_ColorQuantize(rgb, FIQ_WUQUANT);
    pages[1] = FreeImage_ColorQuantize(adobe24, FIQ_NNQUANT);
    if (save_pages(FIF_GIF, scratch("dsp_anim.gif"), pages, 2)) {
        add("GIF frames", FIF_GIF, 0, 2, scratch("dsp_anim.gif"));
        add("GIF playback", FIF_GIF, GIF_PLAYBACK, 2, scratch("dsp_anim.gif"));
    } else fail("GIF: fixture not saved");
    FreeImage_Unload(pages[0]); FreeImage_Unload(pages[1]);
    {
        FIBITMAP *second = noise(24);
        pages[0] = adobe24; pages[1] = FreeImage_ConvertToICCProfile(second, adobe.data, adobe.size, 0);
        FreeImage_Unload(second);
    }
    if (save_pages(FIF_APNG, scratch("dsp_anim.apng"), pages, 2)) {
        add("APNG frames", FIF_APNG, 0, 2, scratch("dsp_anim.apng"));
        add("APNG playback", FIF_APNG, APNG_PLAYBACK, 2, scratch("dsp_anim.apng"));
    } else fail("APNG: fixture not saved");
    if (save_pages(FIF_MNG, scratch("dsp_anim.mng"), pages, 2)) {
        add("MNG frames", FIF_MNG, 0, 2, scratch("dsp_anim.mng"));
        add("MNG playback", FIF_MNG, MNG_PLAYBACK, 2, scratch("dsp_anim.mng"));
    } else fail("MNG: fixture not saved");
    FreeImage_Unload(pages[1]);
    {
        FIBITMAP *small = FreeImage_Rescale(pro, 32, 32, FILTER_BOX);
        FreeImage_CreateICCProfile(small, prophoto.data, prophoto.size);
        b = save_memory(FIF_PNG, small, PNG_DEFAULT);
        if (b.size && write_ico(scratch("dsp_png.ico"), &b)) add("ICO holding a PNG", FIF_ICO, 0, 1, scratch("dsp_png.ico"));
        else fail("ICO: fixture not saved");
        free(b.data);
        FreeImage_Unload(small);
    }
    /* RAW: the preview, 8-bit, 16-bit linear, the sensor data */
    if (access("../RAW/data/fi_raw_bggr.dng", R_OK) == 0) {
        add("DNG", FIF_RAW, RAW_DEFAULT, 1, "../RAW/data/fi_raw_bggr.dng");
        add("DNG, RAW_DISPLAY", FIF_RAW, RAW_DISPLAY, 1, "../RAW/data/fi_raw_bggr.dng");
        add("DNG, RAW_PREVIEW", FIF_RAW, RAW_PREVIEW, 1, "../RAW/data/fi_raw_bggr.dng");
        add("DNG, RAW_UNPROCESSED", FIF_RAW, RAW_UNPROCESSED, 1, "../RAW/data/fi_raw_bggr.dng");
    } else {
        printf("  (no ../RAW/data/fi_raw_bggr.dng: RAW skipped)\n");
    }

    FreeImage_Unload(rgb); FreeImage_Unload(rgba); FreeImage_Unload(thumb); FreeImage_Unload(adobe24); FreeImage_Unload(cmyk);
    FreeImage_Unload(rgb16); FreeImage_Unload(cmyk16); FreeImage_Unload(adobe16); FreeImage_Unload(grey); FreeImage_Unload(g18);
    FreeImage_Unload(u16); FreeImage_Unload(u16g18); FreeImage_Unload(pro); FreeImage_Unload(pal); FreeImage_Unload(bw);
    FreeImage_Unload(rgbf); FreeImage_Unload(c555); FreeImage_Unload(c565); FreeImage_Unload(pal4); FreeImage_Unload(dcmyk);
    FreeImage_Unload(broken);
}

/* ------------------------------------------------------------------ loaders */

static unsigned DLL_CALLCONV io_read(void *b, unsigned s, unsigned c, fi_handle h) { return (unsigned)fread(b, s, c, (FILE *)h); }
static unsigned DLL_CALLCONV io_write(void *b, unsigned s, unsigned c, fi_handle h) { return (unsigned)fwrite(b, s, c, (FILE *)h); }
#ifdef _WIN32
static int DLL_CALLCONV io_seek(fi_handle h, INT64 o, int w) { return _fseeki64((FILE *)h, o, w); }
static INT64 DLL_CALLCONV io_tell(fi_handle h) { return _ftelli64((FILE *)h); }
#else
static int DLL_CALLCONV io_seek(fi_handle h, INT64 o, int w) { return fseeko((FILE *)h, (off_t)o, w); }
static INT64 DLL_CALLCONV io_tell(fi_handle h) { return (INT64)ftello((FILE *)h); }
#endif

#ifdef _WIN32
enum { BY_FILE, BY_MEMORY, BY_HANDLE, BY_WFILE, BY_PAGE_FILE, BY_PAGE_MEMORY, BY_PAGE_HANDLE, BY_PAGE_WFILE, LOADERS };
static const char *LOADER_NAMES[LOADERS] = { "Load", "LoadFromMemory", "LoadFromHandle", "LoadU", "OpenMultiBitmap", "LoadMultiBitmapFromMemory", "OpenMultiBitmapFromHandle", "OpenMultiBitmapU" };
#else
enum { BY_FILE, BY_MEMORY, BY_HANDLE, BY_PAGE_FILE, BY_PAGE_MEMORY, BY_PAGE_HANDLE, LOADERS };
static const char *LOADER_NAMES[LOADERS] = { "Load", "LoadFromMemory", "LoadFromHandle", "OpenMultiBitmap", "LoadMultiBitmapFromMemory", "OpenMultiBitmapFromHandle" };
#endif

static FIBITMAP *locked_copy(FIMULTIBITMAP *mb, int page) {
    FIBITMAP *p, *copy = NULL;
    if (!mb) return NULL;
    p = FreeImage_LockPage(mb, page);
    if (p) {
        copy = FreeImage_Clone(p);
        FreeImage_UnlockPage(mb, p, FALSE);
    }
    FreeImage_CloseMultiBitmap(mb, 0);
    return copy;
}

static FIBITMAP *load(const Fixture *f, int page, int flags, int how) {
    FreeImageIO io = { io_read, io_write, io_seek, io_tell };
    FIBITMAP *dib = NULL;
    FIMEMORY *m = NULL;
    FILE *h = NULL;
    Bytes b = { NULL, 0 };
    if (how == BY_MEMORY || how == BY_PAGE_MEMORY) {
        b = read_file(f->path);
        m = FreeImage_OpenMemory(b.data, b.size);
    }
    if (how == BY_HANDLE || how == BY_PAGE_HANDLE) h = fopen(f->path, "rb");
    switch (how) {
        case BY_FILE: dib = FreeImage_Load(f->fif, f->path, flags); break;
        case BY_MEMORY: dib = FreeImage_LoadFromMemory(f->fif, m, flags); break;
        case BY_HANDLE: dib = h ? FreeImage_LoadFromHandle(f->fif, &io, (fi_handle)h, flags) : NULL; break;
        case BY_PAGE_FILE: dib = locked_copy(FreeImage_OpenMultiBitmap(f->fif, f->path, FALSE, TRUE, TRUE, flags), page); break;
        case BY_PAGE_MEMORY: dib = locked_copy(FreeImage_LoadMultiBitmapFromMemory(f->fif, m, flags), page); break;
        case BY_PAGE_HANDLE: dib = h ? locked_copy(FreeImage_OpenMultiBitmapFromHandle(f->fif, &io, (fi_handle)h, flags), page) : NULL; break;
#ifdef _WIN32
        case BY_WFILE:
        case BY_PAGE_WFILE:
        {
            wchar_t wide[512];
            size_t i;
            for (i = 0; f->path[i] && i < 511; i++) wide[i] = (wchar_t)(unsigned char)f->path[i];
            wide[i] = 0;
            dib = (how == BY_WFILE) ? FreeImage_LoadU(f->fif, wide, flags) : locked_copy(FreeImage_OpenMultiBitmapU(f->fif, wide, FALSE, TRUE, TRUE, flags), page);
            break;
        }
#endif
    }
    if (m) FreeImage_CloseMemory(m);
    if (h) fclose(h);
    free(b.data);
    return dib;
}

/* ------------------------------------------------------------------ what the flag has to give */

typedef struct {
    const char *name;
    Bytes rgb;      /* the getter's profile */
    Bytes grey;     /* the grey profile a grey image gets */
    int flags;
} Display;

static int cmyk_flag(FREE_IMAGE_FORMAT fif) {
    return (fif == FIF_JPEG) ? JPEG_CMYK : (fif == FIF_TIFF) ? TIFF_CMYK : (fif == FIF_PSD) ? PSD_CMYK : 0;
}

static int base_flags(const Fixture *f) {
    int flags = f->flags | cmyk_flag(f->fif);
    return (f->fif == FIF_PSD) ? (flags & ~PSD_LAB) : flags;
}

static int is_managed(FIBITMAP *dib) {
    FREE_IMAGE_TYPE t = FreeImage_GetImageType(dib);
    return t == FIT_BITMAP || t == FIT_UINT16 || t == FIT_RGB16 || t == FIT_RGBA16;
}

static int is_cmyk(FIBITMAP *dib) {
    FREE_IMAGE_TYPE t = FreeImage_GetImageType(dib);
    return (FreeImage_GetICCProfile(dib)->flags & FIICC_COLOR_IS_CMYK) && ((t == FIT_BITMAP && FreeImage_GetBPP(dib) == 32) || t == FIT_RGBA16);
}

static int is_grey(FIBITMAP *dib) {
    RGBQUAD *pal = FreeImage_GetPalette(dib);
    unsigned i, n = FreeImage_GetColorsUsed(dib);
    if (FreeImage_GetImageType(dib) == FIT_UINT16) return 1;
    if (FreeImage_GetImageType(dib) != FIT_BITMAP || FreeImage_GetBPP(dib) > 8 || !pal) return 0;
    for (i = 0; i < n; i++) if (pal[i].rgbRed != pal[i].rgbGreen || pal[i].rgbRed != pal[i].rgbBlue) return 0;
    return 1;
}

/* the public API's version of one image; an embedded profile that cannot be linked is left out the second time */
static FIBITMAP *expect_one(FIBITMAP *src, const Display *d) {
    const Bytes *to;
    int widen, attempt;
    if (!is_managed(src)) return FreeImage_Clone(src);
    to = is_grey(src) ? &d->grey : &d->rgb;
    widen = is_cmyk(src) || (FreeImage_GetImageType(src) == FIT_BITMAP && FreeImage_GetBPP(src) == 16);
    for (attempt = 0; attempt < 2; attempt++) {
        FIBITMAP *img = FreeImage_Clone(src);
        if (attempt) FreeImage_DestroyICCProfile(img);
        if (widen) {
            FIBITMAP *out = FreeImage_ConvertToICCProfile(img, to->data, to->size, d->flags);
            FreeImage_Unload(img);
            if (out) return out;
        } else if (FreeImage_ApplyICCProfile(img, to->data, to->size, d->flags)) {
            return img;
        } else {
            FreeImage_Unload(img);
        }
        if (!FreeImage_GetICCProfile(src)->data) break;
    }
    return widen ? NULL : FreeImage_Clone(src);
}

static FIBITMAP *expected(FIBITMAP *base, const Fixture *f, const Display *d) {
    FIBITMAP *out, *thumb;
    if (f->fif == FIF_RAW && (f->flags & RAW_UNPROCESSED)) return FreeImage_Clone(base);
    out = expect_one(base, d);
    thumb = FreeImage_GetThumbnail(base);
    if (out) {
        FIBITMAP *t = thumb ? expect_one(thumb, d) : NULL;
        FreeImage_SetThumbnail(out, t);
        if (t) FreeImage_Unload(t);
    }
    return out;
}

/* one display: every fixture, page and loader against the public API */
static void check_display(const Display *d) {
    int i, page, how, compared = 0;
    for (i = 0; i < fixture_count; i++) {
        const Fixture *f = &fixtures[i];
        for (page = 0; page < f->pages; page++) {
            FIBITMAP *base = load(f, page, base_flags(f), page ? BY_PAGE_FILE : BY_FILE), *want;
            if (!base) { fail("%s, %s page %d: no base image", d->name, f->name, page); continue; }
            want = expected(base, f, d);
            if (!want) { fail("%s, %s page %d: no expected image", d->name, f->name, page); FreeImage_Unload(base); continue; }
            for (how = page ? BY_PAGE_FILE : BY_FILE; how < LOADERS; how++) {
                FIBITMAP *got = load(f, page, f->flags | FIF_LOAD_DISPLAY_ICC, how);
                if (!got) { fail("%s, %s page %d, %s: not loaded", d->name, f->name, page, LOADER_NAMES[how]); continue; }
                CHECK(digest(got) == digest(want), "%s, %s page %d, %s: %u-bit type %d differs from the public API's %u-bit type %d",
                    d->name, f->name, page, LOADER_NAMES[how], FreeImage_GetBPP(got), FreeImage_GetImageType(got), FreeImage_GetBPP(want), FreeImage_GetImageType(want));
                CHECK(thumb_digest(got) == thumb_digest(want), "%s, %s page %d, %s: thumbnail", d->name, f->name, page, LOADER_NAMES[how]);
                compared++;
                FreeImage_Unload(got);
            }
            FreeImage_Unload(want);
            FreeImage_Unload(base);
        }
    }
    printf("%-22s %d loads compared\n", d->name, compared);
}

/* the display's greys (v, v, v) have the luminance the grey profile gives v */
static void check_grey_companion(const Display *d) {
    cmsHPROFILE g = cmsOpenProfileFromMem(d->grey.data, d->grey.size), rgb = cmsOpenProfileFromMem(d->rgb.data, d->rgb.size);
    cmsHPROFILE xyz = cmsCreateXYZProfile();
    cmsHTRANSFORM tg = NULL, td = NULL;
    double worst = 0;
    unsigned v;
    CHECK(g && cmsGetColorSpace(g) == cmsSigGrayData, "%s: the grey profile is not grey", d->name);
    if (g && rgb) {
        tg = cmsCreateTransform(g, TYPE_GRAY_16, xyz, TYPE_XYZ_DBL, INTENT_RELATIVE_COLORIMETRIC, cmsFLAGS_NOOPTIMIZE);
        td = cmsCreateTransform(rgb, TYPE_RGB_16, xyz, TYPE_XYZ_DBL, INTENT_RELATIVE_COLORIMETRIC, cmsFLAGS_NOOPTIMIZE);
    }
    CHECK(tg && td, "%s: grey profile transforms", d->name);
    if (tg && td) {
        for (v = 0; v < 256; v++) {
            WORD grey = (WORD)(v * 257), rgb3[3] = { grey, grey, grey };
            cmsCIEXYZ a, b;
            cmsDoTransform(tg, &grey, &a, 1);
            cmsDoTransform(td, rgb3, &b, 1);
            if (fabs(a.Y - b.Y) > worst) worst = fabs(a.Y - b.Y);
        }
        CHECK(worst < 0.002, "%s: grey luminance off by %.5f", d->name, worst);
    }
    if (tg) cmsDeleteTransform(tg);
    if (td) cmsDeleteTransform(td);
    if (g) cmsCloseProfile(g);
    if (rgb) cmsCloseProfile(rgb);
    cmsCloseProfile(xyz);
}

static const Fixture *find(const char *name) {
    int i;
    for (i = 0; i < fixture_count; i++) if (!strcmp(fixtures[i].name, name)) return &fixtures[i];
    return NULL;
}

/* pins the profile and fills the display: the getter's bytes and the grey a grey image gets */
static int use_display(Display *d, const char *name, const void *profile, DWORD size, int flags) {
    const Fixture *gf = find("grey 1.8 PNG");
    FIBITMAP *g;
    d->name = name;
    d->flags = flags;
    d->rgb.data = d->grey.data = NULL;
    if (!FreeImage_SetDisplayICCProfile(profile, size, flags)) { fail("%s: not accepted", name); return 0; }
    d->rgb = current_display();
    g = gf ? load(gf, 0, FIF_LOAD_DISPLAY_ICC, BY_FILE) : NULL;
    CHECK(g != NULL, "%s: grey fixture", name);
    if (!g) return 0;
    d->grey = attached(g);
    FreeImage_Unload(g);
    return 1;
}

static void free_display(Display *d) {
    free(d->rgb.data);
    free(d->grey.data);
}

/* ------------------------------------------------------------------ the rules */

/* FIF_LOAD_NOPIXELS: as if the flag were not given, for flags -1 too */
static void header_only(void) {
    int i, same = 0;
    for (i = 0; i < fixture_count; i++) {
        const Fixture *f = &fixtures[i];
        int sets[2], k;
        sets[0] = f->flags | FIF_LOAD_NOPIXELS;
        sets[1] = -1;
        for (k = 0; k < 2; k++) {
            FIBITMAP *a = FreeImage_Load(f->fif, f->path, sets[k] & ~FIF_LOAD_DISPLAY_ICC), *b = FreeImage_Load(f->fif, f->path, sets[k] | FIF_LOAD_DISPLAY_ICC);
            if (a || b) {
                CHECK(a && b && head_digest(a) == head_digest(b), "%s, flags %#x: the header changes with FIF_LOAD_DISPLAY_ICC", f->name, sets[k]);
                same++;
            }
            if (a) FreeImage_Unload(a);
            if (b) FreeImage_Unload(b);
        }
        if (f->pages > 1) {
            FIBITMAP *a = load(f, 1, f->flags | FIF_LOAD_NOPIXELS, BY_PAGE_FILE), *b = load(f, 1, f->flags | FIF_LOAD_NOPIXELS | FIF_LOAD_DISPLAY_ICC, BY_PAGE_FILE);
            CHECK(a && b && head_digest(a) == head_digest(b), "%s page 1: the header changes with FIF_LOAD_DISPLAY_ICC", f->name);
            if (a) FreeImage_Unload(a);
            if (b) FreeImage_Unload(b);
        }
    }
    printf("header-only            %d loads unchanged\n", same);
}

/* PSD_LAB gives way; RAW_UNPROCESSED is left alone; floats are untouched; CMYK becomes RGB */
static void flag_rules(void) {
    const Fixture *lab = find("Lab PSD"), *raw = find("DNG, RAW_UNPROCESSED"), *fl = find("float TIFF"), *c;
    FIBITMAP *a, *b;
    if (lab) {
        a = FreeImage_Load(FIF_PSD, lab->path, FIF_LOAD_DISPLAY_ICC | PSD_LAB);
        b = FreeImage_Load(FIF_PSD, lab->path, FIF_LOAD_DISPLAY_ICC);
        CHECK(a && b && digest(a) == digest(b) && FreeImage_GetBPP(a) == 24, "PSD_LAB with the flag");
        if (a) FreeImage_Unload(a);
        if (b) FreeImage_Unload(b);
    }
    if (raw) {
        a = FreeImage_Load(FIF_RAW, raw->path, FIF_LOAD_DISPLAY_ICC | RAW_UNPROCESSED);
        b = FreeImage_Load(FIF_RAW, raw->path, RAW_UNPROCESSED);
        CHECK(a && b && digest(a) == digest(b), "RAW_UNPROCESSED with the flag");
        if (a) FreeImage_Unload(a);
        if (b) FreeImage_Unload(b);
    }
    if (fl) {
        a = FreeImage_Load(FIF_TIFF, fl->path, FIF_LOAD_DISPLAY_ICC);
        b = FreeImage_Load(FIF_TIFF, fl->path, 0);
        CHECK(a && b && digest(a) == digest(b) && FreeImage_GetImageType(a) == FIT_RGBF, "float image with the flag");
        if (a) FreeImage_Unload(a);
        if (b) FreeImage_Unload(b);
    }
    for (c = fixtures; c < fixtures + fixture_count; c++) {
        if (strstr(c->name, "CMYK") && !strstr(c->name, "2-page")) {
            a = FreeImage_Load(c->fif, c->path, c->flags | FIF_LOAD_DISPLAY_ICC);
            CHECK(a && !(FreeImage_GetICCProfile(a)->flags & FIICC_COLOR_IS_CMYK) && FreeImage_GetColorType(a) == FIC_RGB &&
                (FreeImage_GetBPP(a) == 24 || FreeImage_GetImageType(a) == FIT_RGB16), "%s: RGB with the flag", c->name);
            if (a) FreeImage_Unload(a);
        }
    }
}

/* a changed page is saved in the display's colors; the others as the file holds them */
static void multipage_save(void) {
    const Fixture *tif = find("2-page TIFF"), *apng = find("APNG frames");
    if (tif) {
        Bytes src = read_file(tif->path);
        FIMEMORY *in = FreeImage_OpenMemory(src.data, src.size), *out = FreeImage_OpenMemory(NULL, 0);
        FIMULTIBITMAP *doc = FreeImage_LoadMultiBitmapFromMemory(FIF_TIFF, in, FIF_LOAD_DISPLAY_ICC);
        FIBITMAP *p1 = doc ? FreeImage_LockPage(doc, 1) : NULL, *changed = NULL;
        CHECK(p1 != NULL, "2-page TIFF: page 1 locked");
        if (p1) {
            FreeImage_Invert(p1);
            changed = FreeImage_Clone(p1);
            FreeImage_UnlockPage(doc, p1, TRUE);
        }
        CHECK(doc && FreeImage_SaveMultiBitmapToMemory(FIF_TIFF, doc, out, 0), "2-page TIFF: saved");
        if (doc) FreeImage_CloseMultiBitmap(doc, 0);
        FreeImage_CloseMemory(in);
        if (changed) {
            FIBITMAP *page0, *page1, *orig0 = load(tif, 0, TIFF_CMYK, BY_PAGE_FILE);
            FreeImage_SeekMemory(out, 0, SEEK_SET);
            page0 = locked_copy(FreeImage_LoadMultiBitmapFromMemory(FIF_TIFF, out, TIFF_CMYK), 0);
            FreeImage_SeekMemory(out, 0, SEEK_SET);
            page1 = locked_copy(FreeImage_LoadMultiBitmapFromMemory(FIF_TIFF, out, 0), 1);
            CHECK(page0 && orig0 && digest(page0) == digest(orig0) && is_cmyk(page0), "2-page TIFF: the unchanged CMYK page was saved as the file held it");
            CHECK(page1 && digest(page1) == digest(changed), "2-page TIFF: the changed page was saved in the display's colors");
            if (page0) FreeImage_Unload(page0);
            if (orig0) FreeImage_Unload(orig0);
            if (page1) FreeImage_Unload(page1);
            FreeImage_Unload(changed);
        }
        FreeImage_CloseMemory(out);
        free(src.data);

        /* opened header-only, the flag is ignored: the same file as without it */
        {
            Bytes saved[3];
            int k, flags[3] = { FIF_LOAD_NOPIXELS | FIF_LOAD_DISPLAY_ICC, FIF_LOAD_NOPIXELS, 0 };
            src = read_file(tif->path);
            for (k = 0; k < 3; k++) {
                FIMEMORY *m = FreeImage_OpenMemory(src.data, src.size), *o = FreeImage_OpenMemory(NULL, 0);
                FIMULTIBITMAP *d = FreeImage_LoadMultiBitmapFromMemory(FIF_TIFF, m, flags[k]);
                BYTE *data = NULL;
                DWORD size = 0;
                saved[k].data = NULL; saved[k].size = 0;
                if (d && FreeImage_SaveMultiBitmapToMemory(FIF_TIFF, d, o, 0) && FreeImage_AcquireMemory(o, &data, &size)) saved[k] = copy_bytes(data, size);
                if (d) FreeImage_CloseMultiBitmap(d, 0);
                FreeImage_CloseMemory(o);
                FreeImage_CloseMemory(m);
            }
            CHECK(saved[0].size && same_bytes(&saved[0], saved[1].data, saved[1].size) && same_bytes(&saved[0], saved[2].data, saved[2].size),
                "2-page TIFF opened header-only: the flag changed the saved file");
            for (k = 0; k < 3; k++) free(saved[k].data);
            free(src.data);
        }
    }
    if (apng) {
        Bytes src = read_file(apng->path);
        FIMEMORY *in = FreeImage_OpenMemory(src.data, src.size), *out = FreeImage_OpenMemory(NULL, 0);
        FIMULTIBITMAP *doc = FreeImage_LoadMultiBitmapFromMemory(FIF_APNG, in, FIF_LOAD_DISPLAY_ICC);
        FIBITMAP *p0 = doc ? FreeImage_LockPage(doc, 0) : NULL;
        if (p0) {
            FreeImage_Invert(p0);
            FreeImage_UnlockPage(doc, p0, TRUE);
        }
        CHECK(doc && FreeImage_SaveMultiBitmapToMemory(FIF_APNG, doc, out, 0), "APNG: saved");
        if (doc) FreeImage_CloseMultiBitmap(doc, 0);
        FreeImage_CloseMemory(in);
        {
            FIBITMAP *frame1, *orig1 = load(apng, 1, 0, BY_PAGE_FILE);
            FreeImage_SeekMemory(out, 0, SEEK_SET);
            frame1 = locked_copy(FreeImage_LoadMultiBitmapFromMemory(FIF_APNG, out, 0), 1);
            CHECK(frame1 && orig1 && pixel_digest(frame1) == pixel_digest(orig1), "APNG: the unchanged frame was saved in the display's colors");
            if (frame1) FreeImage_Unload(frame1);
            if (orig1) FreeImage_Unload(orig1);
        }
        FreeImage_CloseMemory(out);
        free(src.data);
    }
}

/* an image saved with the display's profile loads unchanged */
static void reload_is_identity(const Display *d) {
    const Fixture *f = find("Adobe RGB JPEG");
    FIBITMAP *a = f ? FreeImage_Load(FIF_JPEG, f->path, FIF_LOAD_DISPLAY_ICC) : NULL, *b = NULL;
    if (a && FreeImage_Save(FIF_TIFF, a, scratch("dsp_again.tif"), TIFF_DEFAULT)) b = FreeImage_Load(FIF_TIFF, scratch("dsp_again.tif"), FIF_LOAD_DISPLAY_ICC);
    CHECK(a && b && digest(a) == digest(b), "%s: loading a display-tagged image again changed it", d->name);
    if (a) FreeImage_Unload(a);
    if (b) FreeImage_Unload(b);
    remove(scratch("dsp_again.tif"));
}

static void setter_getter(void) {
    Bytes srgb = builtin(FICMS_PROFILE_SRGB), adobe = builtin(FICMS_PROFILE_ADOBE_RGB), grey = builtin(FICMS_PROFILE_GRAY);
    Bytes junk, now, t5 = read_file("data/test5.icc"), t3 = read_file("data/test3.icc");
    BYTE canary[64];
    unsigned i;

    junk.size = 1000; junk.data = (BYTE *)malloc(junk.size);
    for (i = 0; i < junk.size; i++) junk.data[i] = (BYTE)rnd();

    /* no display to ask: sRGB */
    CHECK(FreeImage_SetDisplayICCProfile(NULL, 0, FLAGS_0x101), "back to detection");
    now = current_display();
    CHECK(same_bytes(&now, srgb.data, srgb.size), "without a display the profile is sRGB");
    free(now.data);

    CHECK(FreeImage_SetDisplayICCProfile(adobe.data, adobe.size, FLAGS_0x101), "Adobe RGB accepted");
    CHECK(!FreeImage_SetDisplayICCProfile(press.data, press.size, FLAGS_0x101), "a CMYK display profile accepted");
    CHECK(!FreeImage_SetDisplayICCProfile(grey.data, grey.size, FLAGS_0x101), "a grey display profile accepted");
    CHECK(!FreeImage_SetDisplayICCProfile(junk.data, junk.size, FLAGS_0x101), "junk accepted");
    CHECK(!FreeImage_SetDisplayICCProfile(adobe.data, 100, FLAGS_0x101), "a cut profile accepted");
    CHECK(!FreeImage_SetDisplayICCProfile(unlinkable.data, unlinkable.size, FLAGS_0x101), "a profile without tables accepted");
    CHECK(!FreeImage_SetDisplayICCProfile(adobe.data, adobe.size, FICMS_GAMUT_CHECK), "FICMS_GAMUT_CHECK accepted");
    CHECK(!FreeImage_SetDisplayICCProfile(adobe.data, adobe.size, 4), "intent 4 accepted");
    now = current_display();
    CHECK(same_bytes(&now, adobe.data, adobe.size), "a refused profile replaced the last one");
    free(now.data);

    memset(canary, 0xA5, sizeof(canary));
    CHECK(FreeImage_GetDisplayICCProfile(canary, sizeof(canary)) == adobe.size, "the size needed");
    for (i = 0; i < sizeof(canary) && canary[i] == 0xA5; i++) {}
    CHECK(i == sizeof(canary), "a small buffer was written");
    CHECK(FreeImage_GetDisplayICCProfile(NULL, 0) == adobe.size, "the size without a buffer");

    /* profiles that are sRGB in all but name become FreeImage's sRGB */
    if (t5.size) {
        CHECK(FreeImage_SetDisplayICCProfile(t5.data, t5.size, FLAGS_0x101), "test5.icc accepted");
        now = current_display();
        CHECK(same_bytes(&now, srgb.data, srgb.size), "test5.icc (sRGB-like) did not become sRGB");
        free(now.data);
    }
    if (t3.size) {
        CHECK(FreeImage_SetDisplayICCProfile(t3.data, t3.size, FLAGS_0x101), "test3.icc accepted");
        now = current_display();
        CHECK(same_bytes(&now, t3.data, t3.size), "test3.icc (LUT) was replaced");
        free(now.data);
    }
    {
        const char *twins[2] = { "/usr/share/color/icc/colord/sRGB.icc", "/usr/share/color/icc/ghostscript/srgb.icc" };
        for (i = 0; i < 2; i++) {
            Bytes b = read_file(twins[i]);
            if (!b.size) continue;
            CHECK(FreeImage_SetDisplayICCProfile(b.data, b.size, FLAGS_0x101), "%s accepted", twins[i]);
            now = current_display();
            CHECK(same_bytes(&now, srgb.data, srgb.size), "%s did not become sRGB", twins[i]);
            free(now.data);
            free(b.data);
        }
        {
            Bytes b = read_file("/usr/share/color/icc/colord/Rec709.icc");
            if (b.size) {
                CHECK(FreeImage_SetDisplayICCProfile(b.data, b.size, FLAGS_0x101), "Rec709.icc accepted");
                now = current_display();
                CHECK(same_bytes(&now, b.data, b.size), "Rec709.icc became sRGB");
                free(now.data);
                free(b.data);
            }
        }
    }
    free(junk.data);
    free(t5.data);
    free(t3.data);
}

/* the setter's intent is what the loads use */
static void intents(void) {
    Bytes p3 = builtin(FICMS_PROFILE_DISPLAY_P3);
    const Fixture *f = find("CMYK JPEG");
    Display d;
    if (!f || !use_display(&d, "P3, perceptual", p3.data, p3.size, FICMS_INTENT_PERCEPTUAL)) return;
    {
        FIBITMAP *base = load(f, 0, base_flags(f), BY_FILE), *want = base ? expected(base, f, &d) : NULL;
        FIBITMAP *got = FreeImage_Load(f->fif, f->path, FIF_LOAD_DISPLAY_ICC);
        CHECK(want && got && digest(want) == digest(got), "perceptual intent from the setter");
        if (base) FreeImage_Unload(base);
        if (want) FreeImage_Unload(want);
        if (got) FreeImage_Unload(got);
    }
    free_display(&d);
}

/* ------------------------------------------------------------------ threads */

typedef struct {
    const Fixture *f;
    unsigned long long ok[2];
    int loads;
    int bad;
} Job;

static atomic_int stop_threads;

static void *worker(void *arg) {
    Job *j = (Job *)arg;
    while (!atomic_load(&stop_threads)) {
        FIBITMAP *dib = FreeImage_Load(j->f->fif, j->f->path, j->f->flags | FIF_LOAD_DISPLAY_ICC);
        unsigned long long h = dib ? digest(dib) : 0;
        if (h != j->ok[0] && h != j->ok[1]) j->bad++;
        j->loads++;
        if (dib) FreeImage_Unload(dib);
    }
    return NULL;
}

/* loads on 8 threads while the display changes */
static void threads(void) {
    Bytes adobe = builtin(FICMS_PROFILE_ADOBE_RGB), p3 = builtin(FICMS_PROFILE_DISPLAY_P3);
    const char *names[4] = { "CMYK TIFF", "palette PNG with tRNS", "grey 1.8 UINT16 TIFF", "RGB JPEG + thumbnail" };
    Job jobs[8];
    pthread_t t[8];
    int i, k, loads = 0, bad = 0;
    for (i = 0; i < 8; i++) {
        const Fixture *f = find(names[i % 4]);
        jobs[i].f = f; jobs[i].loads = 0; jobs[i].bad = 0;
        for (k = 0; k < 2; k++) {
            FIBITMAP *dib;
            FreeImage_SetDisplayICCProfile(k ? p3.data : adobe.data, k ? p3.size : adobe.size, FLAGS_0x101);
            dib = FreeImage_Load(f->fif, f->path, f->flags | FIF_LOAD_DISPLAY_ICC);
            jobs[i].ok[k] = dib ? digest(dib) : 1;
            if (dib) FreeImage_Unload(dib);
        }
    }
    atomic_store(&stop_threads, 0);
    for (i = 0; i < 8; i++) pthread_create(&t[i], NULL, worker, &jobs[i]);
    for (k = 0; k < 60; k++) {
        FreeImage_SetDisplayICCProfile(k & 1 ? p3.data : adobe.data, k & 1 ? p3.size : adobe.size, FLAGS_0x101);
        usleep(5000);
    }
    atomic_store(&stop_threads, 1);
    for (i = 0; i < 8; i++) {
        pthread_join(t[i], NULL);
        loads += jobs[i].loads;
        bad += jobs[i].bad;
    }
    CHECK(bad == 0, "threads: %d of %d loads matched neither display", bad, loads);
    printf("threads                %d loads on 8 threads, 60 display changes\n", loads);
}

/* ------------------------------------------------------------------ X11, through xcbstub */
#ifndef _WIN32

static int *stub_connects;

static void stub(const char *mode, const char *file, const char *file1, const char *screen) {
    setenv("XCB_STUB_MODE", mode, 1);
    if (file) setenv("XCB_STUB_FILE", file, 1); else unsetenv("XCB_STUB_FILE");
    if (file1) setenv("XCB_STUB_FILE1", file1, 1); else unsetenv("XCB_STUB_FILE1");
    setenv("XCB_STUB_SCREEN", screen ? screen : "0", 1);
}

/* detection starts again, and gives this profile */
static void detects(const char *what, const Bytes *want) {
    Bytes now;
    FreeImage_SetDisplayICCProfile(NULL, 0, FLAGS_0x101);
    now = current_display();
    CHECK(same_bytes(&now, want->data, want->size), "X11, %s: a %u-byte profile instead of %u bytes", what, now.size, want->size);
    free(now.data);
}

static int x11(void) {
    Bytes srgb = builtin(FICMS_PROFILE_SRGB), adobe = builtin(FICMS_PROFILE_ADOBE_RGB), p3 = builtin(FICMS_PROFILE_DISPLAY_P3);
    Bytes t5 = read_file("data/test5.icc"), now;
    const char *fa = scratch("dsp_x_adobe.icc"), *fp = scratch("dsp_x_p3.icc"), *fc = scratch("dsp_x_press.icc"), *fj = scratch("dsp_x_junk.icc");
    void *xcb = dlopen("libxcb.so.1", RTLD_LAZY | RTLD_LOCAL);
    BYTE junk[700];
    int before, i;

    stub_connects = xcb ? (int *)dlsym(xcb, "xcb_stub_connects") : NULL;
    if (!stub_connects) {
        printf("  libxcb.so.1 is not the stub: run through make x11\n");
        return 1;
    }
    for (i = 0; i < 700; i++) junk[i] = (BYTE)rnd();
    write_file(fa, adobe.data, adobe.size); write_file(fp, p3.data, p3.size);
    write_file(fc, press.data, press.size); write_file(fj, junk, sizeof(junk));

    setenv("DISPLAY", ":0", 1);
    stub("ok", fa, NULL, NULL);          detects("Adobe RGB", &adobe);
    stub("ok", fc, NULL, NULL);          detects("a CMYK profile", &srgb);
    stub("ok", fj, NULL, NULL);          detects("junk", &srgb);
    stub("ok", NULL, NULL, NULL);        detects("no property", &srgb);
    stub("format32", fa, NULL, NULL);    detects("32-bit items", &srgb);
    stub("partial", fa, NULL, NULL);     detects("a property bigger than read", &srgb);
    stub("noatom", fa, NULL, NULL);      detects("no _ICC_PROFILE atom", &srgb);
    stub("error", fa, NULL, NULL);       detects("no connection", &srgb);
    stub("ok", fa, fp, "1");             detects("screen 1", &p3);
    if (t5.size) {
        const char *f5 = scratch("dsp_x_t5.icc");
        write_file(f5, t5.data, t5.size);
        stub("ok", f5, NULL, NULL);      detects("an sRGB twin", &srgb);
        remove(f5);
    }

    /* asked again after a second, not before */
    stub("ok", fa, NULL, NULL);          detects("Adobe RGB again", &adobe);
    before = *stub_connects;
    stub("ok", fp, NULL, NULL);
    now = current_display();
    CHECK(same_bytes(&now, adobe.data, adobe.size) && *stub_connects == before, "X11: asked again within a second");
    free(now.data);
    usleep(1100000);
    now = current_display();
    CHECK(same_bytes(&now, p3.data, p3.size) && *stub_connects == before + 1, "X11: the new profile after a second");
    free(now.data);

    /* the loads take it */
    {
        const Fixture *f = find("RGB JPEG + thumbnail");
        FIBITMAP *dib = f ? FreeImage_Load(f->fif, f->path, FIF_LOAD_DISPLAY_ICC) : NULL;
        CHECK(dib && FreeImage_GetICCProfile(dib)->size == p3.size && !memcmp(FreeImage_GetICCProfile(dib)->data, p3.data, p3.size), "X11: a load in the display's colors");
        if (dib) FreeImage_Unload(dib);
    }

    /* a pinned profile is not asked for */
    FreeImage_SetDisplayICCProfile(adobe.data, adobe.size, FLAGS_0x101);
    before = *stub_connects;
    usleep(1100000);
    now = current_display();
    CHECK(same_bytes(&now, adobe.data, adobe.size) && *stub_connects == before, "X11: a pinned profile was replaced");
    free(now.data);

    /* no DISPLAY: libxcb is not called */
    unsetenv("DISPLAY");
    before = *stub_connects;
    detects("no DISPLAY", &srgb);
    CHECK(*stub_connects == before, "X11: connected without DISPLAY");

    remove(fa); remove(fp); remove(fc); remove(fj);
    free(t5.data);
    printf("%d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
#endif

/* ------------------------------------------------------------------ */

int main(int argc, char **argv) {
    Bytes srgb = builtin(FICMS_PROFILE_SRGB), adobe = builtin(FICMS_PROFILE_ADOBE_RGB), p3 = builtin(FICMS_PROFILE_DISPLAY_P3);
    Bytes t3 = read_file("data/test3.icc");
    Display d;
    int i;

#ifdef FREEIMAGE_LIB
    FreeImage_Initialise(FALSE);
#endif
    FreeImage_SetOutputMessage(on_message);
    press = make_press_profile();
    grey18 = make_gray_profile(1.8);
    unlinkable = make_unlinkable_rgb();
    make_fixtures();

#ifndef _WIN32
    if (argc > 1 && !strcmp(argv[1], "-x11")) return x11();

    /* the tests must not see this machine's display */
    unsetenv("DISPLAY");
#endif

    setter_getter();

    {
        const char *names[4] = { "sRGB", "Adobe RGB", "Display P3", "test3.icc (LUT)" };
        const Bytes *profiles[4] = { &srgb, &adobe, &p3, &t3 };
        for (i = 0; i < 4; i++) {
            if (!profiles[i]->size) continue;
            if (!use_display(&d, names[i], profiles[i]->data, profiles[i]->size, FLAGS_0x101)) continue;
            if (i == 0) {
                Bytes g = builtin(FICMS_PROFILE_GRAY);
                CHECK(same_bytes(&d.grey, g.data, g.size), "sRGB: the grey profile is not FreeImage's grey");
            }
            check_grey_companion(&d);
            check_display(&d);
            reload_is_identity(&d);
            free_display(&d);
        }
    }
    /* detection, with nothing to detect, is sRGB */
    FreeImage_SetDisplayICCProfile(NULL, 0, FLAGS_0x101);
    {
        Display auto_d;
        if (use_display(&auto_d, "sRGB, detected", NULL, 0, FLAGS_0x101)) {
            CHECK(same_bytes(&auto_d.rgb, srgb.data, srgb.size), "detected: not sRGB");
            check_display(&auto_d);
            free_display(&auto_d);
        }
    }
    header_only();
    flag_rules();
    FreeImage_SetDisplayICCProfile(p3.data, p3.size, FLAGS_0x101);
    multipage_save();
    intents();
    threads();

    for (i = 0; i < fixture_count; i++) {
        if (strncmp(fixtures[i].path, "../", 3)) remove(fixtures[i].path);
        free((void *)fixtures[i].path);
    }
    free(press.data); free(grey18.data); free(unlinkable.data); free(t3.data);
    printf("%d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
