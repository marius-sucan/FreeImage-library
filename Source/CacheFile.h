// ==========================================================
// Multi-Page functions
//
// Design and implementation by
// - Floris van den Berg (flvdberg@wxs.nl)
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

#ifndef FREEIMAGE_CACHEFILE_H
#define FREEIMAGE_CACHEFILE_H

// ----------------------------------------------------------

#include "FreeImage.h"
#include "Utilities.h"

// ----------------------------------------------------------

static const int CACHE_SIZE = 32;
static const int BLOCK_SIZE = (64 * 1024) - 8;

// ----------------------------------------------------------

#ifdef _WIN32
#pragma pack(push, 1)
#else
#pragma pack(1)
#endif // _WIN32

struct Block {
     unsigned nr;
     unsigned next;
     BYTE *data;
};

#ifdef _WIN32
#pragma pack(pop)
#else
#pragma pack()
#endif // _WIN32

// ----------------------------------------------------------

// a file name in the caller's width; wide names stay wide down to _wfopen()
class FIFileName {
public :
	FIFileName() {}
	explicit FIFileName(const char *name);
#ifdef _WIN32
	explicit FIFileName(const wchar_t *name);
#endif

	bool empty() const { return m_name.empty(); }

	// ASCII suffixes only
	void append(const char *suffix);

	FILE *openFile(const char *mode) const;
	int removeFile() const;
	// dst_name must have the same width
	int renameFile(const FIFileName& dst_name) const;

	// for messages: non-ASCII shows as '?'
	const char *display() const { return m_name.c_str(); }

private :
	// the name, or display()'s rendering of a wide one
	std::string m_name;
#ifdef _WIN32
	// the wide name, or empty
	std::wstring m_wname;
#endif
};

// ----------------------------------------------------------

class CacheFile {
	typedef std::list<Block *> PageCache;
	typedef std::list<Block *>::iterator PageCacheIt;
	typedef std::map<int, PageCacheIt> PageMap;
	typedef std::map<int, PageCacheIt>::iterator PageMapIt;

public :
	CacheFile();
	~CacheFile();
	
	BOOL open(const FIFileName& filename = FIFileName(), BOOL keep_in_memory = TRUE);
	void close();

	BOOL readFile(BYTE *data, int nr, int size);
	int writeFile(BYTE *data, int size);
	void deleteFile(int nr);

private :
	void cleanupMemCache();
	int allocateBlock();
	Block *lockBlock(int nr);
	BOOL unlockBlock(int nr);
	BOOL deleteBlock(int nr);

private :
	FILE *m_file;
	FIFileName m_filename;
	std::list<int> m_free_pages;
	PageCache m_page_cache_mem;
	PageCache m_page_cache_disk;
	PageMap m_page_map;
	int m_page_count;
	Block *m_current_block;
	BOOL m_keep_in_memory;
};

#endif // FREEIMAGE_CACHEFILE_H
