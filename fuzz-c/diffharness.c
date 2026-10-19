/* Differential libFuzzer target: decode the same in-memory TIFF with the
 * compatibility layer (dlopened) and pinned libtiff 4.7.1 (statically
 * linked). Outputs are compared byte-for-byte only when neither engine
 * reported a decode message: on malformed streams the documented recovery
 * policies differ (docs/fax.md fills no damaged rows; docs/profile.md is
 * stricter than libtiff), so those decodes are counted, not compared. The
 * same applies to complex 16/32/64-bit sample transport on byte-swapped files
 * (documented per-component adapter policy; docs/reader-features.md).
 * Exact comparisons exclude JPEG and OJPEG. */
#include <assert.h>
#include <dlfcn.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <tiffio.h>

#define GUARD 16
#define MAX_INPUT (1u << 20)
static uint64_t inputs, both_opened, compared, skipped_malformed,
    skipped_complex, mismatches;
static int ref_msg, our_msg;
static void ref_msg_handler(const char *mod, const char *fmt, va_list ap) {
  (void)mod; (void)fmt; (void)ap; ref_msg = 1;
}
static void our_msg_handler(const char *mod, const char *fmt, va_list ap) {
  (void)mod; (void)fmt; (void)ap; our_msg = 1;
}
static void counters(void) {
  fprintf(stderr, "DIFF inputs=%llu both_opened=%llu compared=%llu skipped_malformed=%llu skipped_complex=%llu mismatches=%llu\n",
    (unsigned long long)inputs, (unsigned long long)both_opened,
    (unsigned long long)compared, (unsigned long long)skipped_malformed,
    (unsigned long long)skipped_complex, (unsigned long long)mismatches);
}

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
static tmsize_t write_mem(thandle_t p, void *b, tmsize_t n) { (void)p; (void)b; (void)n; return -1; }
static toff_t seek_mem(thandle_t p, toff_t n, int origin) {
  struct memory *m = p;
  if (origin != SEEK_SET || n > m->size)
    return (toff_t)-1;
  m->at = (size_t)n;
  return n;
}
static toff_t size_mem(thandle_t p) { return ((struct memory *)p)->size; }
static int close_mem(thandle_t p) { ((struct memory *)p)->closed++; return 0; }

/* Compatibility-layer entry points, resolved once through dlopen. */
static TIFF *(*vt_client_open_ext)(const char *, const char *, thandle_t,
                                   TIFFReadWriteProc, TIFFReadWriteProc, TIFFSeekProc,
                                   TIFFCloseProc, TIFFSizeProc, TIFFMapFileProc,
                                   TIFFUnmapFileProc, TIFFOpenOptions *);
static int (*vt_read_directory)(TIFF *);
static int (*vt_get_field)(TIFF *, uint32_t, ...);
static int (*vt_set_field)(TIFF *, uint32_t, ...);
static int (*vt_is_tiled)(TIFF *);
static tmsize_t (*vt_tile_size)(TIFF *);
static tmsize_t (*vt_strip_size)(TIFF *);
static uint32_t (*vt_number_of_tiles)(TIFF *);
static uint32_t (*vt_number_of_strips)(TIFF *);
static tmsize_t (*vt_read_encoded_tile)(TIFF *, uint32_t, void *, tmsize_t);
static tmsize_t (*vt_read_encoded_strip)(TIFF *, uint32_t, void *, tmsize_t);
static int (*vt_read_rgba_image)(TIFF *, uint32_t, uint32_t, uint32_t *, int);
static void (*vt_close)(TIFF *);
static TIFFOpenOptions *(*vt_options_alloc)(void);
static void (*vt_options_single)(TIFFOpenOptions *, tmsize_t);
static void (*vt_options_total)(TIFFOpenOptions *, tmsize_t);
static void (*vt_options_free)(TIFFOpenOptions *);
static int (*vt_options_limits)(TIFFOpenOptions *, uint64_t, uint64_t, uint64_t);
static void (*vt_set_error_handler)(TIFFErrorHandler);
static void (*vt_set_warning_handler)(TIFFErrorHandler);

static void *vt_handle;

static int vt_ready(void) {
  if (vt_handle)
    return 1;
  const char *path = getenv("VTIFF_COMPAT_SO");
  vt_handle = dlopen(path && *path ? path : "target/compat/libtiff.so.6", RTLD_NOW | RTLD_LOCAL);
  if (!vt_handle) { fprintf(stderr, "dlopen: %s\n", dlerror()); abort(); }
  atexit(counters);
#define DLSYM(field, name)                                                    \
  do {                                                                        \
    *(void **)(&field) = dlsym(vt_handle, name);                              \
    if (!(field))                                                             \
      abort();                                                                \
  } while (0)
  DLSYM(vt_client_open_ext, "TIFFClientOpenExt");
  DLSYM(vt_read_directory, "TIFFReadDirectory");
  DLSYM(vt_get_field, "TIFFGetField");
  DLSYM(vt_set_field, "TIFFSetField");
  DLSYM(vt_is_tiled, "TIFFIsTiled");
  DLSYM(vt_tile_size, "TIFFTileSize");
  DLSYM(vt_strip_size, "TIFFStripSize");
  DLSYM(vt_number_of_tiles, "TIFFNumberOfTiles");
  DLSYM(vt_number_of_strips, "TIFFNumberOfStrips");
  DLSYM(vt_read_encoded_tile, "TIFFReadEncodedTile");
  DLSYM(vt_read_encoded_strip, "TIFFReadEncodedStrip");
  DLSYM(vt_read_rgba_image, "TIFFReadRGBAImage");
  DLSYM(vt_close, "TIFFClose");
  DLSYM(vt_options_alloc, "TIFFOpenOptionsAlloc");
  DLSYM(vt_options_single, "TIFFOpenOptionsSetMaxSingleMemAlloc");
  DLSYM(vt_options_total, "TIFFOpenOptionsSetMaxCumulatedMemAlloc");
  DLSYM(vt_options_free, "TIFFOpenOptionsFree");
  DLSYM(vt_options_limits, "VTIFFOpenOptionsSetLimits");
  DLSYM(vt_set_error_handler, "TIFFSetErrorHandler");
  DLSYM(vt_set_warning_handler, "TIFFSetWarningHandler");
#undef DLSYM
  TIFFSetErrorHandler(ref_msg_handler); TIFFSetWarningHandler(ref_msg_handler);
  vt_set_error_handler(our_msg_handler); vt_set_warning_handler(our_msg_handler);
  return 1;
}

static void compare_bytes(const uint8_t *a, const uint8_t *b, size_t n,
                          const char *what) {
  compared++;
  for (size_t i=0; i<n; i++) if(a[i]!=b[i]) {
    mismatches++;
    fprintf(stderr,"differential mismatch in %s at byte %zu: ours=%u libtiff=%u\n",
            what,i,a[i],b[i]);
    counters(); abort();
  }
}

static void check_guard(const uint8_t *p, size_t start) {
  for (size_t j = start; j < start + GUARD; j++)
    if (p[j] != 0)
      abort();
}


/* Per-input budgets bound allocation and total decoded bytes across directories.
 * Returns 1 when the segment's sample transport is a documented, intentional
 * divergence from libtiff (complex SampleFormat 5/6 with 16/32/64-bit samples
 * on byte-swapped files: the adapter reverses per component, preserving
 * real/imaginary order per docs/reader-features.md, while libtiff 4.7.1
 * swaps the whole sample word on directory reads), so byte comparison would
 * be wrong. */
static int documented_divergence(TIFF *ref) {
  if (!TIFFIsByteSwapped(ref))
    return 0;
  uint16_t fmt = 0, bits = 1;
  TIFFGetFieldDefaulted(ref, TIFFTAG_SAMPLEFORMAT, &fmt);
  TIFFGetFieldDefaulted(ref, TIFFTAG_BITSPERSAMPLE, &bits);
  return (fmt == SAMPLEFORMAT_COMPLEXINT || fmt == SAMPLEFORMAT_COMPLEXIEEEFP) &&
         (bits == 16 || bits == 32 || bits == 64);
}

static void segments(TIFF *ref, TIFF *ours, size_t *left, size_t *segments_left) {
  int tiled = TIFFIsTiled(ref);
  if (tiled != vt_is_tiled(ours)) return;
  if (documented_divergence(ref)) { skipped_complex++; return; }
  tmsize_t rn = tiled ? TIFFTileSize(ref) : TIFFStripSize(ref);
  tmsize_t on = tiled ? vt_tile_size(ours) : vt_strip_size(ours);
  uint32_t count = tiled ? TIFFNumberOfTiles(ref) : TIFFNumberOfStrips(ref);
  uint32_t oc = tiled ? vt_number_of_tiles(ours) : vt_number_of_strips(ours);
  if (rn <= 0 || rn != on || !count || count != oc ||
      count > *segments_left || (size_t)rn > *left / count / 2) return;
  *segments_left -= count;
  *left -= (size_t)rn * count * 2;
  uint8_t *r = malloc((size_t)rn + GUARD), *o = malloc((size_t)rn + GUARD);
  if (!r || !o) abort();
  for (uint32_t i = 0; i < count; i++) {
    memset(r, 0, (size_t)rn + GUARD); memset(o, 0, (size_t)rn + GUARD);
    ref_msg = 0; our_msg = 0;
    tmsize_t rg = tiled ? TIFFReadEncodedTile(ref,i,r,rn) : TIFFReadEncodedStrip(ref,i,r,rn);
    tmsize_t og = tiled ? vt_read_encoded_tile(ours,i,o,rn) : vt_read_encoded_strip(ours,i,o,rn);
    check_guard(r, (size_t)rn); check_guard(o, (size_t)rn);
    /* Recovery policies for malformed segments are documented per codec and
     * differ between the engines; count those decodes instead of comparing. */
    if (ref_msg || our_msg) { skipped_malformed++; continue; }
    if (rg >= 0 && og >= 0) {
      if (rg > rn || og > rn || rg != og) {
        mismatches++; counters(); abort();
      }
      if (rg) compare_bytes(o,r,(size_t)rg,"strip/tile");
    }
  }
  free(r); free(o);
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
  vt_ready();
  inputs++;
  if (size < 8 || size > MAX_INPUT) return 0;
  size_t left = (size * 64 + 4096);
  if (left > 8 * MAX_INPUT) left = 8 * MAX_INPUT;
  size_t segment_budget = size / 8 + 1;
  if (segment_budget > 4096) segment_budget = 4096;
  TIFFOpenOptions *ro = TIFFOpenOptionsAlloc(), *oo = vt_options_alloc();
  if (!ro || !oo) abort();
  TIFFOpenOptionsSetMaxSingleMemAlloc(ro, (tmsize_t)left);
  TIFFOpenOptionsSetMaxCumulatedMemAlloc(ro, (tmsize_t)left);
  vt_options_single(oo, (tmsize_t)left); vt_options_total(oo, (tmsize_t)left);
  if (!vt_options_limits(oo, MAX_INPUT, left, 2000000)) abort();
  struct memory rm = {data,size,0,0}, om = {data,size,0,0};
  /* Disable chopping consistently; native segments then retain file boundaries. */
  TIFF *ref = TIFFClientOpenExt("reference","rc",&rm,read_mem,write_mem,seek_mem,
                               close_mem,size_mem,NULL,NULL,ro);
  TIFF *ours = vt_client_open_ext("ours","rc",&om,read_mem,write_mem,seek_mem,
                                  close_mem,size_mem,NULL,NULL,oo);
  TIFFOpenOptionsFree(ro); vt_options_free(oo);
  if (ref && ours) {
    both_opened++;
    for (size_t page = 0; page < 16 && page <= size / 16; page++) {
      uint16_t rc=0, oc=0, photo=0, bits=0, ch=0;
      TIFFGetField(ref,TIFFTAG_COMPRESSION,&rc);
      vt_get_field(ours,TIFFTAG_COMPRESSION,&oc);
      /* No universal numerical bound exists for JPEG or OJPEG. */
      if (rc == oc && rc != COMPRESSION_JPEG && rc != COMPRESSION_OJPEG) {
        uint32_t rw=0,rh=0,ow=0,oh=0;
        TIFFGetField(ref,TIFFTAG_PHOTOMETRIC,&photo);
        TIFFGetFieldDefaulted(ref,TIFFTAG_BITSPERSAMPLE,&bits);
        TIFFGetFieldDefaulted(ref,TIFFTAG_SAMPLESPERPIXEL,&ch);
        int geom = TIFFGetField(ref,TIFFTAG_IMAGEWIDTH,&rw) &&
          TIFFGetField(ref,TIFFTAG_IMAGELENGTH,&rh) &&
          vt_get_field(ours,TIFFTAG_IMAGEWIDTH,&ow) &&
          vt_get_field(ours,TIFFTAG_IMAGELENGTH,&oh);
        /* Exact RGBA domain: 8-bit gray/RGB without alpha. Native below is wider. */
        if (geom && rw==ow && rh==oh && rw && rh &&
            bits==8 && ((photo<=1 && ch==1) || (photo==2 && ch==3)) &&
            (uint64_t)rw*rh <= left/8) {
          size_t n=(size_t)rw*rh*4; left-=n*2;
          uint8_t *r=calloc(1,n+GUARD), *o=calloc(1,n+GUARD);
          if (!r || !o) abort();
          ref_msg = 0; our_msg = 0;
          int a=TIFFReadRGBAImage(ref,rw,rh,(uint32_t*)r,1);
          int b=vt_read_rgba_image(ours,rw,rh,(uint32_t*)o,1);
          check_guard(r,n); check_guard(o,n);
          if (ref_msg || our_msg) skipped_malformed++;
          else if(a && b) compare_bytes(o,r,n,"RGBA");
          free(r); free(o);
        }
        if (photo == PHOTOMETRIC_LOGLUV) {
          TIFFSetField(ref,TIFFTAG_SGILOGDATAFMT,SGILOGDATAFMT_16BIT);
          vt_set_field(ours,TIFFTAG_SGILOGDATAFMT,SGILOGDATAFMT_16BIT);
        }
        segments(ref,ours,&left,&segment_budget);
      }
      int a=TIFFReadDirectory(ref), b=vt_read_directory(ours);
      if (!a || !b) break;
    }
  }
  if(ref) TIFFClose(ref);
  if(ours) vt_close(ours);
  return 0;
}
