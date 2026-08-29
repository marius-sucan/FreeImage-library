/*
 * FreeImage 3 - JPEG XR regression test
 *
 * Round-trip corpus: saves every format the plugin supports at three quality
 * settings, printing size + checksum per output, and checks that a lossless
 * round-trip is pixel-exact. Run before and after a change and diff the output.
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

static unsigned long long sum64(const char *path){          /* order-sensitive checksum */
    FILE *f=fopen(path,"rb"); unsigned long long h=1469598103934665603ULL; int c;
    if(!f) return 0;
    while((c=fgetc(f))!=EOF){ h^=(unsigned char)c; h*=1099511628211ULL; }
    fclose(f); return h;
}
static long long fsize(const char*p){FILE*f=fopen(p,"rb");long long n;if(!f)return -1;
    fseek(f,0,SEEK_END);n=ftell(f);fclose(f);return n;}

static FIBITMAP *mk(FREE_IMAGE_TYPE t,int w,int h,int bpp){
    FIBITMAP *d = (t==FIT_BITMAP) ? FreeImage_Allocate(w,h,bpp,0,0,0)
                                  : FreeImage_AllocateT(t,w,h,bpp,0,0,0);
    unsigned y; unsigned s=2463534242u;
    if(!d) return NULL;
    for(y=0;y<(unsigned)h;y++){
        BYTE *p=FreeImage_GetScanLine(d,y);
        unsigned n=FreeImage_GetLine(d), x;
        for(x=0;x<n;x++){ s^=s<<13; s^=s>>17; s^=s<<5; p[x]=(BYTE)(s>>7); }
    }
    if(bpp==1||bpp==8){                       /* greyscale ramp -> FIC_MINISBLACK */
        RGBQUAD *pal=FreeImage_GetPalette(d); int i,n=(bpp==1)?2:256;
        for(i=0;i<n;i++){ int v=(bpp==1)?(i?255:0):i;
            pal[i].rgbRed=pal[i].rgbGreen=pal[i].rgbBlue=(BYTE)v; }
    }
    return d;
}
static int same(FIBITMAP*a,FIBITMAP*b){
    unsigned y,n;
    if(!a||!b) return 0;
    if(FreeImage_GetWidth(a)!=FreeImage_GetWidth(b)||FreeImage_GetHeight(a)!=FreeImage_GetHeight(b)
       ||FreeImage_GetBPP(a)!=FreeImage_GetBPP(b)||FreeImage_GetImageType(a)!=FreeImage_GetImageType(b))
        return 0;
    n=FreeImage_GetLine(a);
    for(y=0;y<FreeImage_GetHeight(a);y++)
        if(memcmp(FreeImage_GetScanLine(a,y),FreeImage_GetScanLine(b,y),n)) return 0;
    return 1;
}
/* Which formats JXR_LOSSLESS actually round-trips bit-exactly. The codec's float
   pipeline works in a fixed-point internal space and 16-bit packed RGB loses bits,
   so "lossless" is exact only for the integer formats - a property of JPEG XR, not
   a defect. Flagged here so a real change in behaviour stands out. */
static void run(const char*name,FREE_IMAGE_TYPE t,int bpp,int w,int h,int exact_expected){
    static const struct{const char*q;int f;} Q[]={{"default",0},{"q90",90},{"lossless",JXR_LOSSLESS}};
    FIBITMAP *d=mk(t,w,h,bpp); int i;
    const char *out = tmppath("fi_jxr_regress.jxr");
    if(!d){ printf("%-14s %2dbpp %3dx%-3d  ALLOC FAILED\n",name,bpp,w,h); return; }
    for(i=0;i<3;i++){
        int ok=FreeImage_Save(FIF_JXR,d,out,Q[i].f);
        if(!ok){ printf("%-14s %2dbpp %3dx%-3d %-8s  SAVE-REFUSED\n",name,bpp,w,h,Q[i].q); continue; }
        {
            FIBITMAP *b=FreeImage_Load(FIF_JXR,out,0);
            const char *note="";
            if(Q[i].f==JXR_LOSSLESS){
                const int got=same(d,b);
                note = got ? (exact_expected ? " exact"
                                             : " exact (better than expected)")
                           : (exact_expected ? " *** REGRESSION: no longer bit-exact ***"
                                             : " not bit-exact (expected for this format)");
            }
            printf("%-14s %2dbpp %3dx%-3d %-8s  size=%-8lld sum=%016llx  reload=%s%s\n",
                   name,bpp,w,h,Q[i].q,fsize(out),sum64(out), b?"ok":"FAIL", note);
            if(b) FreeImage_Unload(b);
        }
    }
    FreeImage_Unload(d);
}
int main(void){
    FreeImage_Initialise(FALSE);
    printf("--- JXR round-trip corpus ---\n");
    run("1bpp-mono",  FIT_BITMAP,1,  64,48, 1);
    run("8bpp-grey",  FIT_BITMAP,8,  64,48, 1);
    run("16bpp-565",  FIT_BITMAP,16, 64,48, 0);
    run("24bpp-rgb",  FIT_BITMAP,24, 64,48, 1);
    run("32bpp-rgba", FIT_BITMAP,32, 64,48, 1);
    run("uint16",     FIT_UINT16,16, 64,48, 1);
    run("float",      FIT_FLOAT, 32, 64,48, 0);
    run("rgb16",      FIT_RGB16, 48, 64,48, 1);
    run("rgba16",     FIT_RGBA16,64, 64,48, 1);
    run("rgbf",       FIT_RGBF,  96, 64,48, 0);
    run("rgbaf",      FIT_RGBAF,128,64,48, 0);
    run("24bpp-odd",  FIT_BITMAP,24, 67,53, 1);   /* pitch padding 1 */
    run("24bpp-odd2", FIT_BITMAP,24, 66,53, 1);   /* pitch padding 2 */
    run("8bpp-odd",   FIT_BITMAP,8,  67,53, 1);
    printf("--- small sizes (refused outright before the 16x16 floor was lifted) ---\n");
    run("24bpp-tiny", FIT_BITMAP,24, 8,8, 1);
    run("24bpp-15",   FIT_BITMAP,24, 15,15, 1);
    run("32bpp-1x1",  FIT_BITMAP,32, 1,1, 1);
    FreeImage_DeInitialise(); return 0;
}
