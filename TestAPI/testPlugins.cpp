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
