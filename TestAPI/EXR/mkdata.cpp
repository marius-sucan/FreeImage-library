/* Generates the TestAPI/EXR corpus: one small file per compression method plus
   the layouts FreeImage itself never writes (tiled, mipmapped, float, YC). */
#include <ImfRgbaFile.h>
#include <ImfTiledRgbaFile.h>
#include <ImfOutputFile.h>
#include <ImfChannelList.h>
#include <ImfFrameBuffer.h>
#include <ImfHeader.h>
#include <ImfArray.h>
#include <half.h>
#include <cstdio>
#include <cmath>
#include <string>
#include <vector>
using namespace Imf;

static const int W = 64, H = 48;

static void fill(Array2D<Rgba> &px) {
    px.resizeErase(H, W);
    for (int y = 0; y < H; y++)
        for (int x = 0; x < W; x++) {
            float u = x / float(W), v = y / float(H);
            px[y][x] = Rgba(half(u * 3.0f), half(v * v * 1.5f),
                            half(std::sin((u + v) * 9.0f) * 0.5f + 0.5f), half(1.0f));
        }
}

int main(int argc, char **argv) {
    std::string dir = argv[1];
    Array2D<Rgba> px; fill(px);
    struct { const char *n; Compression c; } modes[] = {
        {"none", NO_COMPRESSION}, {"rle", RLE_COMPRESSION},
        {"zips", ZIPS_COMPRESSION}, {"zip", ZIP_COMPRESSION},
        {"piz", PIZ_COMPRESSION}, {"pxr24", PXR24_COMPRESSION},
        {"b44", B44_COMPRESSION}, {"b44a", B44A_COMPRESSION},
        {"dwaa", DWAA_COMPRESSION}, {"dwab", DWAB_COMPRESSION},
    };
    for (auto &m : modes) {
        Header h(W, H); h.compression() = m.c;
        RgbaOutputFile o((dir + "/fi_exr_" + m.n + ".exr").c_str(), h, WRITE_RGBA);
        o.setFrameBuffer(&px[0][0], 1, W); o.writePixels(H);
    }
    {   // tiled, one level
        Header h(W, H); h.compression() = ZIP_COMPRESSION;
        TiledRgbaOutputFile o((dir + "/fi_exr_tiled.exr").c_str(), h, WRITE_RGBA, 16, 16, ONE_LEVEL);
        o.setFrameBuffer(&px[0][0], 1, W); o.writeTiles(0, o.numXTiles() - 1, 0, o.numYTiles() - 1);
    }
    {   // tiled, mipmapped
        Header h(W, H); h.compression() = ZIP_COMPRESSION;
        TiledRgbaOutputFile o((dir + "/fi_exr_mipmap.exr").c_str(), h, WRITE_RGBA, 16, 16, MIPMAP_LEVELS);
        for (int l = 0; l < o.numLevels(); l++) {
            o.setFrameBuffer(&px[0][0], 1, W);
            o.writeTiles(0, o.numXTiles(l) - 1, 0, o.numYTiles(l) - 1, l);
        }
    }
    {   // 32-bit float channels
        Header h(W, H); h.compression() = ZIP_COMPRESSION;
        h.channels().insert("R", Channel(Imf::FLOAT));
        h.channels().insert("G", Channel(Imf::FLOAT));
        h.channels().insert("B", Channel(Imf::FLOAT));
        std::vector<float> f(W * H * 3);
        for (int i = 0; i < W * H; i++) {
            f[i*3+0] = float(px[i/W][i%W].r); f[i*3+1] = float(px[i/W][i%W].g);
            f[i*3+2] = float(px[i/W][i%W].b);
        }
        FrameBuffer fb;
        fb.insert("R", Slice(Imf::FLOAT, (char *)&f[0], 12, 12*W));
        fb.insert("G", Slice(Imf::FLOAT, (char *)&f[1], 12, 12*W));
        fb.insert("B", Slice(Imf::FLOAT, (char *)&f[2], 12, 12*W));
        OutputFile o((dir + "/fi_exr_float.exr").c_str(), h);
        o.setFrameBuffer(fb); o.writePixels(H);
    }
    {   // luminance/chroma, chroma subsampled - the RgbaInputFile YC path
        Header h(W, H); h.compression() = PIZ_COMPRESSION;
        RgbaOutputFile o((dir + "/fi_exr_yc.exr").c_str(), h, WRITE_YCA);
        o.setFrameBuffer(&px[0][0], 1, W); o.writePixels(H);
    }
    printf("done\n");
    return 0;
}
