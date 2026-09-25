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
		, start(0)
	{
		SetDefaultIO(&io);
	}

	~MULTIBITMAPHEADER() {
		// needs the file still open: close_proc gets the handle
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
	// empty for streams and memory handles
	FIFileName m_filename;
	BOOL read_only;
	FREE_IMAGE_FORMAT cache_fif;
	int load_flags;
	// a page operation was dropped; reported at close
	BOOL failed;
	// FIMD_ANIMATION per cache block; the cache format may drop it
	std::map<int, FIBITMAP *> page_metadata;
	// plugin decoder state, from the first LockPage until close
	void *read_data;
	// handle position where the image starts; a stream need not hold it at 0
	INT64 start;
};

// =====================================================================
// Helper functions
// =====================================================================

// unique per file name, process and header
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
	
	return header->m_blocks.end();
}

// like FreeImage_FindBlock() but never splits; *file_page is -1 for cached pages
static BlockListIterator
FreeImage_FindPage(MULTIBITMAPHEADER *header, int position, int *file_page) {
	int count = 0;

	*file_page = -1;

	for (BlockListIterator i = header->m_blocks.begin(); i != header->m_blocks.end(); ++i) {
		const int page_count = i->getPageCount();

		if (page_count <= 0) {
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

static void
FreeImage_ForgetPageMetadata(MULTIBITMAPHEADER *header, int ref) {
	std::map<int, FIBITMAP *>::iterator i = header->page_metadata.find(ref);

	if (i != header->page_metadata.end()) {
		FreeImage_Unload(i->second);
		header->page_metadata.erase(i);
	}
}

// not FreeImage_CloneMetadata(): it skips FIMD_ANIMATION
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

static void
FreeImage_RestorePageMetadata(MULTIBITMAPHEADER *header, int ref, FIBITMAP *dib) {
	std::map<int, FIBITMAP *>::const_iterator i = header->page_metadata.find(ref);

	if (i != header->page_metadata.end()) {
		FreeImage_CopyAnimationTags(dib, i->second);
	}
}

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

// lossless cache round trip (WebP defaults to lossy)
static int
FreeImage_GetCacheFlags(FREE_IMAGE_FORMAT fif) {
	switch (fif) {
		case FIF_WEBP:
			return WEBP_LOSSLESS;
		default:
			return 0;
	}
}

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

// only a plugin with a pagecount_proc can hold several pages
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

static void *
FreeImage_GetReadData(MULTIBITMAPHEADER *header) {
	if ((header->read_data == NULL) && (header->handle != NULL)) {
		header->io.seek_proc(header->handle, header->start, SEEK_SET);
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

// shared by FreeImage_OpenMultiBitmap() and FreeImage_OpenMultiBitmapU()
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
				// a new file needs a writer, an existing one a loader
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

				// store the MULTIBITMAPHEADER in the surrounding FIMULTIBITMAP structure

				bitmap->data = header.get();

				// cache the page count

				header->page_count = FreeImage_InternalGetPageCount(bitmap.get());

				if (!create_new && (header->page_count <= 0)) {
					FreeImage_OutputMessageProc(fif, "%s: \"%s\" holds no page this plugin can read",
						FreeImage_GetFormatFromFIF(fif), filename.display());
					// reset before fclose: the header closes the decoder
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
						// an error occured ...
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

// wchar_t filename; Windows only (NULL elsewhere)
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
					// only reading is needed; the save names its own format
					if (!FreeImage_CheckMultiBitmapNode(node, fif, TRUE, FALSE)) {
						return NULL;
					}

					std::unique_ptr<FIMULTIBITMAP> bitmap (new FIMULTIBITMAP);
					std::unique_ptr<MULTIBITMAPHEADER> header (new MULTIBITMAPHEADER);
					header->io = *io;
					header->node = node;
					header->fif = fif;
					header->handle = handle;						
					header->start = MAX(io->tell_proc(handle), (INT64)0);
					header->read_only = read_only;	
					header->cache_fif = fif;
					header->load_flags = flags;
							
					// store the MULTIBITMAPHEADER in the surrounding FIMULTIBITMAP structure

					bitmap->data = header.get();

					// cache the page count

					header->page_count = FreeImage_InternalGetPageCount(bitmap.get());

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

			if (node->m_plugin->save_proc == NULL) {
				FreeImage_OutputMessageProc(fif, "%s does not support writing",
					FreeImage_GetFormatFromFIF(fif));
				return FALSE;
			}

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
				header->io.seek_proc(header->handle, header->start, SEEK_SET);
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

								// load the original source data, with pixels even if the bitmap was opened header-only
								FIBITMAP *dib = (header->node->m_plugin->load_proc != NULL) ?
									header->node->m_plugin->load_proc(&header->io, header->handle, j, header->load_flags & ~FIF_LOAD_NOPIXELS, data_read) : NULL;

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

			// close the read decoder before the save and before the handle
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
						// Windows rename() cannot replace an existing file
						header->m_filename.removeFile();
#endif
						if (spool_name.renameFile(header->m_filename) == 0) {
							success = TRUE;
						} else {
							success = FALSE;
							FreeImage_OutputMessageProc(header->fif, "Failed to rename %s to %s, %s",
								spool_name.display(), header->m_filename.display(), strerror(errno));
#ifndef _WIN32
							// the original is intact: drop the spool
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
	
	// cache offsets and sizes are int: 2 GiB max
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

	// 0: the cache stored nothing
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

	int file_page = -1;
	BlockListIterator block = FreeImage_FindPage(header, page, &file_page);

	if (block == header->m_blocks.end()) {
		return NULL;
	}

	FIBITMAP *dib = NULL;

	if (block->m_type == BLOCK_REFERENCE) {
		// cached page: lockable even with no file yet (create_new)
		dib = FreeImage_LoadPageFromCache(header, *block);
	} else {
		if (header->handle == NULL) {
			return NULL;
		}
		FreeImage_GetReadData(header);

		// NULL read_data is valid: many plugins have no open_proc

		if (header->node->m_plugin->load_proc != NULL) {
			dib = header->node->m_plugin->load_proc(&header->io, header->handle, file_page, header->load_flags, header->read_data);
		}
	}

	if (dib != NULL) {
		// stays valid: mutators refuse to run while a page is locked
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
					// cannot happen while locked; never write to end()
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

				FIMEMORY *hmem = FreeImage_OpenMemory();

				if ((hmem == NULL)
					|| !FreeImage_SaveToMemory(header->cache_fif, page, hmem, FreeImage_GetCacheFlags(header->cache_fif))
					|| !FreeImage_AcquireMemory(hmem, &compressed_data, &compressed_size)
					|| (compressed_data == NULL) || (compressed_size == 0)
					|| (compressed_size > (DWORD)0x7FFFFFFF)) {   /* 2 GiB cache limit */
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
					// nothing stored: leave the block as it was
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

	// source: where the page is; target: where it goes

	BlockListIterator block_source = FreeImage_FindBlock(bitmap, source);

	if (block_source == header->m_blocks.end()) {
		return FALSE;
	}

	// remove the page first, so target counts positions without it

	const PageBlock moved = *block_source;

	header->m_blocks.erase(block_source);
	// invalidate the cached count for the test below
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
						header->start = MAX(FreeImage_TellMemory(stream), 0L);
						header->read_only = read_only;
						header->cache_fif = fif;
						header->load_flags = flags;

						// store the MULTIBITMAPHEADER in the surrounding FIMULTIBITMAP structure

						bitmap->data = header;

						// cache the page count

						header->page_count = FreeImage_InternalGetPageCount(bitmap);

						if (header->page_count <= 0) {
							FreeImage_OutputMessageProc(fif, "%s: the memory stream holds no page this plugin can read",
								FreeImage_GetFormatFromFIF(fif));
							delete header;
							delete bitmap;
							return NULL;
						}

						// allocate a continueus block to describe the bitmap

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
		FIMEMORYHEADER *mem_header = (FIMEMORYHEADER*)(stream->data);

		if (mem_header->delete_me == TRUE) {
			FreeImageIO io;
			SetMemoryIO(&io);

			return FreeImage_SaveMultiBitmapToHandle(fif, bitmap, &io, (fi_handle)stream, flags);
		} else {
			// do not save in a user buffer
			FreeImage_OutputMessageProc(fif, "Memory buffer is read only");
		}
	}

	return FALSE;
}
