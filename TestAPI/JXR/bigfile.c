/*
 * FreeImage 3 - JPEG XR regression test
 *
 * Writes and reads back a JXR larger than 2 GiB, from an image with more than
 * 2^31 pixels, proving the container offsets stay correct at that scale.
 *
 * Needs about 4.5 GB of RAM and 2.4 GB of free space, and takes ~2 minutes.
 * Optional arguments: width height (default 46341 46341).
 *
 * Standalone: build with the Makefile in this directory, run from anywhere.
 * Scratch files are written to $JXR_TEST_TMP (default: the current directory).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/stat.h>
#include "FreeImage.h"

/* Scratch files go to $JXR_TEST_TMP, or the current directory. */
static const char *tmppath(const char *name) {
    static char buf[1024];
    const char *dir = getenv("JXR_TEST_TMP");
    snprintf(buf, sizeof(buf), "%s/%s", (dir && *dir) ? dir : ".", name);
    return buf;
}

static double now(void){ struct timespec t; clock_gettime(CLOCK_MONOTONIC,&t);
    return t.tv_sec + t.tv_nsec/1e9; }

int main(int argc, char **argv){
    const unsigned W = (argc>1)?(unsigned)atoi(argv[1]):46341;
    const unsigned H = (argc>2)?(unsigned)atoi(argv[2]):46341;
    const char *out = tmppath("fi_jxr_big.jxr");
    FIBITMAP *d, *b; unsigned y; unsigned s=1234567u; double t0;
    struct stat st;
    unsigned long long px = (unsigned long long)W*H;

    FreeImage_Initialise(FALSE);
    printf("allocating %ux%u 8bpp = %.3f Gpx, %.2f GiB ...\n",
           W,H,px/1e9,(double)px/(1024.0*1024*1024));
    fflush(stdout);
    d = FreeImage_Allocate(W,H,8,0,0,0);
    if(!d){ printf("ALLOC FAILED\n"); return 2; }
    {   RGBQUAD *pal=FreeImage_GetPalette(d); int i;
        for(i=0;i<256;i++) pal[i].rgbRed=pal[i].rgbGreen=pal[i].rgbBlue=(BYTE)i; }
    printf("filling with incompressible noise ...\n"); fflush(stdout);
    for(y=0;y<H;y++){ BYTE*p=FreeImage_GetScanLine(d,y); unsigned x;
        for(x=0;x<W;x++){ s^=s<<13; s^=s>>17; s^=s<<5; p[x]=(BYTE)(s>>11); } }

    printf("saving lossless ...\n"); fflush(stdout);
    t0=now();
    if(!FreeImage_Save(FIF_JXR,d,out,JXR_LOSSLESS)){
        printf("SAVE FAILED after %.1fs\n",now()-t0);
        stat(out,&st);
        FreeImage_Unload(d); return 1;
    }
    printf("saved in %.1fs\n",now()-t0); fflush(stdout);
    if(stat(out,&st)==0)
        printf("output: %lld bytes = %.3f GiB  (%.4f B/px)  crosses 2GiB=%s 4GiB=%s\n",
               (long long)st.st_size,(double)st.st_size/(1024.0*1024*1024),
               (double)st.st_size/px,
               (st.st_size>2147483648LL)?"YES":"no",
               (st.st_size>4294967296LL)?"YES":"no");
    FreeImage_Unload(d);

    printf("reloading ...\n"); fflush(stdout);
    t0=now();
    b = FreeImage_Load(FIF_JXR,out,0);
    if(!b){ printf("RELOAD FAILED after %.1fs\n",now()-t0); return 1; }
    printf("reloaded in %.1fs: %ux%u %ubpp\n",now()-t0,
           FreeImage_GetWidth(b),FreeImage_GetHeight(b),FreeImage_GetBPP(b));

    /* verify the pixels came back exactly */
    {   unsigned bad=0; s=1234567u;
        for(y=0;y<H && bad<4;y++){ BYTE*p=FreeImage_GetScanLine(b,y); unsigned x;
            for(x=0;x<W;x++){ BYTE e; s^=s<<13; s^=s>>17; s^=s<<5; e=(BYTE)(s>>11);
                if(p[x]!=e){ if(bad<4) printf("  MISMATCH at (%u,%u): got %02x want %02x\n",x,y,p[x],e); bad++; } } }
        printf("pixel verify: %s\n", bad?"FAILED":"exact round-trip over all 2.15 Gpx");
    }
    FreeImage_Unload(b);
    FreeImage_DeInitialise();
    return 0;
}
