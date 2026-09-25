/* FreeImage 3 - JPEG XR test: a round trip through the caller's own FreeImageIO */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "FreeImage.h"

static const char *tmppath(const char *name) {
    static char buf[1024];
    const char *dir = getenv("JXR_TEST_TMP");
    snprintf(buf, sizeof(buf), "%s/%s", (dir && *dir) ? dir : ".", name);
    return buf;
}

/* 64-bit positions, as the callbacks carry them */
static unsigned DLL_CALLCONV nRead(void*b,unsigned s,unsigned c,fi_handle h){
    return (unsigned)fread(b,s,c,(FILE*)h); }
static unsigned DLL_CALLCONV nWrite(void*b,unsigned s,unsigned c,fi_handle h){
    return (unsigned)fwrite(b,s,c,(FILE*)h); }
static int DLL_CALLCONV nSeek(fi_handle h,INT64 off,int origin){
    return fseeko((FILE*)h,(off_t)off,origin); }
static INT64 DLL_CALLCONV nTell(fi_handle h){
    return (INT64)ftello((FILE*)h); }

int main(void){
    FreeImageIO io; FIBITMAP *d,*b; FILE *f; int ok; long sz;
    const char *p = tmppath("fi_jxr_narrow.jxr");
    unsigned y,x,s=99991u;
    io.read_proc=nRead; io.write_proc=nWrite; io.seek_proc=nSeek; io.tell_proc=nTell;

    FreeImage_Initialise(FALSE);
    d=FreeImage_Allocate(256,256,32,0,0,0);
    for(y=0;y<256;y++){ BYTE*q=FreeImage_GetScanLine(d,y);
        for(x=0;x<256*4;x++){ s^=s<<13; s^=s>>17; s^=s<<5; q[x]=(BYTE)(s>>7); } }

    printf("A 256x256 32bpp JXR is ~280 KB, saved and reloaded through a FreeImageIO of our own.\n\n");

    f=fopen(p,"w+b");
    ok=FreeImage_SaveToHandle(FIF_JXR,d,&io,(fi_handle)f,JXR_LOSSLESS);
    fseek(f,0,SEEK_END); sz=ftell(f); fclose(f);
    printf("save   -> %-8s  %ld bytes written\n", ok?"ok":"FAILED", sz);

    f=fopen(p,"rb");
    b=FreeImage_LoadFromHandle(FIF_JXR,&io,(fi_handle)f,0);
    fclose(f);
    printf("reload -> %-8s\n", b?"ok":"FAILED");
    if(b){
        int bad=0; s=99991u;
        for(y=0;y<256 && !bad;y++){ BYTE*q=FreeImage_GetScanLine(b,y);
            for(x=0;x<256*4;x++){ BYTE e; s^=s<<13; s^=s>>17; s^=s<<5; e=(BYTE)(s>>7);
                if(q[x]!=e){bad=1;break;} } }
        printf("pixels -> %s\n", bad?"MISMATCH":"exact");
        FreeImage_Unload(b);
        if(bad) ok=0;
    }
    FreeImage_Unload(d);
    FreeImage_DeInitialise();
    return (ok && b)?0:1;
}
