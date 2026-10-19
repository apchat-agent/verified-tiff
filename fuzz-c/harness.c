/* Coverage-guided libFuzzer target and replay runner for the C compatibility
 * layer: in-memory client callbacks, every directory, RGBA and all
 * strips/tiles, bounded resources and output framing checks. This exercises
 * the unverified adapter; it is not a verified component. */
#include <assert.h>
#include <dirent.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <tiffio.h>
#include <verified_tiff.h>

#define GUARD 16
#define MAX_INPUT (1u << 20)
#define MAX_BUFFER (1u << 20)
#define MAX_RGBA_BYTES (1u << 20)

struct memory {
  const uint8_t *bytes;
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
static int close_mem(thandle_t p) { ((struct memory *)p)->closed++; return 0; }

static void check_tail(const uint8_t *p, size_t start, size_t len, uint8_t sentinel) {
  for (size_t j = start; j < start + len; j++)
    if (p[j] != sentinel)
      abort();
}
static void check_all(const uint8_t *p, size_t len, uint8_t sentinel) {
  for (size_t j = 0; j < len; j++)
    if (p[j] != sentinel)
      abort();
}

/* Returns nonzero when at least one operation reported an error. */
static int exercise_directory(TIFF *t) {
  int errored = 0;
  uint32_t w = 0, h = 0;
  uint16_t photo = 0;
  if (!TIFFGetField(t, TIFFTAG_IMAGEWIDTH, &w) || !TIFFGetField(t, TIFFTAG_IMAGELENGTH, &h))
    return 1;
  TIFFGetField(t, TIFFTAG_PHOTOMETRIC, &photo);
  if (photo == PHOTOMETRIC_LOGLUV)
    TIFFSetField(t, TIFFTAG_SGILOGDATAFMT, SGILOGDATAFMT_16BIT);
  if (photo == PHOTOMETRIC_YCBCR)
    TIFFSetField(t, TIFFTAG_JPEGCOLORMODE, JPEGCOLORMODE_RGB);
  if (TIFFIsTiled(t)) {
    tmsize_t n = TIFFTileSize(t);
    uint32_t count = TIFFNumberOfTiles(t);
    if (n > 0 && n <= MAX_BUFFER && count > 0 && count <= 65536 &&
        (uint64_t)n * count <= 64u << 20) {
      uint8_t *p = malloc((size_t)n + GUARD);
      assert(p);
      for (uint32_t i = 0; i < count; i++) {
        memset(p, 0xa5, (size_t)n + GUARD);
        if (TIFFReadEncodedTile(t, i, p, n) < 0) {
          errored = 1;
          check_all(p, (size_t)n, 0xa5);
        }
        check_tail(p, (size_t)n, GUARD, 0xa5);
      }
      free(p);
    }
  } else {
    tmsize_t n = TIFFStripSize(t);
    uint32_t count = TIFFNumberOfStrips(t);
    if (n > 0 && n <= MAX_BUFFER && count > 0 && count <= 1048576 &&
        (uint64_t)n * count <= 64u << 20) {
      uint8_t *p = malloc((size_t)n + GUARD);
      assert(p);
      for (uint32_t i = 0; i < count; i++) {
        memset(p, 0xa5, (size_t)n + GUARD);
        if (TIFFReadEncodedStrip(t, i, p, n) < 0) {
          errored = 1;
          check_all(p, (size_t)n, 0xa5);
        }
        check_tail(p, (size_t)n, GUARD, 0xa5);
      }
      free(p);
    }
  }
  uint64_t rgba = (uint64_t)w * h * 4;
  if (rgba > 0 && rgba <= MAX_RGBA_BYTES) {
    uint8_t *p = malloc((size_t)rgba + GUARD);
    assert(p);
    memset(p, 0xa5, (size_t)rgba + GUARD);
    if (!TIFFReadRGBAImage(t, w, h, (uint32_t *)p, 1)) {
      errored = 1;
      check_all(p, (size_t)rgba, 0xa5);
    }
    check_tail(p, (size_t)rgba, GUARD, 0xa5);
    free(p);
  }
  return errored;
}

/* Returns nonzero when the input was rejected or an operation errored. */
static int run_input(const uint8_t *data, size_t size) {
  if (size < 8 || size > MAX_INPUT)
    return 1;
  TIFFSetErrorHandler(NULL);
  TIFFSetWarningHandler(NULL);
  TIFFOpenOptions *options = TIFFOpenOptionsAlloc();
  assert(options);
  const char *work_env = getenv("VTIFF_FUZZ_WORK");
  size_t work = work_env ? (size_t)strtoull(work_env, NULL, 10) : 2000000;
  assert(VTIFFOpenOptionsSetLimits(options, MAX_INPUT, MAX_BUFFER, work));
  struct memory m = {data, size, 0, 0};
  int errored = 0;
  TIFF *t = TIFFClientOpenExt("fuzz", "r", &m, read_mem, NULL, seek_mem, close_mem,
                              size_mem, NULL, NULL, options);
  if (t) {
    unsigned dirs = 0;
    do {
      dirs++;
      if (dirs > 100000)
        abort();
      if (exercise_directory(t))
        errored = 1;
    } while (TIFFReadDirectory(t));
    TIFFClose(t);
    if (m.closed != 1)
      abort();
  } else {
    if (m.closed != 0)
      abort();
    errored = 1;
  }
  TIFFOpenOptionsFree(options);
  return errored;
}

#ifdef REPLAY_MAIN
static int cmp_names(const void *a, const void *b) {
  return strcmp(*(const char *const *)a, *(const char *const *)b);
}
int main(int argc, char **argv) {
  assert(argc == 2);
  DIR *dir = opendir(argv[1]);
  if (!dir) {
    fprintf(stderr, "cannot open C fuzz regression directory %s\n", argv[1]);
    return 2;
  }
  char **names = NULL;
  size_t count = 0, cap = 0;
  struct dirent *e;
  while ((e = readdir(dir))) {
    const char *n = e->d_name;
    size_t len = strlen(n);
    if (len > 4 && strcmp(n + len - 4, ".tif") == 0) {
      if (count == cap) {
        cap = cap ? cap * 2 : 16;
        names = realloc(names, cap * sizeof(*names));
        assert(names);
      }
      names[count] = malloc(len + strlen(argv[1]) + 2);
      assert(names[count]);
      sprintf(names[count], "%s/%s", argv[1], n);
      count++;
    }
  }
  closedir(dir);
  if (count == 0) {
    fprintf(stderr, "C fuzz regression corpus %s must not be empty\n", argv[1]);
    return 2;
  }
  qsort(names, count, sizeof(*names), cmp_names);
  size_t errored = 0, clean_accepted = 0;
  for (size_t i = 0; i < count; i++) {
    FILE *f = fopen(names[i], "rb");
    assert(f);
    assert(!fseek(f, 0, SEEK_END));
    long length = ftell(f);
    assert(length > 0);
    rewind(f);
    uint8_t *data = malloc((size_t)length);
    assert(data);
    assert(fread(data, 1, (size_t)length, f) == (size_t)length);
    fclose(f);
    if (run_input(data, (size_t)length))
      errored++;
    else
      clean_accepted++;
    free(data);
    free(names[i]);
  }
  free(names);
  /* The C layer opens lazily like libtiff, so a known-bad case may open
   * cleanly; the replay requirement is no crash and no hang, which the loop
   * above enforces by running every file. */
  printf("C fuzz replay: files=%zu errored_or_rejected=%zu clean_accepted=%zu\n", count,
         errored, clean_accepted);
  return 0;
}
#else
int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
  run_input(data, size);
  return 0;
}
#endif
