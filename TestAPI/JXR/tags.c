/* FreeImage 3 - JPEG XR descriptive tags: every Exif-Main tag the container holds, alone, together and of a
   wrong type, saved losslessly and loaded back; the pixels must survive and the tags come back or be left out;
   every save has its IFD entries in ascending tag order (T.832 A.7.2), the values past them at even offsets (A.7.5).
   Entries jxrlib has no use for, Annex A's and a private one, are ignored without a line on stderr (A.7.1) */
/* argv[1]: a TIFF with a PageNumber tag, default ../../Wrapper/FreeImagePlus/test/test.tif */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "FreeImage.h"

static int checks = 0, failures = 0;
#define CHECK(cond, ...) do { checks++; if (!(cond)) { failures++; printf("  FAIL "); printf(__VA_ARGS__); printf("\n"); } } while (0)

typedef struct {
    const char *key;
    WORD id;
    FREE_IMAGE_MDTYPE type;
    DWORD count;
    const void *value;
} Tag;

static const WORD PAGES[2] = { 2, 5 };
static const WORD STARS = 4, PERCENT = 75;
static const BYTE TITLE[] = { 'T', 0, 'i', 0, 't', 0, 'l', 0, 'e', 0, 0, 0 };

/* the 14 tags, each of the type the container stores */
static const Tag TAGS[14] = {
    { "ImageDescription", 0x010e, FIDT_ASCII, 12, "Description" },
    { "Make", 0x010f, FIDT_ASCII, 7, "Camera" },
    { "Model", 0x0110, FIDT_ASCII, 6, "Model" },
    { "Software", 0x0131, FIDT_ASCII, 10, "FreeImage" },
    { "DateTime", 0x0132, FIDT_ASCII, 20, "2026:10:01 12:00:00" },
    { "Artist", 0x013b, FIDT_ASCII, 3, "Bo" },
    { "Copyright", 0x8298, FIDT_ASCII, 5, "Copy" },
    { "Rating", 0x4746, FIDT_SHORT, 1, &STARS },
    { "RatingPercent", 0x4749, FIDT_SHORT, 1, &PERCENT },
    { "XPTitle", 0x9c9b, FIDT_BYTE, sizeof(TITLE), TITLE },
    { "DocumentName", 0x010d, FIDT_ASCII, 9, "Document" },
    { "PageName", 0x011d, FIDT_ASCII, 2, "P" },
    { "PageNumber", 0x0129, FIDT_SHORT, 2, PAGES },
    { "HostComputer", 0x013c, FIDT_ASCII, 5, "Host" },
};

/* the IFD tags of the last file saved, in file order */
static unsigned saved_tags[64], saved_count = 0;

static void quiet(FREE_IMAGE_FORMAT fif, const char *msg) { (void)fif; (void)msg; }

static unsigned get16(const BYTE *p) { return p[0] | (p[1] << 8); }
static unsigned get32(const BYTE *p) { return p[0] | (p[1] << 8) | (p[2] << 16) | ((unsigned)p[3] << 24); }

/* records the first IFD's tags; ORDERED if they ascend, EVEN if every value stored past its entry starts at an even offset */
enum { ORDERED = 1, EVEN = 2 };
static int read_ifd(const BYTE *data, DWORD size) {
    static const unsigned TYPE_SIZE[13] = { 0, 1, 1, 2, 4, 8, 1, 1, 2, 4, 8, 4, 8 };
    const DWORD ifd = (data && size >= 8) ? get32(data + 4) : 0;
    unsigned i, n;
    int result = ORDERED | EVEN;
    saved_count = 0;
    if (!ifd || ifd + 2 > size) return 0;
    n = get16(data + ifd);
    for (i = 0; i < n && i < 64 && ifd + 2 + 12 * (i + 1) <= size; i++) {
        const BYTE *e = data + ifd + 2 + 12 * i;
        const unsigned type = get16(e + 2);
        saved_tags[saved_count++] = get16(e);
        if (i && saved_tags[i] <= saved_tags[i - 1]) result &= ~ORDERED;
        if (type < 13 && TYPE_SIZE[type] * get32(e + 4) > 4 && (get32(e + 8) & 1)) result &= ~EVEN;
    }
    return (saved_count == n) ? result : 0;
}

static int saved(unsigned tag) {
    unsigned i;
    for (i = 0; i < saved_count; i++) {
        if (saved_tags[i] == tag) return 1;
    }
    return 0;
}

static DWORD width(FREE_IMAGE_MDTYPE type) { return (type == FIDT_SHORT) ? 2 : (type == FIDT_LONG) ? 4 : 1; }

static FIBITMAP *picture(void) {
    FIBITMAP *dib = FreeImage_Allocate(40, 24, 24, FI_RGBA_RED_MASK, FI_RGBA_GREEN_MASK, FI_RGBA_BLUE_MASK);
    unsigned x, y;
    for (y = 0; y < 24; y++) {
        BYTE *p = FreeImage_GetScanLine(dib, y);
        for (x = 0; x < FreeImage_GetLine(dib); x++) p[x] = (BYTE)(x * 7 + y * 13);
    }
    return dib;
}

static void put_in(FREE_IMAGE_MDMODEL model, FIBITMAP *dib, const char *key, WORD id, FREE_IMAGE_MDTYPE type, DWORD count, const void *value) {
    FITAG *tag = FreeImage_CreateTag();
    FreeImage_SetTagKey(tag, key);
    FreeImage_SetTagID(tag, id);
    FreeImage_SetTagType(tag, type);
    FreeImage_SetTagCount(tag, count);
    FreeImage_SetTagLength(tag, count * width(type));
    FreeImage_SetTagValue(tag, value);
    FreeImage_SetMetadata(model, dib, key, tag);
    FreeImage_DeleteTag(tag);
}

static void put(FIBITMAP *dib, const char *key, WORD id, FREE_IMAGE_MDTYPE type, DWORD count, const void *value) {
    put_in(FIMD_EXIF_MAIN, dib, key, id, type, count, value);
}

/* the tag came back with these bytes; UTF-16 comes back FIDT_UNDEFINED */
static int same_tag(FIBITMAP *dib, const Tag *t) {
    FITAG *tag = NULL;
    const DWORD length = t->count * width(t->type);
    if (!FreeImage_GetMetadata(FIMD_EXIF_MAIN, dib, t->key, &tag) || !tag) return 0;
    if (FreeImage_GetTagType(tag) != ((t->type == FIDT_BYTE) ? FIDT_UNDEFINED : t->type)) return 0;
    return (FreeImage_GetTagLength(tag) == length) && !memcmp(FreeImage_GetTagValue(tag), t->value, length);
}

static int has_tag(FIBITMAP *dib, const char *key) {
    FITAG *tag = NULL;
    return FreeImage_GetMetadata(FIMD_EXIF_MAIN, dib, key, &tag) && tag;
}

/* a copy of a save whose first IFD, with these entries added in tag order, is appended and made the first */
static BYTE *with_entries(const BYTE *data, DWORD size, const BYTE (*added)[12], unsigned count, DWORD *out_size) {
    const DWORD ifd = get32(data + 4), at = (size + 1) & ~1u;
    const unsigned n = get16(data + ifd);
    unsigned i = 0, j = 0, k = 0;
    BYTE *out = (BYTE *)calloc(at + 2 + 12 * (n + count) + 4, 1);
    memcpy(out, data, size);
    out[4] = (BYTE)at, out[5] = (BYTE)(at >> 8), out[6] = (BYTE)(at >> 16), out[7] = (BYTE)(at >> 24);
    out[at] = (BYTE)(n + count), out[at + 1] = (BYTE)((n + count) >> 8);
    while (i < n || j < count) {
        const BYTE *e = (j == count || (i < n && get16(data + ifd + 2 + 12 * i) < get16(added[j]))) ? data + ifd + 2 + 12 * i++ : added[j++];
        memcpy(out + at + 2 + 12 * k++, e, 12);
    }
    *out_size = at + 2 + 12 * (n + count) + 4;
    return out;
}

/* loads a file from memory, returning how many bytes went to stderr meanwhile */
static long load_watching_stderr(BYTE *data, DWORD size, FIBITMAP **dib) {
    FILE *t = tmpfile();
    FIMEMORY *mem;
    long written = -1;
    int saved_fd;
    *dib = NULL;
    if (!t) return -1;
    fflush(stderr);
    saved_fd = dup(2);
    dup2(fileno(t), 2);
    mem = FreeImage_OpenMemory(data, size);
    *dib = FreeImage_LoadFromMemory(FIF_JXR, mem, 0);
    FreeImage_CloseMemory(mem);
    fflush(stderr);
    dup2(saved_fd, 2);
    close(saved_fd);
    fseek(t, 0, SEEK_END);
    written = ftell(t);
    fclose(t);
    return written;
}

/* saved lossless and loaded back with the same pixels, or NULL */
static FIBITMAP *round_trip(FIBITMAP *dib, const char *what) {
    FIMEMORY *mem = FreeImage_OpenMemory(NULL, 0);
    FIBITMAP *back = NULL;
    unsigned y;
    int same = 1;
    if (!FreeImage_SaveToMemory(FIF_JXR, dib, mem, JXR_LOSSLESS)) {
        CHECK(0, "%s: not saved", what);
    } else {
        BYTE *data = NULL;
        DWORD size = 0;
        int layout;
        FreeImage_AcquireMemory(mem, &data, &size);
        layout = read_ifd(data, size);
        CHECK(layout & ORDERED, "%s: IFD entries not in ascending tag order", what);
        CHECK(layout & EVEN, "%s: a value at an odd offset", what);
        FreeImage_SeekMemory(mem, 0, SEEK_SET);
        back = FreeImage_LoadFromMemory(FIF_JXR, mem, 0);
        CHECK(back != NULL, "%s: saved, but does not load", what);
    }
    if (back) {
        same = (FreeImage_GetWidth(back) == FreeImage_GetWidth(dib)) && (FreeImage_GetHeight(back) == FreeImage_GetHeight(dib)) &&
               (FreeImage_GetBPP(back) == FreeImage_GetBPP(dib));
        for (y = 0; same && y < FreeImage_GetHeight(dib); y++)
            same = !memcmp(FreeImage_GetScanLine(back, y), FreeImage_GetScanLine(dib, y), FreeImage_GetLine(dib));
        CHECK(same, "%s: other pixels", what);
    }
    FreeImage_CloseMemory(mem);
    return back;
}

int main(int argc, char **argv) {
    const char *tiff = (argc > 1) ? argv[1] : "../../Wrapper/FreeImagePlus/test/test.tif";
    FIBITMAP *dib, *back;
    int i, j;
    char what[128];
    FreeImage_Initialise(FALSE);
    FreeImage_SetOutputMessage(quiet);

    /* each tag alone: the short ones fit in their entry, with no data area */
    for (i = 0; i < 14; i++) {
        dib = picture();
        put(dib, TAGS[i].key, TAGS[i].id, TAGS[i].type, TAGS[i].count, TAGS[i].value);
        snprintf(what, sizeof(what), "%s alone", TAGS[i].key);
        back = round_trip(dib, what);
        CHECK(!back || same_tag(back, &TAGS[i]), "%s: not the same tag back", what);
        FreeImage_Unload(back);
        FreeImage_Unload(dib);
    }

    /* all of them */
    dib = picture();
    for (i = 0; i < 14; i++) put(dib, TAGS[i].key, TAGS[i].id, TAGS[i].type, TAGS[i].count, TAGS[i].value);
    back = round_trip(dib, "all 14 tags");
    for (i = 0; back && i < 14; i++) CHECK(same_tag(back, &TAGS[i]), "all 14 tags: %s not the same back", TAGS[i].key);
    FreeImage_Unload(back);
    FreeImage_Unload(dib);

    /* all of them with XMP, IPTC, Exif, GPS and an ICC profile: every IFD entry a save can have */
    {
        static const char XMP[] = "<x:xmpmeta xmlns:x='adobe:ns:meta/'/>";
        static const WORD ISO = 200;
        static const BYTE GPS_VERSION[4] = { 2, 2, 0, 0 };
        static const unsigned ENTRIES[] = { 0x02bc, 0x83bb, 0x8769, 0x8773, 0x8825, 0x9c9b, 0xbc01 };
        BYTE icc[132];
        FITAG *tag = NULL;
        FIICCPROFILE *profile;
        dib = picture();
        for (i = 0; i < 14; i++) put(dib, TAGS[i].key, TAGS[i].id, TAGS[i].type, TAGS[i].count, TAGS[i].value);
        put_in(FIMD_XMP, dib, "XMLPacket", 0x02bc, FIDT_ASCII, sizeof(XMP), XMP);
        put_in(FIMD_IPTC, dib, "Headline", 0x0269, FIDT_ASCII, 9, "Headline");
        put_in(FIMD_EXIF_EXIF, dib, "ISOSpeedRatings", 0x8827, FIDT_SHORT, 1, &ISO);
        put_in(FIMD_EXIF_GPS, dib, "GPSVersionID", 0x0000, FIDT_BYTE, 4, GPS_VERSION);
        memset(icc, 0, sizeof(icc));
        icc[3] = sizeof(icc);
        memcpy(icc + 36, "acsp", 4);
        FreeImage_CreateICCProfile(dib, icc, sizeof(icc));
        back = round_trip(dib, "everything");
        for (i = 0; i < (int)(sizeof(ENTRIES) / sizeof(ENTRIES[0])); i++) CHECK(saved(ENTRIES[i]), "everything: no %04x entry", ENTRIES[i]);
        if (back) {
            for (i = 0; i < 14; i++) CHECK(same_tag(back, &TAGS[i]), "everything: %s not the same back", TAGS[i].key);
            CHECK(FreeImage_GetMetadata(FIMD_XMP, back, "XMLPacket", &tag) && tag && FreeImage_GetTagLength(tag) >= strlen(XMP) &&
                  !memcmp(FreeImage_GetTagValue(tag), XMP, strlen(XMP)), "everything: XMP not the same back");
            CHECK(FreeImage_GetMetadata(FIMD_IPTC, back, "Headline", &tag) && tag && !strcmp((const char *)FreeImage_GetTagValue(tag), "Headline"),
                  "everything: IPTC Headline not the same back");
            CHECK(FreeImage_GetMetadata(FIMD_EXIF_EXIF, back, "ISOSpeedRatings", &tag) && tag && *(const WORD *)FreeImage_GetTagValue(tag) == ISO,
                  "everything: Exif ISOSpeedRatings not the same back");
            CHECK(FreeImage_GetMetadata(FIMD_EXIF_GPS, back, "GPSVersionID", &tag) && tag && !memcmp(FreeImage_GetTagValue(tag), GPS_VERSION, 4),
                  "everything: GPSVersionID not the same back");
            profile = FreeImage_GetICCProfile(back);
            CHECK(profile->size == sizeof(icc) && !memcmp(profile->data, icc, sizeof(icc)), "everything: ICC profile not the same back");
        }
        FreeImage_Unload(back);
        FreeImage_Unload(dib);
    }

    /* only the inline ones: the rating, the page number, short strings */
    dib = picture();
    for (i = 0; i < 14; i++) {
        if (TAGS[i].count * width(TAGS[i].type) <= 4) put(dib, TAGS[i].key, TAGS[i].id, TAGS[i].type, TAGS[i].count, TAGS[i].value);
    }
    back = round_trip(dib, "the inline tags");
    for (i = 0; back && i < 14; i++) {
        if (TAGS[i].count * width(TAGS[i].type) <= 4) CHECK(same_tag(back, &TAGS[i]), "the inline tags: %s not the same back", TAGS[i].key);
    }
    FreeImage_Unload(back);
    FreeImage_Unload(dib);

    /* a tag of a wrong type is left out, the others kept */
    {
        static const DWORD LONG_VALUE = 3;
        static const BYTE UNTERMINATED[] = { 'T', 0, 'i', 0 };
        static const struct { const char *key; WORD id; FREE_IMAGE_MDTYPE type; DWORD count; const void *value; } WRONG[] = {
            { "Software", 0x0131, FIDT_BYTE, 10, "FreeImage" },
            { "Rating", 0x4746, FIDT_LONG, 1, &LONG_VALUE },
            { "PageNumber", 0x0129, FIDT_LONG, 1, &LONG_VALUE },
            { "XPTitle", 0x9c9b, FIDT_ASCII, 6, "Title" },
            { "XPTitle", 0x9c9b, FIDT_BYTE, sizeof(UNTERMINATED), UNTERMINATED },
            { "Artist", 0x013b, FIDT_SHORT, 1, &STARS },
        };
        for (j = 0; j < (int)(sizeof(WRONG) / sizeof(WRONG[0])); j++) {
            /* with the Make tag, which has to survive */
            dib = picture();
            put(dib, TAGS[1].key, TAGS[1].id, TAGS[1].type, TAGS[1].count, TAGS[1].value);
            put(dib, WRONG[j].key, WRONG[j].id, WRONG[j].type, WRONG[j].count, WRONG[j].value);
            snprintf(what, sizeof(what), "%s of type %d", WRONG[j].key, (int)WRONG[j].type);
            back = round_trip(dib, what);
            if (back) {
                CHECK(!has_tag(back, WRONG[j].key), "%s: written", what);
                CHECK(same_tag(back, &TAGS[1]), "%s: Make lost", what);
            }
            FreeImage_Unload(back);
            FreeImage_Unload(dib);
        }
    }

    /* COLOR_SPACE, IMAGE_TYPE, PTM_COLOR_INFO, PROFILE_LEVEL_CONTAINER, PADDING_DATA and a private tag, values in their entries */
    {
        static const BYTE ADDED[6][12] = {
            { 0x01, 0xa0, 3, 0, 1, 0, 0, 0, 0xff, 0xff, 0, 0 },
            { 0x04, 0xbc, 4, 0, 1, 0, 0, 0, 0, 0, 0, 0 },
            { 0x05, 0xbc, 1, 0, 4, 0, 0, 0, 2, 2, 2, 0 },
            { 0x06, 0xbc, 1, 0, 4, 0, 0, 0, 111, 255, 0, 1 },
            { 0x1c, 0xea, 7, 0, 4, 0, 0, 0, 0x1c, 0xea, 0, 0 },
            { 0xe8, 0xfd, 4, 0, 1, 0, 0, 0, 7, 0, 0, 0 }
        };
        FIMEMORY *mem = FreeImage_OpenMemory(NULL, 0);
        dib = picture();
        if (!FreeImage_SaveToMemory(FIF_JXR, dib, mem, JXR_LOSSLESS)) {
            CHECK(0, "unused tags: not saved");
        } else {
            BYTE *data = NULL, *tagged;
            DWORD size = 0, tagged_size = 0;
            unsigned y;
            int same;
            long written;
            FreeImage_AcquireMemory(mem, &data, &size);
            tagged = with_entries(data, size, ADDED, 6, &tagged_size);
            CHECK(read_ifd(tagged, tagged_size) == (ORDERED | EVEN) && saved(0xa001) && saved(0xfde8), "unused tags: file not made");
            written = load_watching_stderr(tagged, tagged_size, &back);
            CHECK(written == 0, "unused tags: %ld bytes written to stderr", written);
            same = back && FreeImage_GetWidth(back) == FreeImage_GetWidth(dib) && FreeImage_GetHeight(back) == FreeImage_GetHeight(dib);
            for (y = 0; same && y < FreeImage_GetHeight(dib); y++)
                same = !memcmp(FreeImage_GetScanLine(back, y), FreeImage_GetScanLine(dib, y), FreeImage_GetLine(dib));
            CHECK(same, "unused tags: %s", back ? "other pixels" : "does not load");
            FreeImage_Unload(back);
            free(tagged);
        }
        FreeImage_CloseMemory(mem);
        FreeImage_Unload(dib);
    }

    /* a TIFF with a PageNumber, as loaded */
    dib = FreeImage_Load(FIF_TIFF, tiff, 0);
    if (!dib) {
        printf("  %s not found: the TIFF case is skipped\n", tiff);
    } else {
        FITAG *tag = NULL;
        CHECK(FreeImage_GetMetadata(FIMD_EXIF_MAIN, dib, "PageNumber", &tag) && tag, "%s has no PageNumber", tiff);
        back = round_trip(dib, tiff);
        if (back && tag) {
            Tag t = { "PageNumber", 0x0129, FIDT_SHORT, FreeImage_GetTagCount(tag), FreeImage_GetTagValue(tag) };
            CHECK(same_tag(back, &t), "%s: PageNumber not the same back", tiff);
        }
        FreeImage_Unload(back);
        FreeImage_Unload(dib);
    }

    printf("%d checks, %d failures\n", checks, failures);
    FreeImage_DeInitialise();
    return failures ? 1 : 0;
}
