// ==========================================================
// FreeImage 3 Test Script
//
// Design and implementation by
// - Hervé Drolon (drolon@infonie.fr)
//
// This file is part of FreeImage 3
//
// COVERED CODE IS PROVIDED UNDER THIS LICENSE ON AN "AS IS" BASIS, WITHOUT WARRANTY
// OF ANY KIND, EITHER EXPRESSED OR IMPLIED, INCLUDING, WITHOUT LIMITATION, WARRANTIES
// THAT THE COVERED CODE IS FREE OF DEFECTS, MERCHANTABLE, FIT FOR A PARTICULAR PURPOSE
// OR NON-INFRINGING. THE ENTIRE RISK AS TO THE QUALITY AND PERFORMANCE OF THE COVERED
// CODE IS WITH YOU. SHOULD ANY COVERED CODE PROVE DEFECTIVE IN ANY RESPECT, YOU (NOT
// THE INITIAL DEVELOPER OR ANY OTHER CONTRIBUTOR) ASSUME THE COST OF ANY NECESSARY
// SERVICING, REPAIR OR CORRECTION. THIS DISCLAIMER OF WARRANTY CONSTITUTES AN ESSENTIAL
// PART OF THIS LICENSE. NO USE OF ANY COVERED CODE IS AUTHORIZED HEREUNDER EXCEPT UNDER
// THIS DISCLAIMER.
//
// Use at your own risk!
// ==========================================================


#include "TestSuite.h"

#include <new>
#include <string>
#include <string.h>
#ifdef _WIN32
#include <io.h>		// _findfirst
#else
#include <glob.h>
#endif

// Show plugins
// ----------------------------------------------------------
void showPlugins() {
	// print version & copyright infos

	printf("FreeImage version: %s\n\n%s\n\n", FreeImage_GetVersion(), FreeImage_GetCopyrightMessage());

	// print plugins info

	for (int j = FreeImage_GetFIFCount() - 1; j >= 0; --j)
		printf("bitmap type %d (%s): %s (%s)\n", j, FreeImage_GetFormatFromFIF((FREE_IMAGE_FORMAT)j), FreeImage_GetFIFDescription((FREE_IMAGE_FORMAT)j), FreeImage_GetFIFExtensionList((FREE_IMAGE_FORMAT)j));
	printf("\n");
}

// A plugin's close decides the result of a save
// ----------------------------------------------------------
static BOOL s_close_result = TRUE;
static int s_opens = 0;
static int s_closes = 0;

static void * DLL_CALLCONV
TestOpen(FreeImageIO *io, fi_handle handle, BOOL read) {
	s_opens++;
	return &s_opens;
}

static BOOL DLL_CALLCONV
TestCloseEx(FreeImageIO *io, fi_handle handle, void *data) {
	s_closes++;
	return s_close_result;
}

static void DLL_CALLCONV
TestClose(FreeImageIO *io, fi_handle handle, void *data) {
	s_closes++;
}

static int DLL_CALLCONV
TestPageCount(FreeImageIO *io, fi_handle handle, void *data) {
	return 1;
}

static BOOL DLL_CALLCONV
TestSave(FreeImageIO *io, FIBITMAP *dib, fi_handle handle, int page, int flags, void *data) {
	BYTE b = 0;
	return (io->write_proc(&b, 1, 1, handle) == 1) ? TRUE : FALSE;
}

static void DLL_CALLCONV
InitCloseEx(Plugin *plugin, int format_id) {
	plugin->open_proc = TestOpen;
	plugin->close_ex_proc = TestCloseEx;
	plugin->pagecount_proc = TestPageCount;
	plugin->save_proc = TestSave;
}

static void DLL_CALLCONV
InitCloseVoid(Plugin *plugin, int format_id) {
	plugin->open_proc = TestOpen;
	plugin->close_proc = TestClose;
	plugin->pagecount_proc = TestPageCount;
	plugin->save_proc = TestSave;
}

// a close_ex_proc returning FALSE fails the save; a void close_proc cannot
void testPluginCloseResult() {
	printf("testPluginCloseResult ...\n");

	const FREE_IMAGE_FORMAT fif_ex = FreeImage_RegisterLocalPlugin(InitCloseEx, "CLOSEEX", "close_ex_proc test", "closeex", NULL);
	const FREE_IMAGE_FORMAT fif_void = FreeImage_RegisterLocalPlugin(InitCloseVoid, "CLOSEVOID", "close_proc test", "closevoid", NULL);
	assert((fif_ex != FIF_UNKNOWN) && (fif_void != FIF_UNKNOWN));

	FIBITMAP *dib = FreeImage_Allocate(8, 8, 24);
	assert(dib != NULL);
	FIMEMORY *hmem = FreeImage_OpenMemory();
	assert(hmem != NULL);

	s_close_result = TRUE;
	BOOL bResult = FreeImage_SaveToMemory(fif_ex, dib, hmem, 0);
	assert(bResult);
	s_close_result = FALSE;
	bResult = FreeImage_SaveToMemory(fif_ex, dib, hmem, 0);
	assert(!bResult);
	bResult = FreeImage_SaveToMemory(fif_void, dib, hmem, 0);
	assert(bResult);

	// pages from a TIFF, written by the test plugins
	FIMULTIBITMAP *mpage = FreeImage_OpenMultiBitmap(FIF_TIFF, "close-result.tif", TRUE, FALSE, TRUE);
	assert(mpage != NULL);
	bResult = FreeImage_AppendPage(mpage, dib);
	assert(bResult);
	bResult = FreeImage_AppendPage(mpage, dib);
	assert(bResult);
	s_close_result = TRUE;
	bResult = FreeImage_SaveMultiBitmapToMemory(fif_ex, mpage, hmem, 0);
	assert(bResult);
	s_close_result = FALSE;
	bResult = FreeImage_SaveMultiBitmapToMemory(fif_ex, mpage, hmem, 0);
	assert(!bResult);
	bResult = FreeImage_SaveMultiBitmapToMemory(fif_void, mpage, hmem, 0);
	assert(bResult);
	bResult = FreeImage_CloseMultiBitmap(mpage, 0);
	assert(bResult);
	remove("close-result.tif");

	// every open was closed
	assert(s_opens == s_closes);

	FreeImage_CloseMemory(hmem);
	FreeImage_Unload(dib);

	// they stay registered: keep them away from other tests
	FreeImage_SetPluginEnabled(fif_ex, FALSE);
	FreeImage_SetPluginEnabled(fif_void, FALSE);
}

// A plugin out of memory fails the save
// ----------------------------------------------------------
// a load throws too while this is set
static BOOL s_throw_load = FALSE;

static FIBITMAP * DLL_CALLCONV
TestLoad(FreeImageIO *io, fi_handle handle, int page, int flags, void *data) {
	if (s_throw_load) {
		throw std::bad_alloc();
	}
	return FreeImage_Allocate(8, 8, 24);
}

// the page that throws, as a plugin out of memory does; -1 is a single-image save
static int s_throw_page = 1;

static BOOL DLL_CALLCONV
TestSaveThrows(FreeImageIO *io, FIBITMAP *dib, fi_handle handle, int page, int flags, void *data) {
	if (page == s_throw_page) {
		throw std::bad_alloc();
	}
	return TestSave(io, dib, handle, page, flags, data);
}

static void DLL_CALLCONV
InitThrows(Plugin *plugin, int format_id) {
	plugin->open_proc = TestOpen;
	plugin->close_proc = TestClose;
	plugin->pagecount_proc = TestPageCount;
	plugin->load_proc = TestLoad;
	plugin->save_proc = TestSaveThrows;
}

// one image a file: a page lock opens and closes the plugin itself
static void DLL_CALLCONV
InitThrowsSingle(Plugin *plugin, int format_id) {
	plugin->open_proc = TestOpen;
	plugin->close_proc = TestClose;
	plugin->load_proc = TestLoad;
}

// a file matching the pattern exists
static BOOL fileMatches(const char *pattern) {
#ifdef _WIN32
	struct _finddata_t found;
	const intptr_t hFind = _findfirst(pattern, &found);
	if (hFind == -1) {
		return FALSE;
	}
	_findclose(hFind);
	return TRUE;
#else
	glob_t found;
	if (glob(pattern, 0, NULL, &found) != 0) {
		return FALSE;
	}
	globfree(&found);
	return TRUE;
#endif
}

// nothing escapes a save or load whose plugin throws; the files close and nothing is left behind
void testPluginOutOfMemory() {
	printf("testPluginOutOfMemory ...\n");

	const FREE_IMAGE_FORMAT fif = FreeImage_RegisterLocalPlugin(InitThrows, "THROWS", "out of memory test", "throws", NULL);
	assert(fif != FIF_UNKNOWN);

	FIBITMAP *dib = FreeImage_Allocate(8, 8, 24);
	assert(dib != NULL);

	// pages from a TIFF, written by the test plugin
	FIMULTIBITMAP *mpage = FreeImage_OpenMultiBitmap(FIF_TIFF, "out-of-memory.tif", TRUE, FALSE, TRUE);
	assert(mpage != NULL);
	BOOL bResult = FreeImage_AppendPage(mpage, dib);
	assert(bResult);
	bResult = FreeImage_AppendPage(mpage, dib);
	assert(bResult);
	FIMEMORY *hmem = FreeImage_OpenMemory();
	assert(hmem != NULL);
	bResult = FreeImage_SaveMultiBitmapToMemory(fif, mpage, hmem, 0);
	assert(!bResult);
	FreeImage_CloseMemory(hmem);
	bResult = FreeImage_CloseMultiBitmap(mpage, 0);
	assert(bResult);
	remove("out-of-memory.tif");

	// a document of the test plugin
	mpage = FreeImage_OpenMultiBitmap(fif, "out-of-memory.throws", TRUE, FALSE, TRUE);
	assert(mpage != NULL);
	bResult = FreeImage_AppendPage(mpage, dib);
	assert(bResult);
	bResult = FreeImage_AppendPage(mpage, dib);
	assert(bResult);
	bResult = FreeImage_CloseMultiBitmap(mpage, 0);
	assert(!bResult);
	assert(!fileMatches("out-of-memory.throws"));
	assert(!fileMatches("out-of-memory.throws.*.fispool"));

	// a single-image save
	s_throw_page = -1;
	hmem = FreeImage_OpenMemory();
	assert(hmem != NULL);
	bResult = FreeImage_SaveToMemory(fif, dib, hmem, 0);
	assert(!bResult);
	FreeImage_CloseMemory(hmem);
	bResult = FreeImage_Save(fif, dib, "out-of-memory.throws", 0);
	assert(!bResult);
	assert(!fileMatches("out-of-memory.throws"));
	s_throw_page = 1;

	// loads, and a page of a document
	s_throw_load = TRUE;
	hmem = FreeImage_OpenMemory();
	assert(hmem != NULL);
	FIBITMAP *loaded = FreeImage_LoadFromMemory(fif, hmem, 0);
	assert(loaded == NULL);
	FreeImage_CloseMemory(hmem);
	FILE *file = fopen("out-of-memory.throws", "wb");
	assert(file != NULL);
	fputc(0, file);
	fclose(file);
	mpage = FreeImage_OpenMultiBitmap(fif, "out-of-memory.throws", FALSE, TRUE, TRUE);
	assert(mpage != NULL);
	loaded = FreeImage_LockPage(mpage, 0);
	assert(loaded == NULL);
	bResult = FreeImage_CloseMultiBitmap(mpage, 0);
	assert(bResult);
	const FREE_IMAGE_FORMAT fif_single = FreeImage_RegisterLocalPlugin(InitThrowsSingle, "THROWS1", "out of memory test, one image", "throws1", NULL);
	assert(fif_single != FIF_UNKNOWN);
	mpage = FreeImage_OpenMultiBitmap(fif_single, "out-of-memory.throws", FALSE, TRUE, TRUE);
	assert(mpage != NULL);
	loaded = FreeImage_LockPage(mpage, 0);
	assert(loaded == NULL);
	bResult = FreeImage_CloseMultiBitmap(mpage, 0);
	assert(bResult);
	FreeImage_SetPluginEnabled(fif_single, FALSE);
	remove("out-of-memory.throws");
	s_throw_load = FALSE;

	// every open was closed
	assert(s_opens == s_closes);

	FreeImage_Unload(dib);

	// it stays registered: keep it away from other tests
	FreeImage_SetPluginEnabled(fif, FALSE);
}

// Text from a file in a message
// ----------------------------------------------------------
static const char *s_file_text = "%s%s%s%s%s%s%s%s";
static BOOL s_file_text_seen = FALSE;

static void DLL_CALLCONV
FileTextHandler(FREE_IMAGE_FORMAT fif, const char *message) {
	if (strstr(message, s_file_text) != NULL) {
		s_file_text_seen = TRUE;
	}
}

static void put32(std::string& s, unsigned v) {
	for (int i = 0; i < 4; i++) {
		s += (char)((v >> (8 * i)) & 0xFF);
	}
}

static void putAttribute(std::string& s, const char *name, const char *type, const std::string& value) {
	s.append(name, strlen(name) + 1);
	s.append(type, strlen(type) + 1);
	put32(s, (unsigned)value.size());
	s += value;
}

// a 4 x 1 uncompressed OpenEXR file; type 1 is HALF, 2 is FLOAT; names sorted
static FIMEMORY* makeEXR(const char *const names[], const int types[], int count) {
	std::string channels, box, data;
	for (int i = 0; i < count; i++) {
		channels.append(names[i], strlen(names[i]) + 1);
		put32(channels, types[i]);
		put32(channels, 0);
		put32(channels, 1);
		put32(channels, 1);
		data.append(4 * (types[i] == 1 ? 2 : 4), '\0');
	}
	channels += '\0';
	put32(box, 0); put32(box, 0); put32(box, 3); put32(box, 0);

	std::string exr("\x76\x2f\x31\x01\x02\x00\x00\x00", 8);
	putAttribute(exr, "channels", "chlist", channels);
	putAttribute(exr, "compression", "compression", std::string(1, '\0'));
	putAttribute(exr, "dataWindow", "box2i", box);
	putAttribute(exr, "displayWindow", "box2i", box);
	putAttribute(exr, "lineOrder", "lineOrder", std::string(1, '\0'));
	putAttribute(exr, "pixelAspectRatio", "float", std::string("\x00\x00\x80\x3f", 4));
	putAttribute(exr, "screenWindowCenter", "v2f", std::string(8, '\0'));
	putAttribute(exr, "screenWindowWidth", "float", std::string("\x00\x00\x80\x3f", 4));
	exr += '\0';
	const unsigned chunk = (unsigned)exr.size() + 8;
	put32(exr, chunk);
	put32(exr, 0);
	put32(exr, 0);
	put32(exr, (unsigned)data.size());
	exr += data;

	FIMEMORY *hmem = FreeImage_OpenMemory();
	FreeImage_WriteMemory(exr.data(), 1, (unsigned)exr.size(), hmem);
	FreeImage_SeekMemory(hmem, 0, SEEK_SET);
	return hmem;
}

// a color or channel name holding "%s" reaches the message as text, not as a format
void testMessageFileText() {
	printf("testMessageFileText ...\n");

	FreeImage_SetOutputMessage(FileTextHandler);

	// an unknown XPM color name
	std::string xpm = "/* XPM */\nstatic char *t[] = {\n\"4 2 2 1\",\n\"  c #FF0000\",\n\". c ";
	xpm += s_file_text;
	xpm += "\",\n\"  ..\",\n\".. \"};\n";
	FIMEMORY *hmem = FreeImage_OpenMemory((BYTE*)&xpm[0], (DWORD)xpm.size());
	s_file_text_seen = FALSE;
	FIBITMAP *dib = FreeImage_LoadFromMemory(FIF_XPM, hmem, 0);
	assert((dib == NULL) && s_file_text_seen);
	FreeImage_CloseMemory(hmem);

	// a single EXR channel, loaded as Y with a warning naming it
	const char *one[] = { s_file_text };
	const int one_type[] = { 2 };
	hmem = makeEXR(one, one_type, 1);
	s_file_text_seen = FALSE;
	dib = FreeImage_LoadFromMemory(FIF_EXR, hmem, 0);
	assert((dib != NULL) && s_file_text_seen);
	FreeImage_Unload(dib);
	FreeImage_CloseMemory(hmem);

	// EXR channels of two types, refused by an OpenEXR exception naming them
	const char *mixed[] = { s_file_text, "Z" };
	const int mixed_types[] = { 2, 1 };
	hmem = makeEXR(mixed, mixed_types, 2);
	s_file_text_seen = FALSE;
	dib = FreeImage_LoadFromMemory(FIF_EXR, hmem, 0);
	assert((dib == NULL) && s_file_text_seen);
	FreeImage_CloseMemory(hmem);

	FreeImage_SetOutputMessage(FreeImageErrorHandler);
}

// Raw G3 fax
// ----------------------------------------------------------

// bits MSB first; T.4 codes as { length, code }
struct G3Bits {
	std::string bytes;
	unsigned used;
	G3Bits() : used(0) {}
	void put(const unsigned code[2]) {
		for (int i = (int)code[0] - 1; i >= 0; i--) {
			if (used == 0) {
				bytes += '\0';
			}
			if ((code[1] >> i) & 1) {
				bytes[bytes.size() - 1] |= (char)(0x80 >> used);
			}
			used = (used + 1) & 7;
		}
	}
};

static const unsigned G3_EOL[2] = { 12, 0x001 };
static const unsigned G3_W0[2] = { 8, 0x35 }, G3_W32[2] = { 8, 0x1B }, G3_W832[2] = { 9, 0xD2 }, G3_W1728[2] = { 9, 0x9B };
static const unsigned G3_B0[2] = { 10, 0x37 }, G3_B32[2] = { 12, 0x6A }, G3_B832[2] = { 13, 0x4D }, G3_B1728[2] = { 13, 0x65 };

// rows of 1728 pixels: 'w' white, 'b' black, 'h' white then black halves
static G3Bits g3Rows(const char *rows, BOOL eol) {
	G3Bits b;
	for (const char *r = rows; *r; r++) {
		if (eol) {
			b.put(G3_EOL);
		}
		if (*r == 'w') {
			b.put(G3_W1728); b.put(G3_W0);
		} else if (*r == 'b') {
			b.put(G3_W0); b.put(G3_B1728); b.put(G3_B0);
		} else {
			b.put(G3_W832); b.put(G3_W32); b.put(G3_B832); b.put(G3_B32);
		}
	}
	return b;
}

// the number of rows a stream loads as: the given rows, then white ones
static unsigned g3Load(const std::string& data, const char *rows) {
	FIMEMORY *hmem = FreeImage_OpenMemory((BYTE*)&data[0], (DWORD)data.size());
	FIBITMAP *dib = FreeImage_LoadFromMemory(FIF_FAXG3, hmem, 0);
	FreeImage_CloseMemory(hmem);
	assert(dib != NULL);
	const unsigned height = FreeImage_GetHeight(dib);
	const unsigned count = (unsigned)strlen(rows);
	for (unsigned y = 0; y < height; y++) {
		const BYTE *line = FreeImage_GetScanLine(dib, height - 1 - y);
		for (unsigned x = 0; x < 1728; x++) {
			const int black = (line[x >> 3] >> (7 - (x & 7))) & 1;
			const int want = (y < count) && ((rows[y] == 'b') || ((rows[y] == 'h') && (x >= 864)));
			assert(black == want);
		}
	}
	FreeImage_Unload(dib);
	return height;
}

// a page ends where its EOLs do, as an RTC or the bytes after the last EOL may have it
void testFaxG3() {
	printf("testFaxG3 ...\n");

	const char *rows = "wbhhbw";
	const std::string page = g3Rows(rows, TRUE).bytes;
	G3Bits rtc;
	for (int i = 0; i < 6; i++) {
		rtc.put(G3_EOL);
	}
	const std::string zeros(16, '\0');
	// the last ten EOLs of libtiff's g3test.g3 and the bytes after them
	const std::string g3test_end("\x00\x08\x00\x80\x08\x00\x80\x08\x00\x80\x08\x00\x80\x08\x00\x80\x3f\xff\xff\xf8\xff\x00\x00", 23);

	assert(g3Load(page, rows) == 6);
	assert(g3Load(page + rtc.bytes, rows) == 11);
	assert(g3Load(page + rtc.bytes + zeros, rows) == 13);
	assert(g3Load(page + zeros, rows) == 7);
	assert(g3Load(page + g3test_end.substr(16), rows) == 7);
	assert(g3Load(page + g3test_end, rows) == 17);

	// rows without EOLs, then a code word the decoder cannot pass
	G3Bits bare = g3Rows(rows, FALSE);
	assert(g3Load(bare.bytes, rows) == 6);
	static const unsigned BAD[2] = { 12, 0x007 }, ONES[2] = { 8, 0xFF };
	bare.put(BAD);
	bare.put(ONES);
	assert(g3Load(bare.bytes, rows) == 8);
}

// Sun raster
// ----------------------------------------------------------

// a 5 x 3 Sun raster; pixel bytes under 0x80 read the same byte-encoded (type 2)
static std::string rasFile(unsigned depth, unsigned type, unsigned maptype, unsigned maplength) {
	const unsigned header[8] = { 0x59A66A95, 5, 3, depth, 0, type, maptype, maplength };
	std::string s;
	for (int i = 0; i < 8; i++) {
		for (int k = 3; k >= 0; k--) {
			s += (char)((header[i] >> (8 * k)) & 0xFF);
		}
	}
	for (unsigned i = 0; i < maplength; i++) {
		s += (char)(i * 7);
	}
	const unsigned line = 5 * depth / 8;
	for (unsigned y = 0; y < 3; y++) {
		for (unsigned x = 0; x < line; x++) {
			s += (char)((x * 13 + y * 31 + 1) & 0x7F);
		}
		if (line & 1) {
			s += '\0';
		}
	}
	return s;
}

static FIBITMAP* rasLoad(const std::string& data, int flags) {
	FIMEMORY *hmem = FreeImage_OpenMemory((BYTE*)&data[0], (DWORD)data.size());
	FIBITMAP *dib = FreeImage_LoadFromMemory(FIF_RAS, hmem, flags);
	FreeImage_CloseMemory(hmem);
	return dib;
}

// 24- and 32-bit rasters skip a colormap: maptype 1 loads as maptype 0
void testSunRaster() {
	printf("testSunRaster ...\n");

	const unsigned depths[2] = { 24, 32 };
	const unsigned maplengths[3] = { 0, 6, 768 };
	for (int d = 0; d < 2; d++) {
		FIBITMAP *ref = rasLoad(rasFile(depths[d], 1, 0, 0), 0);
		assert(ref != NULL);
		for (int m = 0; m < 3; m++) {
			for (unsigned type = 1; type <= 2; type++) {
				const std::string file = rasFile(depths[d], type, 1, maplengths[m]);
				FIBITMAP *dib = rasLoad(file, 0);
				assert(dib != NULL && FreeImage_GetBPP(dib) == depths[d]);
				for (unsigned y = 0; y < 3; y++) {
					assert(memcmp(FreeImage_GetScanLine(dib, y), FreeImage_GetScanLine(ref, y), FreeImage_GetLine(ref)) == 0);
				}
				FreeImage_Unload(dib);
				FIBITMAP *header = rasLoad(file, FIF_LOAD_NOPIXELS);
				assert(header != NULL && !FreeImage_HasPixels(header) && FreeImage_GetBPP(header) == depths[d] && FreeImage_GetWidth(header) == 5);
				FreeImage_Unload(header);
			}
		}
		FreeImage_Unload(ref);
	}
}
