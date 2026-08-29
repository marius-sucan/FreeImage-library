/*
 * FreeImage 3 - JPEG XR regression test
 *
 * Edge cases the old 16x16 save floor used to hide: very small images at every
 * supported depth and quality, including the low-quality YUV 4:2:0 plus
 * two-level-overlap path. Also covers JXR_PROGRESSIVE and partial-file cleanup.
 * Best run under AddressSanitizer.
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

static char OUT[1024];

static FIBITMAP *mk(FREE_IMAGE_TYPE t,int w,int h,int bpp){
    FIBITMAP *d=(t==FIT_BITMAP)?FreeImage_Allocate(w,h,bpp,0,0,0):FreeImage_AllocateT(t,w,h,bpp,0,0,0);
    unsigned y,x,n; unsigned s=88675123u;
    if(!d) return NULL;
    n=FreeImage_GetLine(d);
    for(y=0;y<(unsigned)h;y++){ BYTE*p=FreeImage_GetScanLine(d,y);
        for(x=0;x<n;x++){ s^=s<<13; s^=s>>17; s^=s<<5; p[x]=(BYTE)(s>>9); } }
    if(bpp==1||bpp==8){ RGBQUAD*pal=FreeImage_GetPalette(d); int i,c=(bpp==1)?2:256;
        for(i=0;i<c;i++){ int v=(bpp==1)?(i?255:0):i; pal[i].rgbRed=pal[i].rgbGreen=pal[i].rgbBlue=(BYTE)v; } }
    return d;
}
static int rt(FREE_IMAGE_TYPE t,int bpp,int w,int h,int flags){
    FIBITMAP *d=mk(t,w,h,bpp), *b; int ok;
    if(!d) return -1;
    ok = FreeImage_Save(FIF_JXR,d,OUT,flags);
    if(ok){ b=FreeImage_Load(FIF_JXR,OUT,0);
            ok = b && FreeImage_GetWidth(b)==(unsigned)w && FreeImage_GetHeight(b)==(unsigned)h;
            if(b) FreeImage_Unload(b); }
    FreeImage_Unload(d);
    return ok?1:0;
}
int main(void){
    static const struct{const char*n;FREE_IMAGE_TYPE t;int bpp;} F[]={
        {"1bpp",FIT_BITMAP,1},{"8bpp",FIT_BITMAP,8},{"16bpp",FIT_BITMAP,16},
        {"24bpp",FIT_BITMAP,24},{"32bpp",FIT_BITMAP,32},{"uint16",FIT_UINT16,16},
        {"float",FIT_FLOAT,32},{"rgb16",FIT_RGB16,48},{"rgba16",FIT_RGBA16,64},
        {"rgbf",FIT_RGBF,96},{"rgbaf",FIT_RGBAF,128}};
    static const struct{const char*n;int f;} Q[]={{"q10(YUV420,OL2)",10},{"q49(YUV420)",49},
        {"q50(YUV444)",50},{"default",0},{"lossless",JXR_LOSSLESS}};
    static const int WH[][2]={{1,1},{2,3},{7,7},{8,16},{15,15},{16,1},{1,16},{17,33}};
    int i,q,s,fails=0,total=0;
    FreeImage_Initialise(FALSE);
    snprintf(OUT, sizeof(OUT), "%s", tmppath("fi_jxr_edge.jxr"));
    printf("=== tiny images x depth x quality (round-trip) ===\n");
    for(q=0;q<5;q++){
        printf("%-18s ",Q[q].n);
        for(i=0;i<11;i++) for(s=0;s<8;s++){
            int r=rt(F[i].t,F[i].bpp,WH[s][0],WH[s][1],Q[q].f);
            total++;
            if(r!=1){ fails++; printf("\n   FAIL %s %dx%d ",F[i].n,WH[s][0],WH[s][1]); }
        }
        printf(" ok\n");
    }
    printf("--> %d/%d round-trips ok\n\n",total-fails,total);

    printf("=== JXR_PROGRESSIVE ===\n");
    {   FIBITMAP *d=mk(FIT_BITMAP,64,48,24); FIBITMAP *b;
        long sq=0,pr=0; FILE*f;
        FreeImage_Save(FIF_JXR,d,OUT,0);
        f=fopen(OUT,"rb"); fseek(f,0,SEEK_END); sq=ftell(f); fclose(f);
        FreeImage_Save(FIF_JXR,d,OUT,JXR_PROGRESSIVE);
        f=fopen(OUT,"rb"); fseek(f,0,SEEK_END); pr=ftell(f); fclose(f);
        b=FreeImage_Load(FIF_JXR,OUT,0);
        printf("  sequential=%ld bytes  progressive=%ld bytes  differ=%s  reload=%s\n",
               sq,pr,(sq!=pr)?"yes":"NO",b?"ok":"FAIL");
        if(b) FreeImage_Unload(b);
        FreeImage_Save(FIF_JXR,d,OUT,JXR_PROGRESSIVE|JXR_LOSSLESS);
        b=FreeImage_Load(FIF_JXR,OUT,0);
        printf("  progressive+lossless reload=%s\n", b?"ok":"FAIL");
        if(b) FreeImage_Unload(b);
        FreeImage_Unload(d);
    }

    printf("\n=== partial-file cleanup on a refused save ===\n");
    {   /* 4bpp is not a supported JXR export depth -> Save must fail */
        FIBITMAP *d=FreeImage_Allocate(32,32,4,0,0,0);
        char p[1024]; snprintf(p, sizeof(p), "%s", tmppath("fi_jxr_refused.jxr"));
        FILE *f;
        int ok=FreeImage_Save(FIF_JXR,d,p,0);
        f=fopen(p,"rb");
        printf("  save=%s  file left on disk=%s\n", ok?"ok":"refused", f?"YES (bad)":"no (good)");
        if(f) fclose(f);
        FreeImage_Unload(d);
    }
    FreeImage_DeInitialise();
    return fails?1:0;
}
