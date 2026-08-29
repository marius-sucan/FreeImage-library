/*
 * FreeImage 3 - JPEG XR regression test
 *
 * Emulates the LLP64 wall that FI_TellProc's 'long' return imposes on Win64:
 * tell_proc cannot report a position past LIMIT, exactly as ftell() cannot
 * report one past LONG_MAX. The plugin must not depend on it.
 *
 * To also exercise the stepped seek in _jxr_io_SetPos - unreachable where 'long'
 * is 64-bit - rebuild PluginJXR.o with -DFI_JXR_SEEK_STEP_MAX=4096; see README.
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

/* Two independent caps, because the plugin's two 'long' dependencies are separate:
   TELL_CAP  - tell_proc cannot report past this. Always on: this is the wall that
               used to abort a >2 GB save, and the fix must not depend on tell at all.
   SEEK_CAP  - a single absolute SEEK_SET cannot reach past this. Off by default,
               because the stepped seek that copes with it is unreachable where
               'long' is 64-bit; pass a value as argv[1] after rebuilding
               PluginJXR.o with a matching -DFI_JXR_SEEK_STEP_MAX (see README). */
static long  TELL_CAP = 4096;
static long  SEEK_CAP = 0;          /* 0 = no cap */
static int   g_tellFailures = 0;
static int   g_seekRefusals = 0;

static unsigned DLL_CALLCONV nRead(void*b,unsigned s,unsigned c,fi_handle h){
    return (unsigned)fread(b,s,c,(FILE*)h); }
static unsigned DLL_CALLCONV nWrite(void*b,unsigned s,unsigned c,fi_handle h){
    return (unsigned)fwrite(b,s,c,(FILE*)h); }
static int DLL_CALLCONV nSeek(fi_handle h,long off,int origin){
    if(origin==SEEK_SET && SEEK_CAP && off>SEEK_CAP){
        g_seekRefusals++; return -1;        /* a 32-bit seek could not express this */
    }
    return fseek((FILE*)h,off,origin); }
static long DLL_CALLCONV nTell(fi_handle h){
    long p=ftell((FILE*)h);
    if(p>TELL_CAP){ g_tellFailures++; return -1L; } /* what ftell does past LONG_MAX */
    return p; }

int main(int argc, char **argv){
    FreeImageIO io; FIBITMAP *d,*b; FILE *f; int ok; long sz;
    const char *p = tmppath("fi_jxr_narrow.jxr");
    unsigned y,x,s=99991u;
    io.read_proc=nRead; io.write_proc=nWrite; io.seek_proc=nSeek; io.tell_proc=nTell;

    FreeImage_Initialise(FALSE);
    d=FreeImage_Allocate(256,256,32,0,0,0);
    for(y=0;y<256;y++){ BYTE*q=FreeImage_GetScanLine(d,y);
        for(x=0;x<256*4;x++){ s^=s<<13; s^=s>>17; s^=s<<5; q[x]=(BYTE)(s>>7); } }

    if(argc > 1) SEEK_CAP = atol(argv[1]);
    printf("A 256x256 32bpp JXR is ~280 KB.\n");
    printf("  tell_proc capped at %ld bytes  (the Win64 wall, scaled down ~500000x)\n", TELL_CAP);
    if(SEEK_CAP)
        printf("  absolute seek capped at %ld bytes - needs PluginJXR.o built with\n"
               "  -DFI_JXR_SEEK_STEP_MAX=%ld, otherwise the reload below cannot pass\n", SEEK_CAP, SEEK_CAP);
    else
        printf("  absolute seek uncapped (pass a cap as argv[1] to exercise the stepped seek)\n");
    printf("\n");

    f=fopen(p,"w+b");
    ok=FreeImage_SaveToHandle(FIF_JXR,d,&io,(fi_handle)f,JXR_LOSSLESS);
    fseek(f,0,SEEK_END); sz=ftell(f); fclose(f);
    printf("save   -> %-8s  %ld bytes written, tell_proc refused %d queries\n",
           ok?"ok":"FAILED", sz, g_tellFailures);

    g_tellFailures=0; g_seekRefusals=0;
    f=fopen(p,"rb");
    b=FreeImage_LoadFromHandle(FIF_JXR,&io,(fi_handle)f,0);
    fclose(f);
    printf("reload -> %-8s  tell_proc refused %d queries, seek_proc refused %d\n",
           b?"ok":"FAILED", g_tellFailures, g_seekRefusals);
    if(b){
        int bad=0; s=99991u;
        for(y=0;y<256 && !bad;y++){ BYTE*q=FreeImage_GetScanLine(b,y);
            for(x=0;x<256*4;x++){ BYTE e; s^=s<<13; s^=s>>17; s^=s<<5; e=(BYTE)(s>>7);
                if(q[x]!=e){bad=1;break;} } }
        printf("pixels -> %s\n", bad?"MISMATCH":"exact");
        FreeImage_Unload(b);
    }
    FreeImage_Unload(d);
    FreeImage_DeInitialise();
    return (ok && b)?0:1;
}
