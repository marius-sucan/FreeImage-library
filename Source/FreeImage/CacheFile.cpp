// ==========================================================
// Multi-Page functions
//
// Design and implementation by
// - Floris van den Berg (flvdberg@wxs.nl)
// - checkered (checkered@users.sourceforge.net)
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

// ----------------------------------------------------------

#ifdef _WIN32
static std::wstring
WidenASCII(const char *s) {
	std::wstring ws;

	for (; *s != '\0'; s++) {
		ws += (wchar_t)(unsigned char)*s;
	}

	return ws;
}
#endif

FIFileName::FIFileName(const char *name) : m_name(name) {
}

#ifdef _WIN32
FIFileName::FIFileName(const wchar_t *name) : m_wname(name) {
	// char rendering for messages: non-ASCII becomes '?'
	m_name.reserve(m_wname.size());

	for (size_t i = 0; i < m_wname.size(); i++) {
		const wchar_t c = m_wname[i];

		if (c < 0x80) {
			m_name += (char)c;
		} else {
			m_name += '?';

			// a surrogate pair is one character
			if ((c >= 0xD800) && (c <= 0xDBFF) && (i + 1 < m_wname.size())
				&& (m_wname[i + 1] >= 0xDC00) && (m_wname[i + 1] <= 0xDFFF)) {
				i++;
			}
		}
	}
}
#endif

void
FIFileName::append(const char *suffix) {
#ifdef _WIN32
	if (!m_wname.empty()) {
		m_wname += WidenASCII(suffix);
	}
#endif
	m_name += suffix;
}

FILE *
FIFileName::openFile(const char *mode) const {
#ifdef _WIN32
	if (!m_wname.empty()) {
		return _wfopen(m_wname.c_str(), WidenASCII(mode).c_str());
	}
#endif
	return fopen(m_name.c_str(), mode);
}

int
FIFileName::removeFile() const {
#ifdef _WIN32
	if (!m_wname.empty()) {
		return _wremove(m_wname.c_str());
	}
#endif
	return remove(m_name.c_str());
}

int
FIFileName::renameFile(const FIFileName& dst_name) const {
#ifdef _WIN32
	if (!m_wname.empty()) {
		return _wrename(m_wname.c_str(), dst_name.m_wname.c_str());
	}
#endif
	return rename(m_name.c_str(), dst_name.m_name.c_str());
}

// ----------------------------------------------------------

CacheFile::CacheFile() :
m_file(NULL),
m_free_pages(),
m_page_cache_mem(),
m_page_cache_disk(),
m_page_map(),
// block 0 is reserved: next == 0 ends a chain
m_page_count(1),
m_current_block(NULL),
m_keep_in_memory(TRUE) {
}

CacheFile::~CacheFile() {
  close();
}

BOOL
CacheFile::open(const FIFileName& filename, BOOL keep_in_memory) {

  assert(!m_file);

  m_filename = filename;
  m_keep_in_memory = keep_in_memory;

	if ((!m_filename.empty()) && (!m_keep_in_memory)) {
		m_file = m_filename.openFile("w+b");
		return (m_file != NULL);
	}

	return (m_keep_in_memory == TRUE);
}

void
CacheFile::close() {
	// dispose the cache entries

	while (!m_page_cache_disk.empty()) {
		Block *block = *m_page_cache_disk.begin();
		m_page_cache_disk.pop_front();
		delete [] block->data;
		delete block;
	}
	while (!m_page_cache_mem.empty()) { 
		Block *block = *m_page_cache_mem.begin(); 
		m_page_cache_mem.pop_front(); 
		delete [] block->data; 
		delete block; 
	} 

	if (m_file) {
		// close the file
		fclose(m_file);
		m_file = NULL;
		
		// delete the file
		m_filename.removeFile();
	}
}

void
CacheFile::cleanupMemCache() {
	if (!m_keep_in_memory) {
		if (m_page_cache_mem.size() > CACHE_SIZE) {
			// flush the least used block to file

			Block *old_block = m_page_cache_mem.back();
			FreeImage_fseek64(m_file, (INT64)old_block->nr * BLOCK_SIZE, SEEK_SET);
			fwrite(old_block->data, BLOCK_SIZE, 1, m_file);

			// remove the data

			delete [] old_block->data;
			old_block->data = NULL;

			// move the block to another list

			m_page_cache_disk.splice(m_page_cache_disk.begin(), m_page_cache_mem, --m_page_cache_mem.end());
			m_page_map[old_block->nr] = m_page_cache_disk.begin();
		}
	}
}

int
CacheFile::allocateBlock() {
	Block *block = new Block;
	block->data = new BYTE[BLOCK_SIZE];
	block->next = 0;

	if (!m_free_pages.empty()) {
		block->nr = *m_free_pages.begin();
		m_free_pages.pop_front();
	} else {
		block->nr = m_page_count++;
	}

	m_page_cache_mem.push_front(block);
	m_page_map[block->nr] = m_page_cache_mem.begin();

	cleanupMemCache();

	return block->nr;
}

Block *
CacheFile::lockBlock(int nr) {
	if (m_current_block == NULL) {
		PageMapIt it = m_page_map.find(nr);

		if (it != m_page_map.end()) {
			m_current_block = *(it->second);

			// the block is swapped out to disc. load it back
			// and remove the block from the cache. it might get cached
			// again as soon as the memory buffer fills up

			if (m_current_block->data == NULL) {
				m_current_block->data = new BYTE[BLOCK_SIZE];

				// (INT64) first: a 32-bit product wraps past 4 GB
				FreeImage_fseek64(m_file, (INT64)m_current_block->nr * BLOCK_SIZE, SEEK_SET);
				if (fread(m_current_block->data, BLOCK_SIZE, 1, m_file) == 1) {
					m_page_cache_mem.splice(m_page_cache_mem.begin(), m_page_cache_disk, it->second);
					m_page_map[nr] = m_page_cache_mem.begin();
				}
				else {
					// back to the disk list: deleteBlock() expects no data there
					delete [] m_current_block->data;
					m_current_block->data = NULL;
					m_current_block = NULL;
					FreeImage_OutputMessageProc(FIF_UNKNOWN, "Failed to lock a block in CacheFile");
					return NULL;
				}
			}

			// if the memory cache size is too large, swap an item to disc

			cleanupMemCache();

			// return the current block

			return m_current_block;
		}
	}

	return NULL;
}

BOOL
CacheFile::unlockBlock(int nr) {
	if (m_current_block) {
		m_current_block = NULL;
		return TRUE;
	}
	return FALSE;
}

BOOL
CacheFile::deleteBlock(int nr) {
	if (m_current_block) {
		return FALSE;
	}

	PageMapIt it = m_page_map.find(nr);

	if (it != m_page_map.end()) {
		Block *block = *(it->second);

		// swapped-out blocks (no data) are in the disk list, others in memory
		if (block->data != NULL) {
			m_page_cache_mem.erase(it->second);
			delete [] block->data;
		} else {
			m_page_cache_disk.erase(it->second);
		}

		delete block;

		m_page_map.erase(it);
	}

	// add block to free page list

	m_free_pages.push_back(nr);

	return TRUE;
}

BOOL
CacheFile::readFile(BYTE *data, int nr, int size) {
	if ((data == NULL) || (size <= 0) || (nr == 0)) {
		return FALSE;
	}

	int s = 0;
	int block_nr = nr;

	do {
		const int copy_nr = block_nr;

		Block *block = lockBlock(copy_nr);

		if (block == NULL) {
			return FALSE;
		}

		block_nr = block->next;

		const int copy_size = (size - s < BLOCK_SIZE) ? (size - s) : BLOCK_SIZE;

		memcpy(data + s, block->data, copy_size);

		unlockBlock(copy_nr);

		s += copy_size;
	} while ((block_nr != 0) && (s < size));

	return (s == size) ? TRUE : FALSE;
}

int
CacheFile::writeFile(BYTE *data, int size) {
	if ((data) && (size > 0)) {
		int nr_blocks_required = (size + BLOCK_SIZE - 1) / BLOCK_SIZE;
		int count = 0;
		int s = 0;
		int stored_alloc;
		int alloc;
		
		stored_alloc = alloc = allocateBlock();

		do {
			int copy_alloc = alloc;

			Block *block = lockBlock(copy_alloc);

			block->next = 0;

			memcpy(block->data, data + s, (s + BLOCK_SIZE > size) ? size - s : BLOCK_SIZE);

			if (count + 1 < nr_blocks_required)
				alloc = block->next = allocateBlock();

			unlockBlock(copy_alloc);

			s += BLOCK_SIZE;			
		} while (++count < nr_blocks_required);

		return stored_alloc;
	}

	return 0;
}

void
CacheFile::deleteFile(int nr) {
	do {
		Block *block = lockBlock(nr);

		if (block == NULL)
			break;

		int next = block->next;

		unlockBlock(nr);

		deleteBlock(nr);

		nr = next;
	} while (nr != 0);
}

