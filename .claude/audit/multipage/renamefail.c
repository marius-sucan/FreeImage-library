/* Fault injection for N7.
   FreeImage_CloseMultiBitmap() builds the rewritten file in a .fispool beside the
   original, then does remove(original) followed by rename(spool, original). If the
   rename fails the original is already gone. Nothing in an ordinary run makes
   rename() fail, so this shim makes it fail on demand.

   Build and use:
     gcc -shared -fPIC -o renamefail.so renamefail.c -ldl
     FAIL_RENAME_SUFFIX=.fispool LD_PRELOAD=./renamefail.so ./ncheck n7
*/
#define _GNU_SOURCE
#include <dlfcn.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int rename(const char *oldpath, const char *newpath) {
	static int (*real_rename)(const char *, const char *) = NULL;
	const char *want = getenv("FAIL_RENAME_SUFFIX");

	if (want != NULL && oldpath != NULL && strstr(oldpath, want) != NULL) {
		fprintf(stderr, "    [renamefail] rename(\"%s\" -> \"%s\") refused\n", oldpath, newpath);
		errno = EACCES;
		return -1;
	}

	if (real_rename == NULL) {
		real_rename = (int (*)(const char *, const char *))dlsym(RTLD_NEXT, "rename");
		if (real_rename == NULL) {
			errno = ENOSYS;
			return -1;
		}
	}
	return real_rename(oldpath, newpath);
}
