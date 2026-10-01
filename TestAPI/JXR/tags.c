/* FreeImage 3 - JPEG XR descriptive tags: every Exif-Main tag the container holds, alone, together and of a
   wrong type, saved losslessly and loaded back; the pixels must survive and the tags come back or be left out */
/* argv[1]: a TIFF with a PageNumber tag, default ../../Wrapper/FreeImagePlus/test/test.tif */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
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

static void quiet(FREE_IMAGE_FORMAT fif, const char *msg) { (void)fif; (void)msg; }

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

static void put(FIBITMAP *dib, const char *key, WORD id, FREE_IMAGE_MDTYPE type, DWORD count, const void *value) {
    FITAG *tag = FreeImage_CreateTag();
    FreeImage_SetTagKey(tag, key);
    FreeImage_SetTagID(tag, id);
    FreeImage_SetTagType(tag, type);
    FreeImage_SetTagCount(tag, count);
    FreeImage_SetTagLength(tag, count * width(type));
    FreeImage_SetTagValue(tag, value);
    FreeImage_SetMetadata(FIMD_EXIF_MAIN, dib, key, tag);
    FreeImage_DeleteTag(tag);
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

/* saved lossless and loaded back with the same pixels, or NULL */
static FIBITMAP *round_trip(FIBITMAP *dib, const char *what) {
    FIMEMORY *mem = FreeImage_OpenMemory(NULL, 0);
    FIBITMAP *back = NULL;
    unsigned y;
    int same = 1;
    if (!FreeImage_SaveToMemory(FIF_JXR, dib, mem, JXR_LOSSLESS)) {
        CHECK(0, "%s: not saved", what);
    } else {
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
