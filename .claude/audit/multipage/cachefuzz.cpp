/* Focused stress test for CacheFile, the block store behind the multi-page cache.
   It drives the class directly rather than through FreeImage_*MultiBitmap, so it can
   hammer the paths that C1 lives in - block allocation, chaining, reuse after a
   delete, and swapping to disk - far harder than a document ever would, and under
   AddressSanitizer.

   Build (see the Makefile target "cachefuzz"):
     g++ -g -O1 -fsanitize=address,undefined -I../../../Source -I../../../Source/FreeImage \
         cachefuzz.cpp ../../../Source/FreeImage/CacheFile.cpp -o cachefuzz
*/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vector>
#include "CacheFile.h"

/* CacheFile reports through this; the rest of the library is not linked in. */
void DLL_CALLCONV FreeImage_OutputMessageProc(int fif, const char *fmt, ...) {
	(void)fif; (void)fmt;
}

static unsigned g_seed = 12345;
static unsigned rnd(void) { g_seed = g_seed * 1103515245u + 12345u; return g_seed >> 8; }

struct Stored {
	int ref;
	std::vector<BYTE> data;
};

/* one round: random writes, reads and deletes against one CacheFile */
static int round_trip(bool keep_in_memory, int iterations, int max_size, const char *label) {
	CacheFile cache;
	std::vector<Stored> live;
	int failures = 0, writes = 0, reads = 0, deletes = 0;

	if (!cache.open(keep_in_memory ? "" : "cachefuzz.ficache", keep_in_memory ? TRUE : FALSE)) {
		printf("  %-28s open FAILED\n", label);
		return 1;
	}

	for (int i = 0; i < iterations; i++) {
		const unsigned action = rnd() % 10;

		if ((action < 5) || live.empty()) {
			/* write a payload of a random size, including sizes either side of a
			   block boundary - the case that needs more than one block is the one
			   that used to walk into block 0 */
			Stored s;
			int size = 1 + (int)(rnd() % (unsigned)max_size);
			if ((rnd() % 4) == 0) {
				/* land exactly on a block boundary now and then */
				size = BLOCK_SIZE * (1 + (int)(rnd() % 3));
			}
			s.data.resize((size_t)size);
			for (int k = 0; k < size; k++) s.data[(size_t)k] = (BYTE)(rnd() & 0xFF);

			s.ref = cache.writeFile(&s.data[0], size);
			writes++;
			if (s.ref == 0) {
				printf("  %-28s writeFile returned 0 (size %d)\n", label, size);
				failures++;
				continue;
			}
			live.push_back(s);

		} else if (action < 8) {
			/* read one back and check it byte for byte */
			const size_t idx = (size_t)(rnd() % (unsigned)live.size());
			Stored& s = live[idx];
			std::vector<BYTE> got(s.data.size());

			reads++;
			if (!cache.readFile(&got[0], s.ref, (int)s.data.size())) {
				printf("  %-28s readFile FAILED (ref %d, %u bytes)\n",
				       label, s.ref, (unsigned)s.data.size());
				failures++;
			} else if (memcmp(&got[0], &s.data[0], s.data.size()) != 0) {
				printf("  %-28s readFile returned WRONG DATA (ref %d, %u bytes)\n",
				       label, s.ref, (unsigned)s.data.size());
				failures++;
			}

		} else {
			/* delete one, so its block numbers go back on the free list and get
			   handed out again - this is what turned block 0 into a continuation */
			const size_t idx = (size_t)(rnd() % (unsigned)live.size());
			cache.deleteFile(live[idx].ref);
			deletes++;
			live.erase(live.begin() + (long)idx);
		}
	}

	/* everything still live must still read back correctly */
	for (size_t i = 0; i < live.size(); i++) {
		std::vector<BYTE> got(live[i].data.size());
		if (!cache.readFile(&got[0], live[i].ref, (int)live[i].data.size())) {
			printf("  %-28s final readFile FAILED (ref %d)\n", label, live[i].ref);
			failures++;
		} else if (memcmp(&got[0], &live[i].data[0], live[i].data.size()) != 0) {
			printf("  %-28s final data WRONG (ref %d)\n", label, live[i].ref);
			failures++;
		}
	}

	printf("  %-28s %d writes, %d reads, %d deletes, %u still live -> %s\n",
	       label, writes, reads, deletes, (unsigned)live.size(),
	       failures ? "*** FAILURES ***" : "ok");
	return failures;
}

/* the exact C1 shape: free the blocks so that 0 would be handed out as a
   continuation rather than as the head of a chain */
static int block_zero_shape(void) {
	CacheFile cache;
	int failures = 0;

	if (!cache.open("", TRUE)) { printf("  open failed\n"); return 1; }

	/* three single-block payloads - under the old code these were blocks 0, 1, 2 */
	BYTE small[16];
	memset(small, 0xAB, sizeof(small));
	int a = cache.writeFile(small, (int)sizeof(small));
	int b = cache.writeFile(small, (int)sizeof(small));
	int c = cache.writeFile(small, (int)sizeof(small));
	printf("  first three block numbers: %d, %d, %d %s\n", a, b, c,
	       (a != 0 && b != 0 && c != 0) ? "(none is 0 - good)" : "*** 0 IS IN USE ***");
	if (a == 0 || b == 0 || c == 0) failures++;

	/* free them in the order that used to put 0 second on the free list */
	cache.deleteFile(b);
	cache.deleteFile(a);

	/* now a payload needing two blocks: the head and the continuation come off the
	   free list in that order */
	std::vector<BYTE> big((size_t)BLOCK_SIZE + 4096);
	for (size_t i = 0; i < big.size(); i++) big[i] = (BYTE)(i * 7);

	int ref = cache.writeFile(&big[0], (int)big.size());
	if (ref == 0) { printf("  writeFile of a two-block payload FAILED\n"); return failures + 1; }

	std::vector<BYTE> got(big.size());
	if (!cache.readFile(&got[0], ref, (int)big.size())) {
		printf("  readFile of the two-block payload FAILED\n");
		failures++;
	} else if (memcmp(&got[0], &big[0], big.size()) != 0) {
		printf("  two-block payload came back CORRUPT\n");
		failures++;
	} else {
		printf("  two-block payload after the deletes: intact\n");
	}

	/* and the survivor is still readable */
	BYTE back[16];
	if (!cache.readFile(back, c, (int)sizeof(back)) || memcmp(back, small, sizeof(small)) != 0) {
		printf("  the surviving single-block payload is gone\n");
		failures++;
	}

	return failures;
}

int main(void) {
	int failures = 0;

	/* unbuffered: a run against the unfixed code aborts inside memcpy, and the
	   results printed up to that point are worth seeing */
	setvbuf(stdout, NULL, _IONBF, 0);

	printf("CacheFile stress (BLOCK_SIZE = %d, CACHE_SIZE = %d blocks)\n\n", BLOCK_SIZE, CACHE_SIZE);

	printf("the C1 shape:\n");
	failures += block_zero_shape();

	printf("\nrandomised:\n");
	/* small payloads: mostly one block each, lots of reuse */
	failures += round_trip(true,  4000, 4096, "memory cache, small");
	failures += round_trip(false, 4000, 4096, "disk cache, small");
	/* payloads spanning several blocks, enough of them to overflow the 32-block
	   memory cache and force real swapping to the file */
	failures += round_trip(true,  1200, BLOCK_SIZE * 3, "memory cache, multi-block");
	failures += round_trip(false, 1200, BLOCK_SIZE * 3, "disk cache, multi-block");

	printf("\n%s\n", failures ? "*** FAILURES ***" : "all clear");
	return failures ? 1 : 0;
}
