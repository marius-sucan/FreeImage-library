/*
 * FreeImage 3 - JPEG XR regression test
 *
 * A save carrying ICC + EXIF + XMP, exercising WriteMetadata's return check on
 * the way out and ReadProfile on the way back in. Pass a source image as argv[1];
 * defaults to raw_exif.jpg beside this test.
 *
 * Standalone: build with the Makefile in this directory, run from anywhere.
 * Scratch files are written to $JXR_TEST_TMP (default: the current directory).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "FreeImage.h"

/* Scratch files go to $JXR_TEST_TMP, or the current directory. */
static const char *tmppath(const char *name) {
    static char buf[1024];
    const char *dir = getenv("JXR_TEST_TMP");
    snprintf(buf, sizeof(buf), "%s/%s", (dir && *dir) ? dir : ".", name);
    return buf;
}
int main(int argc, char **argv){
    FIBITMAP *d,*b; FITAG *t; BYTE icc[512]; int i; unsigned n;
    const char *out = tmppath("fi_jxr_meta.jxr");
    FreeImage_Initialise(FALSE);
    {   /* run from this directory, from TestAPI/, or from the repo root */
        static const char *cand[] = { NULL, "raw_exif.jpg", "../raw_exif.jpg",
                                      "TestAPI/raw_exif.jpg" };
        int c; cand[0] = (argc > 1) ? argv[1] : NULL;
        for(c = (argc > 1) ? 0 : 1; c < 4 && !d; c++)
            d = FreeImage_Load(FIF_JPEG, cand[c], JPEG_EXIFROTATE);
    }
    if(!d){ printf("source image not found (pass one as argv[1])\n"); return 2; }
    printf("source EXIF_MAIN tags: %u  EXIF_EXIF tags: %u\n",
        FreeImage_GetMetadataCount(FIMD_EXIF_MAIN,d), FreeImage_GetMetadataCount(FIMD_EXIF_EXIF,d));
    for(i=0;i<512;i++) icc[i]=(BYTE)(i*7);
    FreeImage_CreateICCProfile(d, icc, sizeof(icc));
    t = FreeImage_CreateTag();
    {   const char *xmp = "<?xpacket begin=''?><x:xmpmeta xmlns:x='adobe:ns:meta/'/><?xpacket end='w'?>";
        FreeImage_SetTagKey(t,"XMLPacket"); FreeImage_SetTagType(t,FIDT_ASCII);
        FreeImage_SetTagLength(t,(DWORD)strlen(xmp)+1); FreeImage_SetTagCount(t,(DWORD)strlen(xmp)+1);
        FreeImage_SetTagValue(t,(void*)xmp);
        FreeImage_SetMetadata(FIMD_XMP,d,FreeImage_GetTagKey(t),t); }
    FreeImage_DeleteTag(t);

    printf("save with ICC+EXIF+XMP -> %s\n", FreeImage_Save(FIF_JXR,d,out,0)?"ok":"FAILED");
    b = FreeImage_Load(FIF_JXR,out,0);
    if(!b){ printf("reload FAILED\n"); return 1; }
    printf("reload ok: %ux%u\n", FreeImage_GetWidth(b), FreeImage_GetHeight(b));
    n = FreeImage_GetICCProfile(b)->size;
    printf("  ICC profile back: %u bytes, %s\n", n,
           (n==sizeof(icc) && !memcmp(FreeImage_GetICCProfile(b)->data,icc,n))?"identical":"DIFFERENT");
    printf("  EXIF_MAIN tags back: %u\n", FreeImage_GetMetadataCount(FIMD_EXIF_MAIN,b));
    printf("  EXIF_EXIF tags back: %u\n", FreeImage_GetMetadataCount(FIMD_EXIF_EXIF,b));
    printf("  XMP present: %s\n", FreeImage_GetMetadata(FIMD_XMP,b,"XMLPacket",&t)?"yes":"no");
    FreeImage_Unload(b); FreeImage_Unload(d);
    FreeImage_DeInitialise(); return 0;
}
