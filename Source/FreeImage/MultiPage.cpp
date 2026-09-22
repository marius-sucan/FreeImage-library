// ==========================================================
// Multi-Page functions
//
// Design and implementation by
// - Floris van den Berg (flvdberg@wxs.nl)
// - Laurent Rocher (rocherl@club-internet.fr)
// - Steve Johnson (steve@parisgroup.net)
// - Petr Pytelka (pyta@lightcomp.com)
// - Hervé Drolon (drolon@infonie.fr)
// - Vadim Alexandrov (vadimalexandrov@users.sourceforge.net
// - Martin Dyring-Andersen (mda@spamfighter.com)
// - Volodymyr Goncharov (volodymyr.goncharov@gmail.com)
// - Mihail Naydenov (mnaydenov@users.sourceforge.net)
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

#ifdef _MSC_VER 
#pragma warning (disable : 4786) // identifier was truncated to 'number' characters
#endif

#include "CacheFile.h"
#include "FreeImageIO.h"
#include "Plugin.h"
#include "Utilities.h"
#include "FreeImage.h"

// the cache and spool files are named after the process that owns them
#ifdef _WIN32
#include <process.h>
#define FI_GetProcessId() _getpid()
#else
#include <unistd.h>
#define FI_GetProcessId() getpid()
#endif

namespace {

// ----------------------------------------------------------

enum BlockType { BLOCK_CONTINUEUS, BLOCK_REFERENCE };

// ----------------------------------------------------------

class PageBlock {

  union {
    struct {
      int  m_start;
      int  m_end;
    };
    struct {
      int  m_reference;
      int  m_size;
    };
  };

public:
  BlockType m_type;

  PageBlock(BlockType type = BLOCK_CONTINUEUS, int val1 = -1, int val2 = -1) : m_type(type)
  {
    if(m_type == BLOCK_CONTINUEUS)
    {
      m_start = val1;
      m_end = val2;
    }
    else
    {
      m_reference = val1;
      m_size = val2;
    }
  }

  bool isValid() const { return !(m_type == BLOCK_CONTINUEUS && m_start == -1 && m_end == -1); }
  /*explicit*/ operator bool() const { return isValid(); }

  int getStart() const { assert(isValid() && m_type == BLOCK_CONTINUEUS); return m_start; }
  int getEnd() const { assert(isValid() && m_type == BLOCK_CONTINUEUS); return m_end; }

  bool isSinglePage() const { assert(isValid()); return m_type == BLOCK_CONTINUEUS ? (m_start == m_end) : true; }
  int getPageCount() const { assert(isValid()); return m_type == BLOCK_CONTINUEUS ? (m_end - m_start + 1) : 1;}

  int getReference() const { assert(isValid() && m_type == BLOCK_REFERENCE); return m_reference; }
  int getSize() const { assert(isValid() && m_type == BLOCK_REFERENCE); return m_size;  }
};

// ----------------------------------------------------------

typedef std::list<PageBlock> BlockList;
typedef BlockList::iterator BlockListIterator;

// ----------------------------------------------------------

struct MULTIBITMAPHEADER {
	
	MULTIBITMAPHEADER()
		: node(NULL)
		, fif(FIF_UNKNOWN)
		, handle(NULL)
		, changed(FALSE)
		, page_count(0)
		, read_only(TRUE)
		, cache_fif(fif)
		, load_flags(0)
		, failed(FALSE)
		, read_data(NULL)
	{
		SetDefaultIO(&io);
	}

	~MULTIBITMAPHEADER() {
		// Counting the pages opens the decoder the whole session reads with (see
		// FreeImage_GetReadData()), and FreeImage_CloseMultiBitmap() closes it. An
		// open that gives up after the count - no page to read, no cache file, a
		// std::bad_alloc - never gets that far, and used to leave it open: a whole
		// libtiff handle for a TIFF. Every one of those destroys the header, so this
		// is where the decoder goes. The plugin's close_proc is handed the file
		// handle, so a header that still holds a decoder must be destroyed while
		// the file is open.
		if (read_data != NULL) {
			FreeImage_Close(node, &io, handle, read_data);
		}
		for (std::map<int, FIBITMAP *>::iterator i = page_metadata.begin(); i != page_metadata.end(); ++i) {
			FreeImage_Unload(i->second);
		}
	}
	
	PluginNode *node;
	FREE_IMAGE_FORMAT fif;
	FreeImageIO io;
	fi_handle handle;
	CacheFile m_cachefile;
	std::map<FIBITMAP *, int> locked_pages;
	BOOL changed;
	int page_count;
	BlockList m_blocks;
	// the file this was opened from by name, in the width the caller gave it;
	// empty for a stream or a memory handle, which are never written back
	FIFileName m_filename;
	BOOL read_only;
	FREE_IMAGE_FORMAT cache_fif;
	int load_flags;
	// Set when a page operation had to be dropped - a locked page, a format that
	// cannot hold what was asked of it, a cache write that failed. The mutators
	// return void and cannot say so themselves, so this is what
	// FreeImage_CloseMultiBitmap() reports rather than claiming a success it did
	// not achieve.
	BOOL failed;
	// FIMD_ANIMATION as it stood when each cached page went in, kept by cache block
	// number. A page is stored in the cache encoded in cache_fif, and what that format
	// cannot hold it cannot give back: libwebp deletes the ANMF chunk of a one-frame
	// animation whose frame fills the canvas (see MuxCleanup in muxedit.c), taking the
	// frame's duration and position with it, so a frame appended with a duration came
	// back out of the cache without one. Keeping the tags here rather than trusting the
	// round trip makes every format behave alike, whatever its single-image writer can
	// carry. The carriers are 1x1 bitmaps: only their metadata is wanted.
	std::map<int, FIBITMAP *> page_metadata;
	// Decoder state belonging to the plugin, opened by the first
	// FreeImage_LockPage() call and kept until FreeImage_CloseMultiBitmap().
	// It used to be opened and closed around every single page request, which
	// made each one re-parse the whole file, and left a plugin no way to carry
	// anything from one page to the next - see the GIF_PLAYBACK cache in
	// PluginGIF.cpp, which is what made that quadratic.
	void *read_data;
};

// =====================================================================
// Helper functions
// =====================================================================

// Name a file that keeps a multi-bitmap company while it is open: the block cache,
// and the spool the rewritten file is built in.
//
// This used to be done by replacing the file's extension, which made the name a
// function of the stem alone. "a.tif" and "a.tiff" in one directory therefore both
// wanted "a.ficache", and each opened it "w+b" - truncating the other's - so whichever
// closed first lost every page it held. Two processes on one file, or one process
// opening the same file twice, collided just as completely.
//
// So the whole filename is kept rather than its stem, and the process id and the
// address of the multi-bitmap's own header are added: two live multi-bitmaps cannot
// share a header address, and two processes cannot share a process id, which is as
// much uniqueness as this needs. Both files are still created beside the image -
// the spool has to be, because rename() only replaces a file atomically within one
// filesystem, and there is no reason to send the cache somewhere else on its own.
// The name is about 30 characters longer than the image's, which matters only for a
// filename already close to the system's limit.
// What is added is plain ASCII, so it is the same whether the image's name came in
// char or, from FreeImage_OpenMultiBitmapU(), in wchar_t - and the companion is
// spelled in the image's width, so it is created in the same directory.
inline void
MakeCompanionName(FIFileName& dst_filename, const FIFileName& src_filename, const void *owner, const char *dst_extension) {
	char suffix[64];

	sprintf(suffix, ".%lu.%llx.",
		(unsigned long)FI_GetProcessId(),
		(unsigned long long)(size_t)owner);

	dst_filename = src_filename;
	dst_filename.append(suffix);
	dst_filename.append(dst_extension);
}

} //< ns


// =====================================================================
// Internal Multipage functions
// =====================================================================

inline MULTIBITMAPHEADER *
FreeImage_GetMultiBitmapHeader(FIMULTIBITMAP *bitmap) {
	return (MULTIBITMAPHEADER *)bitmap->data;
}

static BlockListIterator DLL_CALLCONV
FreeImage_FindBlock(FIMULTIBITMAP *bitmap, int position) {
	assert(NULL != bitmap);

	MULTIBITMAPHEADER *header = FreeImage_GetMultiBitmapHeader(bitmap);

	// step 1: find the block that matches the given position

	int prev_count = 0;
	int count = 0;
	BlockListIterator i;

	for (i = header->m_blocks.begin(); i != header->m_blocks.end(); ++i) {
		prev_count = count;
		
		count += i->getPageCount();

		if (count > position) {
			break;
		}
	}

	// step 2: make sure we found the node. from here it gets a little complicated:
	// * if the block is single page, just return it
	// * if the block is a span of pages, split it in 3 new blocks
	//   and return the middle block, which is now a single page
	
	if ((i != header->m_blocks.end()) && (count > position)) {
		
		if (i->isSinglePage()) {
			return i;
		}
		
		const int item = i->getStart() + (position - prev_count);
		
		// left part
		
		if (item != i->getStart()) {
			header->m_blocks.insert(i, PageBlock(BLOCK_CONTINUEUS, i->getStart(), item - 1));
		}
		
		// middle part
		
		BlockListIterator block_target = header->m_blocks.insert(i, PageBlock(BLOCK_CONTINUEUS, item, item));
		
		// right part
		
		if (item != i->getEnd()) {
			header->m_blocks.insert(i, PageBlock(BLOCK_CONTINUEUS, item + 1, i->getEnd()));
		}
		
		// remove the old block that was just splitted
		
		header->m_blocks.erase(i);
		
		// return the splitted block
		
		return block_target;
	}
	
	// The position is not in the list. Every caller checks the page number against
	// FreeImage_GetPageCount() before coming here, so this means the block list and
	// the page count have got out of step. Return end() and let the caller fail:
	// an assert(false) used to stand here, and since only Makefile.mingw defines
	// NDEBUG, it took the whole host process down on a bad page number.
	return header->m_blocks.end();
}

// Resolve a logical page number to the block that holds it, *without* splitting
// anything. FreeImage_FindBlock() above cuts a single page out of a run, which is
// what the writers need but never what a reader needs: splitting on every read turns
// one block into as many blocks as there are pages, and makes walking a document
// quadratic in the block list.
// On return, *file_page is the page's index inside the source file, or -1 when the
// page lives in the cache and so has no place in the file at all.
static BlockListIterator
FreeImage_FindPage(MULTIBITMAPHEADER *header, int position, int *file_page) {
	int count = 0;

	*file_page = -1;

	for (BlockListIterator i = header->m_blocks.begin(); i != header->m_blocks.end(); ++i) {
		const int page_count = i->getPageCount();

		if (page_count <= 0) {
			// an empty run - opening a file with no readable pages makes one
			continue;
		}

		if (position < count + page_count) {
			if (i->m_type == BLOCK_CONTINUEUS) {
				*file_page = i->getStart() + (position - count);
			}
			return i;
		}

		count += page_count;
	}

	return header->m_blocks.end();
}

// Forget the animation tags kept for a cache block, because the block is going away
// or is about to hold a different page.
static void
FreeImage_ForgetPageMetadata(MULTIBITMAPHEADER *header, int ref) {
	std::map<int, FIBITMAP *>::iterator i = header->page_metadata.find(ref);

	if (i != header->page_metadata.end()) {
		FreeImage_Unload(i->second);
		header->page_metadata.erase(i);
	}
}

// Copy the FIMD_ANIMATION tags from one bitmap to another, one tag at a time.
// FreeImage_CloneMetadata() cannot be used for this: it copies every model *except*
// FIMD_ANIMATION, on the grounds that one bitmap's frame timing does not belong to a
// copy of it - which is right for a clone and exactly wrong here, where the copy is
// standing in for the same page.
static unsigned
FreeImage_CopyAnimationTags(FIBITMAP *dst, FIBITMAP *src) {
	FITAG *tag = NULL;
	FIMETADATA *mdhandle = FreeImage_FindFirstMetadata(FIMD_ANIMATION, src, &tag);
	unsigned count = 0;

	if (mdhandle != NULL) {
		do {
			if (FreeImage_SetMetadata(FIMD_ANIMATION, dst, FreeImage_GetTagKey(tag), tag)) {
				count++;
			}
		} while (FreeImage_FindNextMetadata(mdhandle, &tag));
		FreeImage_FindCloseMetadata(mdhandle);
	}

	return count;
}

// Keep this page's animation tags alongside the block that holds its pixels.
static void
FreeImage_RememberPageMetadata(MULTIBITMAPHEADER *header, int ref, FIBITMAP *dib) {
	FreeImage_ForgetPageMetadata(header, ref);

	if (FreeImage_GetMetadataCount(FIMD_ANIMATION, dib) == 0) {
		return;
	}

	FIBITMAP *carrier = FreeImage_Allocate(1, 1, 1, 0, 0, 0);

	if (carrier == NULL) {
		return;
	}
	if (FreeImage_CopyAnimationTags(carrier, dib) > 0) {
		header->page_metadata[ref] = carrier;
	} else {
		FreeImage_Unload(carrier);
	}
}

// Put them back on the page that has just come out of the cache.
static void
FreeImage_RestorePageMetadata(MULTIBITMAPHEADER *header, int ref, FIBITMAP *dib) {
	std::map<int, FIBITMAP *>::const_iterator i = header->page_metadata.find(ref);

	if (i != header->page_metadata.end()) {
		FreeImage_CopyAnimationTags(dib, i->second);
	}
}

// Read one page back out of the cache, where FreeImage_SavePageToBlock() or
// FreeImage_UnlockPage() put it, encoded in cache_fif.
static FIBITMAP *
FreeImage_LoadPageFromCache(MULTIBITMAPHEADER *header, const PageBlock& block) {
	const int size = block.getSize();

	if (size <= 0) {
		return NULL;
	}

	BYTE *compressed_data = (BYTE*)malloc(size * sizeof(BYTE));

	if (compressed_data == NULL) {
		return NULL;
	}

	FIBITMAP *dib = NULL;

	if (header->m_cachefile.readFile(compressed_data, block.getReference(), size)) {
		FIMEMORY *hmem = FreeImage_OpenMemory(compressed_data, size);

		if (hmem != NULL) {
			dib = FreeImage_LoadFromMemory(header->cache_fif, hmem, 0);
			FreeImage_CloseMemory(hmem);
		}
	}

	free(compressed_data);

	if (dib != NULL) {
		FreeImage_RestorePageMetadata(header, block.getReference(), dib);
	}

	return dib;
}

// Flags for the round trip every page makes through the cache, which is stored in
// cache_fif - the file's own format. A format with a lossy default has to be told to
// use its lossless mode here: the cache is scratch space, and a page that went into it
// lossily would be decoded and encoded again on the way to the file, carrying two
// generations of loss into a document the caller only saved once. WebP is the only
// multi-page format this applies to; the others have nothing lossy to turn off.
static int
FreeImage_GetCacheFlags(FREE_IMAGE_FORMAT fif) {
	switch (fif) {
		case FIF_WEBP:
			return WEBP_LOSSLESS;
		default:
			return 0;
	}
}

// Can this plugin serve the multi-bitmap the caller is asking for? A format with no
// loader cannot produce a single page of an existing file, and one with no writer can
// never be turned into a file at all: FreeImage_OpenMultiBitmap(FIF_AVIF, f, TRUE, ...)
// used to hand back a perfectly ordinary-looking handle for a format that has no way
// of writing anything, and the caller found out page by page, or not at all.
// FreeImage_LoadFromHandle() and FreeImage_SaveToHandle() refuse on exactly these
// grounds; this entry point simply never asked.
static BOOL
FreeImage_CheckMultiBitmapNode(PluginNode *node, FREE_IMAGE_FORMAT fif, BOOL needs_reading, BOOL needs_writing) {
	if ((node == NULL) || (node->m_plugin == NULL)) {
		return FALSE;
	}

	if (needs_reading && (node->m_plugin->load_proc == NULL)) {
		FreeImage_OutputMessageProc((int)fif, "%s does not support reading",
			FreeImage_GetFormatFromFIF(fif));
		return FALSE;
	}

	if (needs_writing && (node->m_plugin->save_proc == NULL)) {
		FreeImage_OutputMessageProc((int)fif, "%s does not support writing",
			FreeImage_GetFormatFromFIF(fif));
		return FALSE;
	}

	return TRUE;
}

// Can this document take another page? A plugin with no pagecount_proc has no idea
// what a page is: its Save writes a complete file every time it is called, so a
// second page does not extend the first, it concatenates another whole file onto the
// stream. Every reader then sees only the first page, and the rest is trailing
// rubbish - which FreeImage_CloseMultiBitmap() used to report as a success.
static BOOL
FreeImage_CanHoldAnotherPage(FIMULTIBITMAP *bitmap) {
	MULTIBITMAPHEADER *header = FreeImage_GetMultiBitmapHeader(bitmap);

	if ((header->node == NULL) || (header->node->m_plugin == NULL)) {
		return FALSE;
	}

	if ((header->node->m_plugin->pagecount_proc == NULL) && (FreeImage_GetPageCount(bitmap) >= 1)) {
		FreeImage_OutputMessageProc(header->fif,
			"%s is not a multi-page format: it cannot hold more than one page",
			FreeImage_GetFormatFromFIF(header->fif));
		header->failed = TRUE;
		return FALSE;
	}

	return TRUE;
}

// The decoder a read-only session keeps for the life of the multi-bitmap. It is
// opened on first use and closed in FreeImage_CloseMultiBitmap().
//
// Counting the pages and reading them used to open the plugin separately, so
// every document was parsed twice over: once by FreeImage_InternalGetPageCount()
// here, which then threw its decoder away, and again by the first
// FreeImage_LockPage(). For a format whose open_proc indexes the whole file -
// MNG walks every chunk, GIF every block - that is the file read end to end for
// nothing, and any message the plugin emits while parsing is emitted twice.
static void *
FreeImage_GetReadData(MULTIBITMAPHEADER *header) {
	if ((header->read_data == NULL) && (header->handle != NULL)) {
		header->io.seek_proc(header->handle, 0, SEEK_SET);
		header->read_data = FreeImage_Open(header->node, &header->io, header->handle, TRUE);
	}
	return header->read_data;
}

int DLL_CALLCONV
FreeImage_InternalGetPageCount(FIMULTIBITMAP *bitmap) {
	if (bitmap) {
		if (((MULTIBITMAPHEADER *)bitmap->data)->handle) {
			MULTIBITMAPHEADER *header = FreeImage_GetMultiBitmapHeader(bitmap);

			void *data = FreeImage_GetReadData(header);

			int page_count = (header->node->m_plugin->pagecount_proc != NULL) ? header->node->m_plugin->pagecount_proc(&header->io, header->handle, data) : 1;

			return page_count;
		}
	}

	return 0;
}

// =====================================================================
// Multipage functions
// =====================================================================

// FreeImage_OpenMultiBitmap() and FreeImage_OpenMultiBitmapU() are one function, and
// this is it. They differ only in how the filename is spelled, and FIFileName takes
// care of that - for the image itself, and for the cache and spool files named after it.
static FIMULTIBITMAP *
FreeImage_OpenMultiBitmapByName(FREE_IMAGE_FORMAT fif, const FIFileName& filename, BOOL create_new, BOOL read_only, BOOL keep_cache_in_memory, int flags) {

	FILE *handle = NULL;
	try {
		// sanity check on the parameters

		if (create_new) {
			read_only = FALSE;
		}

		// retrieve the plugin list to find the node belonging to this plugin

		PluginList *list = FreeImage_GetPluginList();

		if (list) {
			PluginNode *node = list->FindNodeFromFIF(fif);

			if (node) {
				// An existing file has to be read, and a brand new one has to be
				// written at FreeImage_CloseMultiBitmap() time - asking for one in a
				// format that has no writer cannot come to anything, so refuse it
				// here instead of accepting the call and producing no file at all.
				// An edit session (create_new FALSE, read_only FALSE) is deliberately
				// not held to the same test: read_only FALSE only means "modifications
				// go to the cache", and it is what FreeImage_OpenMultiBitmapFromHandle()
				// and FreeImage_LoadMultiBitmapFromMemory() pass themselves, so a
				// caller reading a read-only format that way is not doing anything
				// wrong. If it does go on to change something, the save at close
				// refuses and says why.
				if (!FreeImage_CheckMultiBitmapNode(node, fif, !create_new, create_new)) {
					return NULL;
				}

				if (!create_new) {
					handle = filename.openFile("rb");
					if (handle == NULL) {
						return NULL;
					}
				}

				std::unique_ptr<FIMULTIBITMAP> bitmap (new FIMULTIBITMAP);
				std::unique_ptr<MULTIBITMAPHEADER> header (new MULTIBITMAPHEADER);
				header->m_filename = filename;
				// io is default
				header->node = node;
				header->fif = fif;
				header->handle = handle;						
				header->read_only = read_only;
				header->cache_fif = fif;
				header->load_flags = flags;

				// A format with no writer can still be read page by page - that is how an
				// AVIF image sequence is played here, a frame at a time - but the writable
				// session asked for with read_only FALSE can never end in a file: the save
				// at FreeImage_CloseMultiBitmap() has nothing to write with. The document
				// is handed over all the same, so the pages can be read, and the close
				// reports the failure instead of returning the TRUE that says the file on
				// disk is the document the caller asked for.
				// FreeImage_OpenMultiBitmapFromHandle() and FreeImage_LoadMultiBitmapFromMemory()
				// pass read_only FALSE themselves and are deliberately left out of this:
				// they have no filename, which is what the save at close tests
				// (header->m_filename), so nothing was ever going to be written back.

				if (!read_only && (node->m_plugin->save_proc == NULL)) {
					FreeImage_OutputMessageProc(fif, "%s does not support writing: \"%s\" can be read page by page, but nothing can be saved back to it - FreeImage_CloseMultiBitmap() will report the failure",
						FreeImage_GetFormatFromFIF(fif), filename.display());
					header->failed = TRUE;
				}

				// store the MULTIBITMAPHEADER in the surrounding FIMULTIBITMAP structure

				bitmap->data = header.get();

				// cache the page count

				header->page_count = FreeImage_InternalGetPageCount(bitmap.get());

				// An existing file that yields no page could not be read as this
				// format: either the plugin's open_proc refused it or its pagecount_proc
				// found nothing. Handing back a multi-bitmap whose every LockPage()
				// returns NULL only moves the failure somewhere less obvious - and with
				// read_only FALSE, that useless handle would go on to overwrite the file
				// at close. FreeImage_Load() would have returned NULL here, and so does
				// this now.

				if (!create_new && (header->page_count <= 0)) {
					FreeImage_OutputMessageProc(fif, "%s: \"%s\" holds no page this plugin can read",
						FreeImage_GetFormatFromFIF(fif), filename.display());
					// the header closes the decoder the pages were counted with, and
					// needs the file still open to do it
					header.reset();
					if (handle) {
						fclose(handle);
					}
					return NULL;
				}

				// allocate a continueus block to describe the bitmap

				if (!create_new) {
					header->m_blocks.push_back(PageBlock(BLOCK_CONTINUEUS, 0, header->page_count - 1));
				}

				// set up the cache

				if (!read_only) {
					FIFileName cache_name;
					MakeCompanionName(cache_name, filename, header.get(), "ficache");
					
					if (!header->m_cachefile.open(cache_name, keep_cache_in_memory)) {
						// an error occured ... the header goes first, as above
						header.reset();
						if(handle){
						  fclose(handle);
						}
						return NULL;
					}
				}
				// return the multibitmap
				// std::bad_alloc won't be thrown from here on
				header.release(); // now owned by bitmap
				return bitmap.release(); // now owned by caller
			}
		}
	} catch (std::bad_alloc &) {
		/** @todo report error */
	}
	if (handle) {
		fclose(handle);
	}
	return NULL;
}

FIMULTIBITMAP * DLL_CALLCONV
FreeImage_OpenMultiBitmap(FREE_IMAGE_FORMAT fif, const char *filename, BOOL create_new, BOOL read_only, BOOL keep_cache_in_memory, int flags) {
	if (filename != NULL) {
		try {
			return FreeImage_OpenMultiBitmapByName(fif, FIFileName(filename), create_new, read_only, keep_cache_in_memory, flags);
		} catch (std::bad_alloc &) {
			/** @todo report error */
		}
	}
	return NULL;
}

// FreeImage_OpenMultiBitmap() for a wchar_t filename, as FreeImage_LoadU() is to
// FreeImage_Load(). A multi-bitmap goes back to its file long after opening it - the
// page cache and the spool are made beside it, and FreeImage_CloseMultiBitmap()
// renames the spool over it - and every one of those steps uses the wide name too.
// Like the other ...U functions this works on Windows only; anywhere else it does
// nothing and returns NULL.
FIMULTIBITMAP * DLL_CALLCONV
FreeImage_OpenMultiBitmapU(FREE_IMAGE_FORMAT fif, const wchar_t *filename, BOOL create_new, BOOL read_only, BOOL keep_cache_in_memory, int flags) {
#ifdef _WIN32
	if (filename != NULL) {
		try {
			return FreeImage_OpenMultiBitmapByName(fif, FIFileName(filename), create_new, read_only, keep_cache_in_memory, flags);
		} catch (std::bad_alloc &) {
			/** @todo report error */
		}
	}
#endif
	return NULL;
}

FIMULTIBITMAP * DLL_CALLCONV
FreeImage_OpenMultiBitmapFromHandle(FREE_IMAGE_FORMAT fif, FreeImageIO *io, fi_handle handle, int flags) {
	try {
		BOOL read_only = FALSE;	// modifications (if any) will be stored into the memory cache

		if (io && handle) {
		
			// retrieve the plugin list to find the node belonging to this plugin
			PluginList *list = FreeImage_GetPluginList();
		
			if (list) {
				PluginNode *node = list->FindNodeFromFIF(fif);
			
				if (node) {
					// the stream is there to be read; the format it will eventually be
					// written back as is FreeImage_SaveMultiBitmapToHandle()'s argument,
					// not this one, so only the loader is required here
					if (!FreeImage_CheckMultiBitmapNode(node, fif, TRUE, FALSE)) {
						return NULL;
					}

					std::unique_ptr<FIMULTIBITMAP> bitmap (new FIMULTIBITMAP);
					std::unique_ptr<MULTIBITMAPHEADER> header (new MULTIBITMAPHEADER);
					header->io = *io;
					header->node = node;
					header->fif = fif;
					header->handle = handle;						
					header->read_only = read_only;	
					header->cache_fif = fif;
					header->load_flags = flags;
							
					// store the MULTIBITMAPHEADER in the surrounding FIMULTIBITMAP structure

					bitmap->data = header.get();

					// cache the page count

					header->page_count = FreeImage_InternalGetPageCount(bitmap.get());

					// nothing readable in the stream - see FreeImage_OpenMultiBitmap()

					if (header->page_count <= 0) {
						FreeImage_OutputMessageProc(fif, "%s: the stream holds no page this plugin can read",
							FreeImage_GetFormatFromFIF(fif));
						return NULL;
					}

					// allocate a continueus block to describe the bitmap

					header->m_blocks.push_back(PageBlock(BLOCK_CONTINUEUS, 0, header->page_count - 1));
					
					// no need to open cache - it is in-memory by default

					header.release();
					return bitmap.release();
				}
			}
		}
	} catch (std::bad_alloc &) {
		/** @todo report error */
	}
	return NULL;
}

BOOL DLL_CALLCONV
FreeImage_SaveMultiBitmapToHandle(FREE_IMAGE_FORMAT fif, FIMULTIBITMAP *bitmap, FreeImageIO *io, fi_handle handle, int flags) {
	if(!bitmap || !bitmap->data || !io || !handle) {
		return FALSE;
	}

	BOOL success = TRUE;

	// retrieve the plugin list to find the node belonging to this plugin
	PluginList *list = FreeImage_GetPluginList();
	
	if (list) {
		PluginNode *node = list->FindNodeFromFIF(fif);

		if(node) {
			MULTIBITMAPHEADER *header = FreeImage_GetMultiBitmapHeader(bitmap);

			// A plugin that cannot write is not a destination. This used to go
			// straight on and call save_proc, which every read-only format leaves
			// NULL - a call to address zero, and the caller had asked a perfectly
			// answerable question: FreeImage_FIFSupportsWriting() knows.
			if (node->m_plugin->save_proc == NULL) {
				FreeImage_OutputMessageProc(fif, "%s does not support writing",
					FreeImage_GetFormatFromFIF(fif));
				return FALSE;
			}

			// ... and a plugin that has no idea what a page is cannot be handed
			// several of them: it would write one complete file per page, one after
			// another, into the same stream.
			if ((node->m_plugin->pagecount_proc == NULL) && (FreeImage_GetPageCount(bitmap) > 1)) {
				FreeImage_OutputMessageProc(fif,
					"%s is not a multi-page format: cannot write %d pages",
					FreeImage_GetFormatFromFIF(fif), FreeImage_GetPageCount(bitmap));
				return FALSE;
			}

			// dst data
			void *data = FreeImage_Open(node, io, handle, FALSE);
			// src data
			void *data_read = NULL;
			
			if(header->handle) {
				// open src
				header->io.seek_proc(header->handle, 0, SEEK_SET);
				data_read = FreeImage_Open(header->node, &header->io, header->handle, TRUE);
			}
			
			// write all the pages to the file using handle and io
			
			int count = 0;
			
			for (BlockListIterator i = header->m_blocks.begin(); i != header->m_blocks.end(); i++) {
				if (success) {
					switch(i->m_type) {
						case BLOCK_CONTINUEUS:
						{
							for (int j = i->getStart(); j <= i->getEnd(); j++) {

								// load the original source data
								FIBITMAP *dib = (header->node->m_plugin->load_proc != NULL) ?
									header->node->m_plugin->load_proc(&header->io, header->handle, j, header->load_flags, data_read) : NULL;

								// a page that will not load is not a page to hand to
								// save_proc, which is entitled to a bitmap
								if (dib == NULL) {
									success = FALSE;
									break;
								}

								// save the data
								success = node->m_plugin->save_proc(io, dib, handle, count, flags, data);
								count++;

								FreeImage_Unload(dib);

								if (!success) {
									break;
								}
							}

							break;
						}
						
						case BLOCK_REFERENCE:
						{
							// read the page back out of the cache

							FIBITMAP *dib = FreeImage_LoadPageFromCache(header, *i);

							if (dib == NULL) {
								success = FALSE;
								break;
							}

							// save the data

							success = node->m_plugin->save_proc(io, dib, handle, count, flags, data);
							count++;

							// unload the dib

							FreeImage_Unload(dib);

							break;
						}
					}
				} else {
					break;
				}
			}
			
			// close the files
			
			FreeImage_Close(header->node, &header->io, header->handle, data_read);

			FreeImage_Close(node, io, handle, data); 
			
			return success;
		}
	}

	return FALSE;
}


BOOL DLL_CALLCONV
FreeImage_CloseMultiBitmap(FIMULTIBITMAP *bitmap, int flags) {
	if (bitmap) {
		BOOL success = TRUE;
		
		if (bitmap->data) {
			MULTIBITMAPHEADER *header = FreeImage_GetMultiBitmapHeader(bitmap);

			// close the decoder FreeImage_LockPage() left open, if any. This has to
			// happen before the save below - which opens a second, independent one on
			// the same handle - and before the handle itself is closed, since a plugin
			// may still write through it (libtiff does)
			if (header->read_data != NULL) {
				FreeImage_Close(header->node, &header->io, header->handle, header->read_data);
				header->read_data = NULL;
			}

			// saves changes only of images loaded directly from a file
			if (header->changed && !header->m_filename.empty()) {
				try {
					// open a temp file

					FIFileName spool_name;

					MakeCompanionName(spool_name, header->m_filename, header, "fispool");

					// open the spool file and the source file
        
					FILE *f = spool_name.openFile("w+b");
				
					// saves changes
					if (f == NULL) {
						FreeImage_OutputMessageProc(header->fif, "Failed to open %s, %s", spool_name.display(), strerror(errno));
						success = FALSE;
					} else {
						success = FreeImage_SaveMultiBitmapToHandle(header->fif, bitmap, &header->io, (fi_handle)f, flags);

						// close the files

						if (fclose(f) != 0) {
							success = FALSE;
							FreeImage_OutputMessageProc(header->fif, "Failed to close %s, %s", spool_name.display(), strerror(errno));
						}
					}
					if (header->handle) {
						fclose((FILE *)header->handle);
					}
				
					// applies changes to the destination file

					if (success) {
#ifdef _WIN32
						// rename() will not replace an existing file on Windows, so
						// there the original has to go first. Everywhere else it is
						// replaced atomically, and removing it beforehand - which this
						// code used to do on every platform - meant that a rename that
						// failed for any reason left the caller with no file at all,
						// the rewritten one stranded under the spool's name.
						header->m_filename.removeFile();
#endif
						if (spool_name.renameFile(header->m_filename) == 0) {
							success = TRUE;
						} else {
							success = FALSE;
							FreeImage_OutputMessageProc(header->fif, "Failed to rename %s to %s, %s",
								spool_name.display(), header->m_filename.display(), strerror(errno));
#ifndef _WIN32
							// the original is still there, so the spool is only litter
							spool_name.removeFile();
#endif
						}
					} else {
						spool_name.removeFile();
					}
				} catch (std::bad_alloc &) {
					success = FALSE;
				}

			} else {
				if (header->handle && !header->m_filename.empty()) {
					fclose((FILE *)header->handle);
				}
			}

			// delete the last open bitmaps

			while (!header->locked_pages.empty()) {
				FreeImage_Unload(header->locked_pages.begin()->first);

				header->locked_pages.erase(header->locked_pages.begin()->first);
			}

			// A page operation that had to be dropped is not a success, whatever the
			// save above did. FreeImage_AppendPage() and the other mutators return
			// void, so this is the only place the caller can be told that the file
			// on disk is not the document they asked for - it used to return TRUE
			// after silently discarding every page it had been handed.

			if (header->failed) {
				success = FALSE;
			}

			// delete the FIMULTIBITMAPHEADER

			delete header;
		}

		delete bitmap;

		return success;
	}

	return FALSE;
}

int DLL_CALLCONV
FreeImage_GetPageCount(FIMULTIBITMAP *bitmap) {
	if (bitmap) {
		MULTIBITMAPHEADER *header = FreeImage_GetMultiBitmapHeader(bitmap);

		if (header->page_count == -1) {
			header->page_count = 0;

			for (BlockListIterator i = header->m_blocks.begin(); i != header->m_blocks.end(); ++i) {
				header->page_count += i->getPageCount();
			}
		}

		return header->page_count;
	}

	return 0;
}

static PageBlock
FreeImage_SavePageToBlock(MULTIBITMAPHEADER *header, FIBITMAP *data) {
	PageBlock res;
	
	if (header->read_only || !header->locked_pages.empty()) {
		return res;
	}

	DWORD compressed_size = 0;
	BYTE *compressed_data = NULL;

	// compress the bitmap data

	// open a memory handle
	FIMEMORY *hmem = FreeImage_OpenMemory();
	if(hmem==NULL) {
		return res;
	}
	// save the file to memory
	if(!FreeImage_SaveToMemory(header->cache_fif, data, hmem, FreeImage_GetCacheFlags(header->cache_fif))) {
		FreeImage_CloseMemory(hmem);
		return res;
	}
	// get the buffer from the memory stream
	if(!FreeImage_AcquireMemory(hmem, &compressed_data, &compressed_size)) {
		FreeImage_CloseMemory(hmem);
		return res;
	}
	
	// A page is addressed in the cache by an int offset and an int length, and
	// PageBlock holds its size in an int too, so anything past 2 GiB cannot be
	// described at all. The DWORD came straight through to writeFile()'s int
	// parameter, where it turned negative and was refused with no explanation.
	if (compressed_size > (DWORD)0x7FFFFFFF) {
		FreeImage_OutputMessageProc(header->fif,
			"This page is %u bytes once encoded; the page cache cannot hold more than 2 GiB",
			compressed_size);
		FreeImage_CloseMemory(hmem);
		return res;
	}

	// write the compressed data to the cache
	int ref = header->m_cachefile.writeFile(compressed_data, compressed_size);
	// get rid of the compressed data
	FreeImage_CloseMemory(hmem);

	// 0 is the cache saying it stored nothing. It could not be told apart from a
	// real block number until block numbering was moved to start at 1.
	if (ref == 0) {
		return res;
	}

	FreeImage_RememberPageMetadata(header, ref, data);

	res = PageBlock(BLOCK_REFERENCE, ref, compressed_size);

	return res;
}

BOOL DLL_CALLCONV
FreeImage_AppendPageEx(FIMULTIBITMAP *bitmap, FIBITMAP *data) {
	if (!bitmap || !data) {
		return FALSE;
	}

	MULTIBITMAPHEADER *header = FreeImage_GetMultiBitmapHeader(bitmap);

	if (!FreeImage_CanHoldAnotherPage(bitmap)) {
		return FALSE;
	}

	if(const PageBlock block = FreeImage_SavePageToBlock(header, data)) {
		// add the block
		header->m_blocks.push_back(block);
		header->changed = TRUE;
		header->page_count = -1;
		return TRUE;
	}

	// the page was dropped - the bitmap is read-only, a page is locked, or
	// cache_fif cannot encode this bitmap
	FreeImage_OutputMessageProc(header->fif,
		"FreeImage_AppendPage: the page could not be stored (the bitmap is read-only or has locked pages, or %s cannot encode this image)",
		FreeImage_GetFormatFromFIF(header->cache_fif));
	header->failed = TRUE;
	return FALSE;
}

void DLL_CALLCONV
FreeImage_AppendPage(FIMULTIBITMAP *bitmap, FIBITMAP *data) {
	FreeImage_AppendPageEx(bitmap, data);
}

BOOL DLL_CALLCONV
FreeImage_InsertPageEx(FIMULTIBITMAP *bitmap, int page, FIBITMAP *data) {
	if (!bitmap || !data) {
		return FALSE;
	}

	MULTIBITMAPHEADER *header = FreeImage_GetMultiBitmapHeader(bitmap);
	const int page_count = FreeImage_GetPageCount(bitmap);

	// A negative position used to fall through the "insert at the front" branch below
	// and silently put the page at 0; a position at or past the end is
	// FreeImage_AppendPage()'s job and was refused without a word, so a caller working
	// from a stale page count lost the page and was told nothing.

	if ((page < 0) || (page >= page_count)) {
		FreeImage_OutputMessageProc(header->fif,
			"FreeImage_InsertPage: cannot insert at %d (the bitmap has %d page(s); use FreeImage_AppendPage to add at the end)",
			page, page_count);
		header->failed = TRUE;
		return FALSE;
	}

	if (!FreeImage_CanHoldAnotherPage(bitmap)) {
		return FALSE;
	}

	if(const PageBlock block = FreeImage_SavePageToBlock(header, data)) {
		// add a block
		if (page > 0) {
			BlockListIterator block_source = FreeImage_FindBlock(bitmap, page);

			if (block_source == header->m_blocks.end()) {
				FreeImage_OutputMessageProc(header->fif, "FreeImage_InsertPage: page %d could not be located", page);
				header->failed = TRUE;
				return FALSE;
			}
			header->m_blocks.insert(block_source, block);
		} else {
			header->m_blocks.push_front(block);
		}

		header->changed = TRUE;
		header->page_count = -1;
		return TRUE;
	}

	FreeImage_OutputMessageProc(header->fif,
		"FreeImage_InsertPage: the page could not be stored (the bitmap is read-only or has locked pages, or %s cannot encode this image)",
		FreeImage_GetFormatFromFIF(header->cache_fif));
	header->failed = TRUE;
	return FALSE;
}

void DLL_CALLCONV
FreeImage_InsertPage(FIMULTIBITMAP *bitmap, int page, FIBITMAP *data) {
	FreeImage_InsertPageEx(bitmap, page, data);
}

BOOL DLL_CALLCONV
FreeImage_DeletePageEx(FIMULTIBITMAP *bitmap, int page) {
	if (!bitmap) {
		return FALSE;
	}

	MULTIBITMAPHEADER *header = FreeImage_GetMultiBitmapHeader(bitmap);

	if (header->read_only || !header->locked_pages.empty()) {
		FreeImage_OutputMessageProc(header->fif, "FreeImage_DeletePage: the bitmap is %s",
			header->read_only ? "read-only" : "holding locked pages");
		header->failed = TRUE;
		return FALSE;
	}

	const int page_count = FreeImage_GetPageCount(bitmap);

	// Neither end was checked. A negative page made FreeImage_FindBlock() split a run
	// into blocks with negative extents, after which the page count and the file
	// disagreed for good; a page at or past the end reached an assert(false), which
	// aborts the process in any build that does not define NDEBUG - which is all of
	// them except MinGW.

	if ((page < 0) || (page >= page_count)) {
		FreeImage_OutputMessageProc(header->fif,
			"FreeImage_DeletePage: page %d does not exist (the bitmap has %d page(s))",
			page, page_count);
		header->failed = TRUE;
		return FALSE;
	}

	if (page_count <= 1) {
		FreeImage_OutputMessageProc(header->fif,
			"FreeImage_DeletePage: a multi-page bitmap must keep at least one page");
		header->failed = TRUE;
		return FALSE;
	}

	BlockListIterator i = FreeImage_FindBlock(bitmap, page);

	if (i != header->m_blocks.end()) {
		switch(i->m_type) {
			case BLOCK_CONTINUEUS :
				header->m_blocks.erase(i);
				break;

			case BLOCK_REFERENCE :
				FreeImage_ForgetPageMetadata(header, i->getReference());
				header->m_cachefile.deleteFile(i->getReference());
				header->m_blocks.erase(i);
				break;
		}

		header->changed = TRUE;
		header->page_count = -1;
		return TRUE;
	}

	FreeImage_OutputMessageProc(header->fif, "FreeImage_DeletePage: page %d could not be located", page);
	header->failed = TRUE;
	return FALSE;
}

void DLL_CALLCONV
FreeImage_DeletePage(FIMULTIBITMAP *bitmap, int page) {
	FreeImage_DeletePageEx(bitmap, page);
}

FIBITMAP * DLL_CALLCONV
FreeImage_LockPage(FIMULTIBITMAP *bitmap, int page) {
	if (!bitmap) {
		return NULL;
	}

	MULTIBITMAPHEADER *header = FreeImage_GetMultiBitmapHeader(bitmap);
	const int page_count = FreeImage_GetPageCount(bitmap);

	// A page number outside the document is a caller error, not something to guess
	// at. -1 in particular used to be passed on to the plugin, where it is their own
	// "this is a single image, not a page" value, and came back as page 0.

	if ((page < 0) || (page >= page_count)) {
		FreeImage_OutputMessageProc(header->fif,
			"FreeImage_LockPage: page %d does not exist (the bitmap has %d page(s))",
			page, page_count);
		return NULL;
	}

	// only lock if the page wasn't locked before...

	for (std::map<FIBITMAP *, int>::iterator i = header->locked_pages.begin(); i != header->locked_pages.end(); ++i) {
		if (i->second == page) {
			return NULL;
		}
	}

	// Find out where the page actually lives. The number the caller uses is a
	// position in the document as it stands now, which stops matching the page's
	// position in the file as soon as anything has been deleted, inserted or moved -
	// and a page appended or edited in this session is not in the file at all, it is
	// in the cache. This used to be handed straight to the plugin as a file page
	// number, so after a FreeImage_DeletePage() every lock returned the wrong page,
	// and FreeImage_UnlockPage() then wrote the caller's edit over a different one.

	int file_page = -1;
	BlockListIterator block = FreeImage_FindPage(header, page, &file_page);

	if (block == header->m_blocks.end()) {
		return NULL;
	}

	FIBITMAP *dib = NULL;

	if (block->m_type == BLOCK_REFERENCE) {
		// the page is in the cache, not in the file - so it can be locked even on a
		// multi-bitmap created with create_new, which has no file behind it yet
		dib = FreeImage_LoadPageFromCache(header, *block);
	} else {
		// Open the bitmap once and keep the decoder for the life of the
		// multi-bitmap, rather than opening and closing it around every page.
		// Reopening made each request re-parse the file from the beginning -
		// for a GIF, a scan of every block in it - and threw away whatever the
		// plugin had worked out about the pages it had already decoded. Closed
		// in FreeImage_CloseMultiBitmap().

		if (header->handle == NULL) {
			return NULL;
		}
		FreeImage_GetReadData(header);

		// NULL is what a plugin with nothing to carry from one page to the next
		// returns, and it is what FreeImage_Open() returns for a plugin with no
		// open_proc at all - PNG, JPEG, BMP and TARGA all leave it NULL, and JNG's
		// is a stub that returns NULL. None of that is a failure, so a plugin's
		// load_proc has always been called with whatever came back, NULL included:
		// FreeImage_LoadFromHandle() still does exactly that, which is why every
		// plugin already copes with a NULL data pointer.
		//
		// Requiring a non-NULL read_data here therefore made every single-image
		// format unlockable through this API - FreeImage_LockPage(0) returned NULL
		// for a document whose FreeImage_GetPageCount() had just said 1.

		if (header->node->m_plugin->load_proc != NULL) {
			dib = header->node->m_plugin->load_proc(&header->io, header->handle, file_page, header->load_flags, header->read_data);
		}
	}

	if (dib != NULL) {
		// Remember the position the caller asked for. Every mutator refuses to run
		// while a page is locked, so the block list cannot move underneath this and
		// the number is still the right one when FreeImage_UnlockPage() resolves it.
		header->locked_pages[dib] = page;
	}

	return dib;
}

void DLL_CALLCONV
FreeImage_UnlockPage(FIMULTIBITMAP *bitmap, FIBITMAP *page, BOOL changed) {
	if ((bitmap) && (page)) {
		MULTIBITMAPHEADER *header = FreeImage_GetMultiBitmapHeader(bitmap);

		// find out if the page we try to unlock is actually locked...

		if (header->locked_pages.find(page) != header->locked_pages.end()) {
			// store the bitmap compressed in the cache for later writing
			
			if (changed && !header->read_only) {
				header->changed = TRUE;

				// cut loose the block from the rest

				BlockListIterator i = FreeImage_FindBlock(bitmap, header->locked_pages[page]);

				if (i == header->m_blocks.end()) {
					// the page was locked and nothing may change the list while it
					// is, so this cannot happen - but the edit has nowhere to go, and
					// writing it to end() would corrupt the list
					FreeImage_OutputMessageProc(header->fif,
						"FreeImage_UnlockPage: page %d is no longer in the bitmap, the changes are lost",
						header->locked_pages[page]);
					header->failed = TRUE;
					FreeImage_Unload(page);
					header->locked_pages.erase(page);
					return;
				}

				// compress the data

				DWORD compressed_size = 0;
				BYTE *compressed_data = NULL;

				// Encode the page into the cache. None of these three used to be
				// checked, so a cache_fif that cannot encode this bitmap - every
				// read-only format, for one, since cache_fif is the file's own format -
				// left compressed_data NULL and compressed_size 0, and the block was
				// then overwritten with a reference to block 0 of length 0: a page
				// pointing at another page's data.
				FIMEMORY *hmem = FreeImage_OpenMemory();

				if ((hmem == NULL)
					|| !FreeImage_SaveToMemory(header->cache_fif, page, hmem, FreeImage_GetCacheFlags(header->cache_fif))
					|| !FreeImage_AcquireMemory(hmem, &compressed_data, &compressed_size)
					|| (compressed_data == NULL) || (compressed_size == 0)
					|| (compressed_size > (DWORD)0x7FFFFFFF)) {   /* see FreeImage_SavePageToBlock */
					FreeImage_OutputMessageProc(header->fif,
						"FreeImage_UnlockPage: %s cannot store this page, the changes are lost",
						FreeImage_GetFormatFromFIF(header->cache_fif));
					header->failed = TRUE;
					if (hmem != NULL) {
						FreeImage_CloseMemory(hmem);
					}
					FreeImage_Unload(page);
					header->locked_pages.erase(page);
					return;
				}

				// write the data to the cache

				if (i->m_type == BLOCK_REFERENCE) {
					FreeImage_ForgetPageMetadata(header, i->getReference());
					header->m_cachefile.deleteFile(i->getReference());
				}

				int iPage = header->m_cachefile.writeFile(compressed_data, compressed_size);

				if (iPage == 0) {
					// the cache stored nothing, so there is no block for the page to
					// point at. Leave the block as it was rather than aim it at 0,
					// which is no longer a block number at all.
					FreeImage_OutputMessageProc(header->fif,
						"FreeImage_UnlockPage: the cache could not store this page, the changes are lost");
					header->failed = TRUE;
					FreeImage_CloseMemory(hmem);
					FreeImage_Unload(page);
					header->locked_pages.erase(page);
					return;
				}

				FreeImage_RememberPageMetadata(header, iPage, page);

				*i = PageBlock(BLOCK_REFERENCE, iPage, compressed_size);

				// get rid of the compressed data

				FreeImage_CloseMemory(hmem);
			}

			// reset the locked page so that another page can be locked

			FreeImage_Unload(page);

			header->locked_pages.erase(page);
		}
	}
}

BOOL DLL_CALLCONV
FreeImage_MovePage(FIMULTIBITMAP *bitmap, int target, int source) {
	if (!bitmap) {
		return FALSE;
	}

	MULTIBITMAPHEADER *header = FreeImage_GetMultiBitmapHeader(bitmap);

	if (header->read_only || !header->locked_pages.empty()) {
		return FALSE;
	}

	const int page_count = FreeImage_GetPageCount(bitmap);

	if ((target == source)
		|| (target < 0) || (target >= page_count)
		|| (source < 0) || (source >= page_count)) {
		return FALSE;
	}

	// "source" is where the page is now and "target" is where it should end up - the
	// description the .NET wrapper carries for this function spells that out ("Moves
	// the source page to the position of the target page", target = "New position of
	// the page", source = "Old position of the page"). The two were used the other way
	// round: the page at "target" was the one that moved, and it landed just before
	// "source" instead of at "target".

	BlockListIterator block_source = FreeImage_FindBlock(bitmap, source);

	if (block_source == header->m_blocks.end()) {
		return FALSE;
	}

	// FreeImage_FindBlock() has cut the page out as a block of its own, so it can be
	// lifted out whole. Take it out first and only then look for the destination, so
	// that "target" counts positions in the list the page is no longer part of -
	// otherwise moving a page to a later position leaves it one short.

	const PageBlock moved = *block_source;

	header->m_blocks.erase(block_source);
	// the document is one page shorter for the moment, and the test below has to see
	// that: FreeImage_GetPageCount() returns the cached count until it is invalidated
	header->page_count = -1;

	if (target >= FreeImage_GetPageCount(bitmap)) {
		header->m_blocks.push_back(moved);
	} else {
		BlockListIterator block_target = FreeImage_FindBlock(bitmap, target);

		if (block_target == header->m_blocks.end()) {
			// put the page back rather than drop it
			header->m_blocks.push_back(moved);
			header->page_count = -1;
			return FALSE;
		}
		header->m_blocks.insert(block_target, moved);
	}

	header->changed = TRUE;
	// ... and the count is back up again, so drop the shortened one cached above
	header->page_count = -1;

	return TRUE;
}

BOOL DLL_CALLCONV
FreeImage_GetLockedPageNumbers(FIMULTIBITMAP *bitmap, int *pages, int *count) {
	if ((bitmap) && (count)) {
		MULTIBITMAPHEADER *header = FreeImage_GetMultiBitmapHeader(bitmap);

		if ((pages == NULL) || (*count == 0)) {
			*count = (int)header->locked_pages.size();
		} else {
			int c = 0;

			for (std::map<FIBITMAP *, int>::iterator i = header->locked_pages.begin(); i != header->locked_pages.end(); ++i) {
				pages[c] = i->second;

				c++;

				if (c == *count) {
					break;
				}
			}
		}

		return TRUE;
	}

	return FALSE;
}

// =====================================================================
// Memory IO Multipage functions
// =====================================================================

FIMULTIBITMAP * DLL_CALLCONV
FreeImage_LoadMultiBitmapFromMemory(FREE_IMAGE_FORMAT fif, FIMEMORY *stream, int flags) {
	BOOL read_only = FALSE;	// modifications (if any) will be stored into the memory cache

	// retrieve the plugin list to find the node belonging to this plugin

	PluginList *list = FreeImage_GetPluginList();

	if (list) {
		PluginNode *node = list->FindNodeFromFIF(fif);

		if (node) {
				// as above: the memory stream is read here, and written back through
				// FreeImage_SaveMultiBitmapToMemory()'s own format argument
				if (!FreeImage_CheckMultiBitmapNode(node, fif, TRUE, FALSE)) {
					return NULL;
				}

				FIMULTIBITMAP *bitmap = new(std::nothrow) FIMULTIBITMAP;

				if (bitmap) {
					MULTIBITMAPHEADER *header = new(std::nothrow) MULTIBITMAPHEADER;

					if (header) {
						header->node = node;
						header->fif = fif;
						SetMemoryIO(&header->io);
						header->handle = (fi_handle)stream;						
						header->read_only = read_only;
						header->cache_fif = fif;
						header->load_flags = flags;

						// store the MULTIBITMAPHEADER in the surrounding FIMULTIBITMAP structure

						bitmap->data = header;

						// cache the page count

						header->page_count = FreeImage_InternalGetPageCount(bitmap);

						// nothing readable in the stream - see FreeImage_OpenMultiBitmap()

						if (header->page_count <= 0) {
							FreeImage_OutputMessageProc(fif, "%s: the memory stream holds no page this plugin can read",
								FreeImage_GetFormatFromFIF(fif));
							delete header;
							delete bitmap;
							return NULL;
						}

						// allocate a continueus block to describe the bitmap. Nothing else after
						// the count can throw, and this function has no try of its own, so a
						// std::bad_alloc here used to escape to the caller - leaking the header
						// and the decoder the count had opened

						try {
							header->m_blocks.push_back(PageBlock(BLOCK_CONTINUEUS, 0, header->page_count - 1));
						} catch (std::bad_alloc &) {
							delete header;
							delete bitmap;
							return NULL;
						}

						// no need to open cache - it is in-memory by default

						return bitmap;
					}
					
					delete bitmap;
				}

		}
	}

	return NULL;
}

BOOL DLL_CALLCONV
FreeImage_SaveMultiBitmapToMemory(FREE_IMAGE_FORMAT fif, FIMULTIBITMAP *bitmap, FIMEMORY *stream, int flags) {
	if (stream && stream->data) {
		FreeImageIO io;
		SetMemoryIO(&io);

		return FreeImage_SaveMultiBitmapToHandle(fif, bitmap, &io, (fi_handle)stream, flags);
	}

	return FALSE;
}
