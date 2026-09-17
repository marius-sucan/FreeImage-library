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

CacheFile::CacheFile() :
m_file(NULL),
m_free_pages(),
m_page_cache_mem(),
m_page_cache_disk(),
m_page_map(),
// Block numbers start at 1, not 0. Block::next == 0 is what marks the end of a
// chain, so a block really numbered 0 means two things at once: while it happened
// to be the head of its chain nobody noticed, but as soon as it was freed and handed
// out again as a *continuation*, readFile() read the block before it, saw next == 0
// and stopped - returning a page with its tail missing, which then failed to decode
// and cost the caller the whole document. Giving up the first BLOCK_SIZE of the
// cache file is the entire price of never having to tell the two apart.
// It also makes writeFile()'s 0 an unambiguous failure return.
m_page_count(1),
m_current_block(NULL),
m_keep_in_memory(TRUE) {
}

CacheFile::~CacheFile() {
  close();
}

BOOL
CacheFile::open(const std::string& filename, BOOL keep_in_memory) {

  assert(!m_file);

  m_filename = filename;
  m_keep_in_memory = keep_in_memory;

	if ((!m_filename.empty()) && (!m_keep_in_memory)) {
		m_file = fopen(m_filename.c_str(), "w+b"); 
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
		remove(m_filename.c_str());
	}
}

void
CacheFile::cleanupMemCache() {
	if (!m_keep_in_memory) {
		if (m_page_cache_mem.size() > CACHE_SIZE) {
			// flush the least used block to file

			Block *old_block = m_page_cache_mem.back();
			// (long) before the multiply - see lockBlock()
			fseek(m_file, (long)old_block->nr * BLOCK_SIZE, SEEK_SET);
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

				// (long) before the multiply: nr is unsigned and BLOCK_SIZE an int,
				// so the product was worked out in 32 bits and wrapped once the cache
				// passed 4 GB, seeking to an offset belonging to another block
				fseek(m_file, (long)m_current_block->nr * BLOCK_SIZE, SEEK_SET);
				if (fread(m_current_block->data, BLOCK_SIZE, 1, m_file) == 1) {
					m_page_cache_mem.splice(m_page_cache_mem.begin(), m_page_cache_disk, it->second);
					m_page_map[nr] = m_page_cache_mem.begin();
				}
				else {
					// Put the block back as it was found: it is still the disk list's,
					// and a block with data sitting in that list breaks the rule
					// deleteBlock() relies on to know which list to take it out of.
					// Leaving m_current_block set, as this path used to, also made
					// every later lockBlock() return NULL - one failed read and the
					// cache was wedged for good.
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

		// Take the block out of the list that holds it. Only the map entry used to
		// go, which left the block itself sitting in the memory cache with nothing
		// pointing at it: the number is about to be handed out again, and the next
		// cleanupMemCache() would flush that stale block to the file at an offset
		// belonging to whichever page now owns the number. It also never got freed
		// before close().
		// A block that has been swapped out has no data and lives in the disk list;
		// one that still has data lives in the memory list.
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

		// lockBlock() returns NULL for a number that is not in the map and when the
		// read of a swapped-out block fails. Neither was checked, so a chain that
		// did not lead where it said it did was a null dereference.
		if (block == NULL) {
			return FALSE;
		}

		block_nr = block->next;

		// Never copy past the end of the caller's buffer. The old arithmetic went
		// negative once s ran past size - a chain longer than the page it holds
		// turned "size - s" into a huge size_t and the memcpy into a heap overflow.
		const int copy_size = (size - s < BLOCK_SIZE) ? (size - s) : BLOCK_SIZE;

		memcpy(data + s, block->data, copy_size);

		unlockBlock(copy_nr);

		s += copy_size;
	} while ((block_nr != 0) && (s < size));

	// A chain that ends early has not delivered the page, and handing back a
	// half-filled buffer as a success is how a truncated block chain used to reach
	// the decoder as a corrupt file.
	return (s == size) ? TRUE : FALSE;
}

int
CacheFile::writeFile(BYTE *data, int size) {
	if ((data) && (size > 0)) {
		// round up, rather than always adding one: a page whose length was an exact
		// multiple of BLOCK_SIZE used to claim a block it had nothing to put in
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

