/* FreeImage 3 - JPEG XR edge cases: tiny images, JXR_PROGRESSIVE; every save in a pixel format of T.832 Table A.6, CMYK in a CMYK one */
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
/* the last byte of a saved file's T.832 Table A.6 PIXEL_FORMAT, else -1: they all share these 15 bytes, WIC's do not */
static int t832_format(const char *path){
    static const BYTE A6[15]={0x24,0xC3,0xDD,0x6F,0x03,0x4E,0xFE,0x4B,0xB1,0x85,0x3D,0x77,0x76,0x8D,0xC9};
    BYTE b[16]; unsigned ifd,n,i,off=0; int fmt=-1;
    FILE *f=fopen(path,"rb");
    if(!f) return -1;
    if(fseek(f,4,SEEK_SET)==0 && fread(b,1,4,f)==4){
        ifd=b[0]|(b[1]<<8)|(b[2]<<16)|((unsigned)b[3]<<24);
        if(fseek(f,(long)ifd,SEEK_SET)==0 && fread(b,1,2,f)==2){
            n=b[0]|(b[1]<<8);
            for(i=0;i<n && fread(b,1,12,f)==12;i++){
                if((b[0]|(b[1]<<8))==0xBC01){ off=b[8]|(b[9]<<8)|(b[10]<<16)|((unsigned)b[11]<<24); break; }
            }
        }
    }
    if(off && fseek(f,(long)off,SEEK_SET)==0 && fread(b,1,16,f)==16 && !memcmp(b,A6,15)) fmt=b[15];
    fclose(f);
    return fmt;
}
/* CMYK goes out as 32bppCMYK or 64bppCMYK and comes back as CMYK with JXR_CMYK */
static int rt(FREE_IMAGE_TYPE t,int bpp,int cmyk,int w,int h,int flags){
    FIBITMAP *d=mk(t,w,h,bpp), *b; int ok, fmt=-1;
    if(!d) return -1;
    if(cmyk) FreeImage_GetICCProfile(d)->flags|=FIICC_COLOR_IS_CMYK;
    ok = FreeImage_Save(FIF_JXR,d,OUT,flags) && (fmt=t832_format(OUT))>=0;
    if(ok && cmyk) ok = (fmt==((bpp==32)?0x1C:0x1F));
    if(ok){ b=FreeImage_Load(FIF_JXR,OUT,cmyk?JXR_CMYK:0);
            ok = b && FreeImage_GetWidth(b)==(unsigned)w && FreeImage_GetHeight(b)==(unsigned)h &&
                 FreeImage_GetImageType(b)==t && FreeImage_GetBPP(b)==(unsigned)bpp &&
                 (!cmyk || FreeImage_GetColorType(b)==FIC_CMYK);
            if(b) FreeImage_Unload(b); }
    FreeImage_Unload(d);
    return ok?1:0;
}
int main(void){
    static const struct{const char*n;FREE_IMAGE_TYPE t;int bpp;int cmyk;} F[]={
        {"1bpp",FIT_BITMAP,1},{"8bpp",FIT_BITMAP,8},{"16bpp",FIT_BITMAP,16},
        {"24bpp",FIT_BITMAP,24},{"32bpp",FIT_BITMAP,32},{"uint16",FIT_UINT16,16},
        {"float",FIT_FLOAT,32},{"rgb16",FIT_RGB16,48},{"rgba16",FIT_RGBA16,64},
        {"rgbf",FIT_RGBF,96},{"rgbaf",FIT_RGBAF,128},
        {"cmyk32",FIT_BITMAP,32,1},{"cmyk64",FIT_RGBA16,64,1}};
    static const struct{const char*n;int f;} Q[]={{"q10(YUV420,OL2)",10},{"q49(YUV420)",49},
        {"q50(YUV444)",50},{"default",0},{"lossless",JXR_LOSSLESS}};
    static const int WH[][2]={{1,1},{2,3},{7,7},{8,16},{15,15},{16,1},{1,16},{17,33}};
    int i,q,s,fails=0,total=0;
    FreeImage_Initialise(FALSE);
    snprintf(OUT, sizeof(OUT), "%s", tmppath("fi_jxr_edge.jxr"));
    printf("=== tiny images x depth x quality (round-trip) ===\n");
    for(q=0;q<5;q++){
        printf("%-18s ",Q[q].n);
        for(i=0;i<(int)(sizeof(F)/sizeof(F[0]));i++) for(s=0;s<8;s++){
            int r=rt(F[i].t,F[i].bpp,F[i].cmyk,WH[s][0],WH[s][1],Q[q].f);
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
    {   /* T.832 has no float CMYK */
        FIBITMAP *d=mk(FIT_RGBAF,16,16,128);
        char p[1024]; snprintf(p, sizeof(p), "%s", tmppath("fi_jxr_refused.jxr"));
        FILE *f;
        int ok;
        FreeImage_GetICCProfile(d)->flags|=FIICC_COLOR_IS_CMYK;
        ok=FreeImage_Save(FIF_JXR,d,p,JXR_LOSSLESS);
        f=fopen(p,"rb");
        printf("  float CMYK: save=%s  file left on disk=%s\n", ok?"ok (bad)":"refused", f?"YES (bad)":"no (good)");
        if(ok||f) fails++;
        if(f) fclose(f);
        FreeImage_Unload(d);
    }
    FreeImage_DeInitialise();
    return fails?1:0;
}
