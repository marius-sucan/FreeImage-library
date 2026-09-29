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
	remove("out-of-memory.throws");
	s_throw_load = FALSE;

	// every open was closed
	assert(s_opens == s_closes);

	FreeImage_Unload(dib);

	// it stays registered: keep it away from other tests
	FreeImage_SetPluginEnabled(fif, FALSE);
}
