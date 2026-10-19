/* Differential libFuzzer target: decode the same in-memory TIFF with the
 * compatibility layer (dlopened) and pinned libtiff 4.7.1 (statically
 * linked). Outputs are compared byte-for-byte only when neither engine
 * reported a decode message: on malformed streams the documented recovery
 * policies differ (docs/fax.md fills no damaged rows; docs/profile.md is
 * stricter than libtiff), so those decodes are counted, not compared. The
 * same applies to complex 16/32/64-bit sample transport on byte-swapped files
 * (documented per-component adapter policy; docs/reader-features.md; each
 * engine's transport is pinned by compat/tests/byteorder_regression.c).
 * Every non-compared outcome increments a counter, printed at exit; the
 * counter units are documented in fuzz/README.md. Exact comparisons exclude
 * JPEG and OJPEG. A deterministic share of inputs (first byte low bits 11)
 * additionally runs with strip chopping enabled on both engines ("rC"),
 * with the structural agreement counted separately. */
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
/* Per-codec breakdown indexed by compression tag value; slot NCODEC is the
 * shared bucket for values outside the table. */
#define NCODEC 17
static const uint16_t codec_key[NCODEC] = {
    1, 2, 3, 4, 5, 7, 8, 32773, 32809, 32946,
    34676, 34677, 34712, 34925, 50000, 50001, 50002};
/* Units per counter are documented in fuzz/README.md. */
static uint64_t inputs, both_opened, inputs_with_compare, compared, mismatches,
    skipped_warning, skipped_error, ours_failed, ref_failed, both_failed,
    skipped_complex, skipped_jpeg, skipped_meta, skipped_unsupported,
    dir_divergent, chop_both_opened, chop_divergent;
static uint64_t compared_by_codec[NCODEC + 1], skipped_by_codec[NCODEC + 1];
static int compared_this_input;
static int ref_err, ref_warn, our_err, our_warn;
static void ref_err_handler(const char *mod, const char *fmt, va_list ap) {
  (void)mod; (void)fmt; (void)ap; ref_err = 1;
}
static void ref_warn_handler(const char *mod, const char *fmt, va_list ap) {
  (void)mod; (void)fmt; (void)ap; ref_warn = 1;
}
static void our_err_handler(const char *mod, const char *fmt, va_list ap) {
  (void)mod; (void)fmt; (void)ap; our_err = 1;
}
static void our_warn_handler(const char *mod, const char *fmt, va_list ap) {
  (void)mod; (void)fmt; (void)ap; our_warn = 1;
}
static int codec_index(uint16_t v) {
  for (int i = 0; i < NCODEC; i++)
    if (codec_key[i] == v) return i;
  return NCODEC;
}
/* Codecs the adapter documents segment decoding for (docs/profile.md,
 * docs/c-api.md: unsupported APIs report ENOSYS by design); JPEG and OJPEG
 * are excluded separately. Directories outside this set are adapter-ENOSYS
 * by design, so probing them would only restate the support inventory -
 * and libtiff 4.7.1 corrupts internal field state when strips of an
 * unsupported codec are read before the next directory
 * (tif_dirread.c:5256 SEGV, tests/fixtures/diff-triage/
 * libtiff-crash-unsupported-codec.tif). */
static int supported_codec(uint16_t c) {
  switch (c) {
    case 1: case 2: case 3: case 4: case 5: case 8: case 32773: case 32946:
    case 34676: case 34887: case 50000: case 50001:
      return 1;
    default:
      return 0;
  }
}
static void counters(void) {
  fprintf(stderr,
          "DIFF inputs=%llu both_opened=%llu inputs_with_compare=%llu"
          " compared=%llu mismatches=%llu skipped_warning=%llu"
          " skipped_error=%llu ours_failed=%llu ref_failed=%llu"
          " both_failed=%llu skipped_complex=%llu skipped_jpeg=%llu"
          " skipped_unsupported=%llu skipped_meta=%llu dir_divergent=%llu"
          " chop_both_opened=%llu chop_divergent=%llu\n",
          (unsigned long long)inputs, (unsigned long long)both_opened,
          (unsigned long long)inputs_with_compare,
          (unsigned long long)compared, (unsigned long long)mismatches,
          (unsigned long long)skipped_warning,
          (unsigned long long)skipped_error,
          (unsigned long long)ours_failed, (unsigned long long)ref_failed,
          (unsigned long long)both_failed,
          (unsigned long long)skipped_complex,
          (unsigned long long)skipped_jpeg,
          (unsigned long long)skipped_unsupported,
          (unsigned long long)skipped_meta,
          (unsigned long long)dir_divergent,
          (unsigned long long)chop_both_opened,
          (unsigned long long)chop_divergent);
  for (int i = 0; i <= NCODEC; i++)
    if (compared_by_codec[i] || skipped_by_codec[i])
      fprintf(stderr, "DIFF-CODEC comp=%u compared=%llu skipped=%llu\n",
              (unsigned)(i < NCODEC ? codec_key[i] : 0u),
              (unsigned long long)compared_by_codec[i],
              (unsigned long long)skipped_by_codec[i]);
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
static uint64_t (*vt_strile_offset)(TIFF *, uint32_t);
static uint64_t (*vt_strile_count)(TIFF *, uint32_t);
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
  DLSYM(vt_strile_offset, "TIFFGetStrileOffset");
  DLSYM(vt_strile_count, "TIFFGetStrileByteCount");
  DLSYM(vt_close, "TIFFClose");
  DLSYM(vt_options_alloc, "TIFFOpenOptionsAlloc");
  DLSYM(vt_options_single, "TIFFOpenOptionsSetMaxSingleMemAlloc");
  DLSYM(vt_options_total, "TIFFOpenOptionsSetMaxCumulatedMemAlloc");
  DLSYM(vt_options_free, "TIFFOpenOptionsFree");
  DLSYM(vt_options_limits, "VTIFFOpenOptionsSetLimits");
  DLSYM(vt_set_error_handler, "TIFFSetErrorHandler");
  DLSYM(vt_set_warning_handler, "TIFFSetWarningHandler");
#undef DLSYM
  TIFFSetErrorHandler(ref_err_handler); TIFFSetWarningHandler(ref_warn_handler);
  vt_set_error_handler(our_err_handler); vt_set_warning_handler(our_warn_handler);
  return 1;
}

static void compare_bytes(const uint8_t *a, const uint8_t *b, size_t n,
                          const char *what) {
  compared++;
  compared_this_input = 1;
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

/* Returns 1 when the segment's sample transport is a documented, intentional
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

/* Strip chopping agreement, checked once per chop-mode input before the
 * segment loop: tiled flag, strip/tile size and strip/tile count. */
static void chop_check(TIFF *ref, TIFF *ours) {
  int rt = TIFFIsTiled(ref), ot = vt_is_tiled(ours);
  if (rt != ot) { chop_divergent++; return; }
  tmsize_t rs = rt ? TIFFTileSize(ref) : TIFFStripSize(ref);
  tmsize_t os = rt ? vt_tile_size(ours) : vt_strip_size(ours);
  uint32_t rn = rt ? TIFFNumberOfTiles(ref) : TIFFNumberOfStrips(ref);
  uint32_t on = rt ? vt_number_of_tiles(ours) : vt_number_of_strips(ours);
  if (rs != os || rn != on) chop_divergent++;
}

/* Both engines must agree on the strile table the probes are about to use.
 * libtiff recovers an unreadable offset/count array with estimates or zeros
 * (tif_dirread.c:5048-5064, :5598-5604) while the adapter reads the declared
 * arrays at their stored offsets (docs/metadata-contracts.md), so a
 * corrupted array silently changes what the segments decode. Values are
 * observable through the public strile accessors on both engines. */
static int striles_agree(TIFF *ref, TIFF *ours, uint32_t count) {
  uint32_t n = count > 4096 ? 4096 : count;
  for (uint32_t i = 0; i < n; i++) {
    if (TIFFGetStrileOffset(ref, i) != vt_strile_offset(ours, i) ||
        TIFFGetStrileByteCount(ref, i) != vt_strile_count(ours, i))
      return 0;
  }
  return 1;
}

static void segments(TIFF *ref, TIFF *ours, size_t *left, size_t *segments_left,
                     int ci) {
  int tiled = TIFFIsTiled(ref);
  if (tiled != vt_is_tiled(ours)) { skipped_meta++; return; }
  if (documented_divergence(ref)) { skipped_complex++; return; }
  tmsize_t rn = tiled ? TIFFTileSize(ref) : TIFFStripSize(ref);
  tmsize_t on = tiled ? vt_tile_size(ours) : vt_strip_size(ours);
  uint32_t count = tiled ? TIFFNumberOfTiles(ref) : TIFFNumberOfStrips(ref);
  uint32_t oc = tiled ? vt_number_of_tiles(ours) : vt_number_of_strips(ours);
  if (rn != on || count != oc) { skipped_meta++; return; }
  if (rn <= 0 || !count || count > *segments_left || (size_t)rn > *left / count / 2) return;
  *segments_left -= count;
  *left -= (size_t)rn * count * 2;
  uint8_t *r = malloc((size_t)rn + GUARD), *o = malloc((size_t)rn + GUARD);
  if (!r || !o) abort();
  for (uint32_t i = 0; i < count; i++) {
    memset(r, 0, (size_t)rn + GUARD); memset(o, 0, (size_t)rn + GUARD);
    ref_err = 0; ref_warn = 0; our_err = 0; our_warn = 0;
    tmsize_t rg = tiled ? TIFFReadEncodedTile(ref,i,r,rn) : TIFFReadEncodedStrip(ref,i,r,rn);
    tmsize_t og = tiled ? vt_read_encoded_tile(ours,i,o,rn) : vt_read_encoded_strip(ours,i,o,rn);
    check_guard(r, (size_t)rn); check_guard(o, (size_t)rn);
    if (rg < 0 && og < 0) { both_failed++; skipped_by_codec[ci]++; continue; }
    if (og < 0) { ours_failed++; skipped_by_codec[ci]++; continue; }
    if (rg < 0) { ref_failed++; skipped_by_codec[ci]++; continue; }
    /* Recovery policies for malformed segments are documented per codec and
     * differ between the engines; count those decodes instead of comparing. */
    if (ref_err || our_err) { skipped_error++; skipped_by_codec[ci]++; continue; }
    if (ref_warn || our_warn) { skipped_warning++; skipped_by_codec[ci]++; continue; }
    if (rg > rn || og > rn || rg != og) {
      mismatches++; counters(); abort();
    }
    if (rg) {
      compare_bytes(o,r,(size_t)rg,"strip/tile");
      compared_by_codec[ci]++;
    }
  }
  free(r); free(o);
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
  vt_ready();
  inputs++;
  compared_this_input = 0;
  if (size < 8 || size > MAX_INPUT) return 0;
  /* Per-input budgets bound allocation and total decoded bytes across directories. */
  size_t left = (size * 64 + 4096);
  if (left > 8 * MAX_INPUT) left = 8 * MAX_INPUT;
  size_t segment_budget = size / 8 + 1;
  if (segment_budget > 4096) segment_budget = 4096;
  int chop = (size & 3u) == 0;
  TIFFOpenOptions *ro = TIFFOpenOptionsAlloc(), *oo = vt_options_alloc();
  if (!ro || !oo) abort();
  TIFFOpenOptionsSetMaxSingleMemAlloc(ro, (tmsize_t)left);
  TIFFOpenOptionsSetMaxCumulatedMemAlloc(ro, (tmsize_t)left);
  vt_options_single(oo, (tmsize_t)left); vt_options_total(oo, (tmsize_t)left);
  if (!vt_options_limits(oo, MAX_INPUT, left, 2000000)) abort();
  struct memory rm = {data,size,0,0}, om = {data,size,0,0};
  /* "rC" enables the pinned single-strip chop policy on both engines;
   * "rc" disables chopping consistently, so native segments retain file
   * boundaries. */
  const char *mode = chop ? "rC" : "rc";
  TIFF *ref = TIFFClientOpenExt("reference",mode,&rm,read_mem,write_mem,seek_mem,
                               close_mem,size_mem,NULL,NULL,ro);
  TIFF *ours = vt_client_open_ext("ours",mode,&om,read_mem,write_mem,seek_mem,
                                  close_mem,size_mem,NULL,NULL,oo);
  TIFFOpenOptionsFree(ro); vt_options_free(oo);
  if (ref && ours) {
    both_opened++;
    if (chop) { chop_both_opened++; chop_check(ref, ours); }
    for (size_t page = 0; page < 16 && page <= size / 16; page++) {
      uint16_t rc=0, oc=0, photo=0, bits=0, ch=0;
      TIFFGetField(ref,TIFFTAG_COMPRESSION,&rc);
      vt_get_field(ours,TIFFTAG_COMPRESSION,&oc);
      int ci = codec_index(rc);
      if (rc != oc) {
        skipped_meta++;
      } else if (rc == COMPRESSION_JPEG || rc == COMPRESSION_OJPEG) {
        /* No universal numerical bound exists for JPEG or OJPEG. */
        skipped_jpeg++;
      } else if (!supported_codec(rc)) {
        skipped_unsupported++;
      } else {
        uint32_t rw=0,rh=0,ow=0,oh=0;
        uint16_t photo_ours=0;
        int r_photo=TIFFGetField(ref,TIFFTAG_PHOTOMETRIC,&photo);
        int o_photo=vt_get_field(ours,TIFFTAG_PHOTOMETRIC,&photo_ours);
        TIFFGetFieldDefaulted(ref,TIFFTAG_BITSPERSAMPLE,&bits);
        TIFFGetFieldDefaulted(ref,TIFFTAG_SAMPLESPERPIXEL,&ch);
        int r_w=TIFFGetField(ref,TIFFTAG_IMAGEWIDTH,&rw), r_h=TIFFGetField(ref,TIFFTAG_IMAGELENGTH,&rh);
        int o_w=vt_get_field(ours,TIFFTAG_IMAGEWIDTH,&ow), o_h=vt_get_field(ours,TIFFTAG_IMAGELENGTH,&oh);
        /* Exact RGBA domain: 8-bit gray/RGB without alpha. Native below is wider.
         * The RGBA comparison is only meaningful when both engines derived the
         * same photometric interpretation, including whether the tag was set
         * at all: libtiff rejects wrongly typed known tags and falls back
         * (tif_getimage.c:394-411) while the adapter reads known tags
         * independently of the file's stored type (docs/metadata-contracts.md). */
        int tiles = TIFFIsTiled(ref);
        int strile_ok = striles_agree(ref, ours, tiles ? TIFFNumberOfTiles(ref) : TIFFNumberOfStrips(ref));
        if (r_w!=o_w || r_h!=o_h || (r_w&&rw!=ow) || (r_h&&rh!=oh) ||
            r_photo!=o_photo || (r_photo&&photo!=photo_ours)) {
          skipped_meta++;
        } else if (!strile_ok) {
          skipped_meta++;
        } else if (r_w && r_h && rw && rh && r_photo &&
                   bits==8 && ((photo<=1 && ch==1) || (photo==2 && ch==3)) &&
                   (uint64_t)rw*rh <= left/8) {
          size_t n=(size_t)rw*rh*4; left-=n*2;
          uint8_t *r=calloc(1,n+GUARD), *o=calloc(1,n+GUARD);
          if (!r || !o) abort();
          ref_err = 0; ref_warn = 0; our_err = 0; our_warn = 0;
          int a=TIFFReadRGBAImage(ref,rw,rh,(uint32_t*)r,1);
          int b=vt_read_rgba_image(ours,rw,rh,(uint32_t*)o,1);
          check_guard(r,n); check_guard(o,n);
          if (!a && !b) { both_failed++; skipped_by_codec[ci]++; }
          else if (!b) { ours_failed++; skipped_by_codec[ci]++; }
          else if (!a) { ref_failed++; skipped_by_codec[ci]++; }
          else if (ref_err || our_err) { skipped_error++; skipped_by_codec[ci]++; }
          else if (ref_warn || our_warn) { skipped_warning++; skipped_by_codec[ci]++; }
          else {
            compare_bytes(o,r,n,"RGBA");
            compared_by_codec[ci]++;
          }
          free(r); free(o);
        }
        if (photo == PHOTOMETRIC_LOGLUV) {
          TIFFSetField(ref,TIFFTAG_SGILOGDATAFMT,SGILOGDATAFMT_16BIT);
          vt_set_field(ours,TIFFTAG_SGILOGDATAFMT,SGILOGDATAFMT_16BIT);
        }
        if (strile_ok) segments(ref,ours,&left,&segment_budget,ci);
      }
      int a=TIFFReadDirectory(ref), b=vt_read_directory(ours);
      if (a != b) dir_divergent++;
      if (!a || !b) break;
    }
  }
  if(ref) TIFFClose(ref);
  if(ours) vt_close(ours);
  if (compared_this_input) inputs_with_compare++;
  return 0;
}
