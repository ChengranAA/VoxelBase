/*
   NIFTI_ZNZ.H -- Layer 2: Transparent compressed / uncompressed file I/O

   This is a header-only library (STB-style). To use it:

     #define NIFTI_ZNZ_IMPLEMENTATION
     #include "nifti_znz.h"

   Depends on Layer 1 (nifti_base.h), which is included automatically.

   If zlib is available (HAVE_ZLIB is defined, or <zlib.h> is auto-detected),
   znzopen() can open .gz files transparently.  Without zlib, the library
   still works but compression is ignored — files are opened with plain
   fopen().

   PUBLIC DOMAIN - No warranty, use at your own risk.
*/

#ifndef NIFTI_ZNZ_H
#define NIFTI_ZNZ_H

#include "nifti_base.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------
 *  Opaque file handle
 * ------------------------------------------------------------------*/
typedef struct znzptr *znzFile;

/* ------------------------------------------------------------------
 *  API
 * ------------------------------------------------------------------*/

/* Open a file.  If use_compression is non-zero AND zlib is available,
   the file is opened via gzopen(); otherwise plain fopen() is used.
   Returns NULL on failure. */
znzFile znzopen(const char *path, const char *mode, int use_compression);

/* Close a file and set *file to NULL.  Returns 0 on success. */
int     znzclose(znzFile *file);

/* Read nmemb items of size bytes each.  Returns number of items read. */
size_t  znzread (void *buf, size_t size, size_t nmemb, znzFile file);

/* Write nmemb items of size bytes each.  Returns number of items written. */
size_t  znzwrite(const void *buf, size_t size, size_t nmemb, znzFile file);

/* Seek to an offset (supports large files on both POSIX and Windows).
   Returns the resulting offset from the beginning, or -1 on error. */
long long znzseek(znzFile file, long long offset, int whence);

/* Return the current file position, or -1 on error. */
long long znztell(znzFile file);

/* Rewind to the beginning of the file. */
void    znzrewind(znzFile file);

#ifdef __cplusplus
}
#endif

#endif /* NIFTI_ZNZ_H */

/* ====================================================================
 *  IMPLEMENTATION
 * ==================================================================== */
#ifdef NIFTI_ZNZ_IMPLEMENTATION
#ifndef NIFTI_ZNZ_IMPLEMENTED
#define NIFTI_ZNZ_IMPLEMENTED

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

/* ------------------------------------------------------------------
 *  zlib detection
 * ------------------------------------------------------------------*/

/* The user may define HAVE_ZLIB before including this file.
   If they don't, we try to auto-detect with __has_include. */
#if !defined(HAVE_ZLIB)
#  if defined(__has_include)
#    if __has_include(<zlib.h>)
#      define HAVE_ZLIB 1
#    endif
#  endif
#endif

#if defined(HAVE_ZLIB) && HAVE_ZLIB
#  include <zlib.h>
#endif

/* ------------------------------------------------------------------
 *  Large-file seek / tell
 * ------------------------------------------------------------------*/

/* On Windows use the 64-bit variants; on POSIX we rely on fseeko/ftello
   (which are 64-bit when _FILE_OFFSET_BITS=64). */
#if defined(_WIN32) || defined(_WIN64)
#  define ZNZ_FSEEK _fseeki64
#  define ZNZ_FTELL _ftelli64
#else
#  define ZNZ_FSEEK fseeko
#  define ZNZ_FTELL ftello
#endif

/* ------------------------------------------------------------------
 *  Internal structure
 * ------------------------------------------------------------------*/
struct znzptr {
    int    withz;    /* 1 if opened with gzopen, 0 for plain fopen */
    FILE  *nzfptr;   /* plain C file pointer (always valid when !withz) */
#if defined(HAVE_ZLIB) && HAVE_ZLIB
    gzFile zfptr;    /* zlib file handle (only valid when withz == 1) */
#endif
};

/* ------------------------------------------------------------------
 *  znzopen
 * ------------------------------------------------------------------*/
znzFile znzopen(const char *path, const char *mode, int use_compression)
{
    znzFile file;

    if (!path || !mode) return NULL;

    file = (znzFile)malloc(sizeof(struct znzptr));
    if (!file) return NULL;

    /* zero-initialise */
    file->withz  = 0;
    file->nzfptr = NULL;
#if defined(HAVE_ZLIB) && HAVE_ZLIB
    file->zfptr  = NULL;
#endif

#if defined(HAVE_ZLIB) && HAVE_ZLIB
    if (use_compression) {
        file->zfptr = gzopen(path, mode);
        if (file->zfptr) {
            file->withz = 1;
            return file;
        }
        /* gzopen failed — fall through to plain fopen */
    }
#else
    (void)use_compression;  /* suppress unused-parameter warning */
#endif

    file->nzfptr = fopen(path, mode);
    if (!file->nzfptr) {
        free(file);
        return NULL;
    }

    return file;
}

/* ------------------------------------------------------------------
 *  znzclose
 * ------------------------------------------------------------------*/
int znzclose(znzFile *file)
{
    int ret = 0;

    if (!file || !*file) return 0;

    if ((*file)->withz) {
#if defined(HAVE_ZLIB) && HAVE_ZLIB
        if ((*file)->zfptr) {
            ret = gzclose((*file)->zfptr);
        }
#endif
    } else {
        if ((*file)->nzfptr) {
            ret = fclose((*file)->nzfptr);
        }
    }

    free(*file);
    *file = NULL;
    return ret;
}

/* ------------------------------------------------------------------
 *  znzread
 * ------------------------------------------------------------------*/
size_t znzread(void *buf, size_t size, size_t nmemb, znzFile file)
{
    if (!file || !buf || size == 0) return 0;

    if (file->withz) {
#if defined(HAVE_ZLIB) && HAVE_ZLIB
        if (!file->zfptr) return 0;
        /* gzread can fail for very large single reads (>2GB).
           Read in 1GB chunks to stay safely below any internal
           signed-int limits in zlib. */
        {
            size_t total_bytes = size * nmemb;
            size_t bytes_read  = 0;
            char  *ptr = (char *)buf;
            while (bytes_read < total_bytes) {
                size_t remain = total_bytes - bytes_read;
                unsigned chunk = (unsigned)(remain > 0x40000000u
                                            ? 0x40000000u : remain);
                int n = gzread(file->zfptr, ptr + bytes_read, chunk);
                if (n <= 0) break;
                bytes_read += (size_t)n;
                if ((size_t)n < (size_t)chunk) break; /* EOF / short */
            }
            return bytes_read / size;
        }
#else
        return 0;
#endif
    } else {
        if (!file->nzfptr) return 0;
        return fread(buf, size, nmemb, file->nzfptr);
    }
}

/* ------------------------------------------------------------------
 *  znzwrite
 * ------------------------------------------------------------------*/
size_t znzwrite(const void *buf, size_t size, size_t nmemb, znzFile file)
{
    if (!file || !buf || size == 0) return 0;

    if (file->withz) {
#if defined(HAVE_ZLIB) && HAVE_ZLIB
        if (!file->zfptr) return 0;
        {
            unsigned nbytes = (unsigned)(size * nmemb);
            int bytes_written = gzwrite(file->zfptr, buf, nbytes);
            if (bytes_written <= 0) return 0;
            return (size_t)bytes_written / size;
        }
#else
        return 0;
#endif
    } else {
        if (!file->nzfptr) return 0;
        return fwrite(buf, size, nmemb, file->nzfptr);
    }
}

/* ------------------------------------------------------------------
 *  znzseek
 * ------------------------------------------------------------------*/
long long znzseek(znzFile file, long long offset, int whence)
{
    if (!file) return -1;

    if (file->withz) {
#if defined(HAVE_ZLIB) && HAVE_ZLIB
        if (!file->zfptr) return -1;
        {
            z_off_t result = gzseek(file->zfptr, (z_off_t)offset, whence);
            if (result < 0) return -1;
            return (long long)result;
        }
#else
        return -1;
#endif
    } else {
        if (!file->nzfptr) return -1;
        if (ZNZ_FSEEK(file->nzfptr, (long)offset, whence) != 0)
            return -1;
        return (long long)ZNZ_FTELL(file->nzfptr);
    }
}

/* ------------------------------------------------------------------
 *  znztell
 * ------------------------------------------------------------------*/
long long znztell(znzFile file)
{
    if (!file) return -1;

    if (file->withz) {
#if defined(HAVE_ZLIB) && HAVE_ZLIB
        if (!file->zfptr) return -1;
        {
            z_off_t result = gztell(file->zfptr);
            if (result < 0) return -1;
            return (long long)result;
        }
#else
        return -1;
#endif
    } else {
        if (!file->nzfptr) return -1;
        return (long long)ZNZ_FTELL(file->nzfptr);
    }
}

/* ------------------------------------------------------------------
 *  znzrewind
 * ------------------------------------------------------------------*/
void znzrewind(znzFile file)
{
    if (!file) return;

    if (file->withz) {
#if defined(HAVE_ZLIB) && HAVE_ZLIB
        if (file->zfptr) {
            gzrewind(file->zfptr);
        }
#endif
    } else {
        if (file->nzfptr) {
            rewind(file->nzfptr);
        }
    }
}

#endif /* NIFTI_ZNZ_IMPLEMENTED */
#endif /* NIFTI_ZNZ_IMPLEMENTATION */
