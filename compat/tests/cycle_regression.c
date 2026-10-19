/* A cyclic IFD chain must end the sequential directory walk like libtiff's
 * loop detection, not walk bounded-max_directories revisited directories.
 * This regression is adapter-only (verified_tiff.h): the libtiff semantics it
 * cites — tif_dirread.c:5696 checking offset and directory number together,
 * tif_dir.c:2057 permitting the same offset at the same directory number —
 * rest on those pinned-source lines, and the same binary is not run against
 * libtiff. */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <tiffio.h>
#include <verified_tiff.h>
struct memory {
  uint8_t *bytes;
  size_t size, at;
  unsigned closed;
};
static tmsize_t read_mem(thandle_t p, void *out, tmsize_t n) {
  struct memory *m = p;
  if (n < 0)
    return -1;
  size_t take = (size_t)n;
  if (take > m->size - m->at)
    take = m->size - m->at;
  memcpy(out, m->bytes + m->at, take);
  m->at += take;
  return (tmsize_t)take;
}
static toff_t seek_mem(thandle_t p, toff_t n, int origin) {
  struct memory *m = p;
  if (origin != SEEK_SET || n > m->size)
    return (toff_t)-1;
  m->at = (size_t)n;
  return n;
}
static toff_t size_mem(thandle_t p) { return ((struct memory *)p)->size; }
static int close_mem(thandle_t p) {
  ((struct memory *)p)->closed++;
  return 0;
}
static void pixel(TIFF *t, uint64_t off) {
  unsigned char b=0;
  assert(TIFFCurrentDirOffset(t)==off);
  assert(TIFFReadEncodedStrip(t,0,&b,1)==1 && b==0xa5);
}
int main(int argc, char **argv) {
  assert(argc == 2);
  FILE *f = fopen(argv[1], "rb");
  assert(f);
  assert(!fseek(f, 0, SEEK_END));
  long length = ftell(f);
  assert(length > 0);
  rewind(f);
  uint8_t *data = malloc((size_t)length);
  assert(data);
  assert(fread(data, 1, (size_t)length, f) == (size_t)length);
  fclose(f);
  TIFFSetErrorHandler(NULL);
  TIFFSetWarningHandler(NULL);
  TIFFOpenOptions *options = TIFFOpenOptionsAlloc();
  assert(options);
  assert(VTIFFOpenOptionsSetLimits(options, 1 << 20, 1 << 20, 2000000));
  struct memory m = {data, (size_t)length, 0, 0};
  TIFF *t = TIFFClientOpenExt("cycle", "r", &m, read_mem, NULL, seek_mem, close_mem,
                              size_mem, NULL, NULL, options);
  assert(t);
  pixel(t,8);
  assert(TIFFReadDirectory(t)); pixel(t,110);
  assert(!TIFFReadDirectory(t));
  assert(TIFFSetDirectory(t,0)); pixel(t,8);
  assert(TIFFReadDirectory(t)); pixel(t,110);
  assert(!TIFFReadDirectory(t));
  assert(TIFFSetSubDirectory(t,8)); pixel(t,8);
  assert(TIFFReadDirectory(t)); pixel(t,110);
  TIFFClose(t);
  /* Same two directories with an acyclic terminator. */
  memset(data+208,0,4);
  m.at=0;
  t=TIFFClientOpenExt("acyclic","r",&m,read_mem,NULL,seek_mem,close_mem,
                      size_mem,NULL,NULL,options);
  assert(t); pixel(t,8);
  assert(TIFFReadDirectory(t)); pixel(t,110);
  assert(!TIFFReadDirectory(t));
  assert(TIFFSetDirectory(t,0));
  assert(TIFFReadDirectory(t)); pixel(t,110);
  assert(TIFFSetSubDirectory(t,8));
  assert(TIFFReadDirectory(t)); pixel(t,110);
  TIFFClose(t);
  TIFFOpenOptionsFree(options);
  free(data);
  puts("cycle regression: exact progression, cycles, acyclic reselection, subdirectories OK");
  return 0;
}
