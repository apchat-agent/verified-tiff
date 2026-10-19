/* Run unchanged against the adapter and pinned libtiff, with explicit chop modes.
 * argv[1] optionally selects one case by name (red-run triage on older trees). */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <tiffio.h>
struct memory { unsigned char *p; size_t n, at; };
static tmsize_t rd(thandle_t h,void *p,tmsize_t n) {
  struct memory *m=h; if(n<0) return -1;
  size_t k=(size_t)n; if(k>m->n-m->at) k=m->n-m->at;
  memcpy(p,m->p+m->at,k);m->at+=k;return (tmsize_t)k;
}
static tmsize_t wr(thandle_t h,void *p,tmsize_t n) {(void)h;(void)p;(void)n;return -1;}
static toff_t sk(thandle_t h,toff_t n,int w) {
  struct memory *m=h;if(w!=SEEK_SET || n>m->n)return (toff_t)-1;m->at=n;return n;
}
static toff_t sz(thandle_t h){return ((struct memory*)h)->n;}
static int cl(thandle_t h){(void)h;return 0;}
static void u16(unsigned char *p,uint16_t v){p[0]=v;p[1]=v>>8;}
static void u32(unsigned char *p,uint32_t v){u16(p,v);u16(p+2,v>>16);}
static void field(unsigned char *p,uint16_t tag,uint16_t type,uint32_t val) {
  u16(p,tag);u16(p+2,type);u32(p+4,1);u32(p+8,val);
}
static TIFF *build(struct memory *m,const char *name,const char *mode,uint32_t w,
                   uint32_t h,uint16_t photo,uint16_t ch,size_t bytes,
                   unsigned char **store) {
  size_t n=134+bytes;unsigned char *p=calloc(1,n);assert(p);
  p[0]='I';p[1]='I';u16(p+2,42);u32(p+4,8);u16(p+8,10);
  uint16_t tags[]={256,257,258,259,262,273,277,278,279,284};
  uint32_t values[]={w,h,8,1,photo,134,ch,UINT32_MAX,(uint32_t)bytes,1};
  for(int i=0;i<10;i++)field(p+10+12*i,tags[i],4,values[i]);
  memset(p+134,0xa5,bytes);
  m->p=p;m->n=n;m->at=0;
  TIFF *t=TIFFClientOpen(name,mode,m,rd,wr,sk,cl,sz,NULL,NULL);assert(t);
  *store=p;return t;
}
static void check(const char *name,uint32_t w,uint32_t h,size_t bytes,
                  const char *mode,uint32_t expected,uint16_t photo,uint16_t ch) {
  struct memory m;unsigned char *p;
  TIFF *t=build(&m,name,mode,w,h,photo,ch,bytes,&p);
  uint32_t count=TIFFNumberOfStrips(t);
  printf("%s %s strips=%u expected=%u\n",name,mode,count,expected);fflush(stdout);
  assert(count==expected);
  assert(TIFFSetField(t,TIFFTAG_DOCUMENTNAME,"updated"));
  assert(TIFFNumberOfStrips(t)==expected);
  if(bytes==(size_t)w*h) {
    unsigned char b[8192];
    tmsize_t got=TIFFReadEncodedStrip(t,expected-1,b,sizeof(b));
    assert(got>0);for(tmsize_t i=0;i<got;i++)assert(b[i]==0xa5);
  }
  TIFFClose(t);free(p);
}
/* libtiff 4.7.1 chops a single uncompressed strip into one-row blocks and,
 * once the strip byte count is exhausted, reports zero offset and byte count
 * for the remaining strips (tif_dirread.c:7837-7840: newcounts[i]=stripbytes;
 * newoffsets[i] = stripbytes ? offset : 0). Width 8193 makes rowblockbytes
 * exceed STRIP_SIZE_DEFAULT (tif_dirread.c:7895-7898), so every chopped strip
 * holds one row and the 5-byte count covers only the first.
 * The adapter keeps declared byte counts, so its tail strips are zero.
 * libtiff cannot reach that state read-only: a single-strip byte count below
 * the image size is replaced by rowbytes*imagelength before the chop
 * (tif_dirread.c:5046-5064 via ByteCountLooksBad, estimate at
 * tif_dirread.c:5598-5604), so its strile views differ and stay unasserted
 * in the REFERENCE_LIB build. */
static void zero_tail(void) {
  struct memory m;unsigned char *p;
  TIFF *t=build(&m,"zero-tail","rC",8193,4,1,1,5,&p);
  uint32_t count=TIFFNumberOfStrips(t);
  printf("zero-tail rC strips=%u expected=4\n",count);fflush(stdout);
  assert(count==4);
#ifndef REFERENCE_LIB
  assert(TIFFGetStrileOffset(t,0)==134);
  assert(TIFFGetStrileByteCount(t,0)==5);
  assert(TIFFGetStrileOffset(t,3)==0);
  assert(TIFFGetStrileByteCount(t,3)==0);
#endif
  TIFFClose(t);free(p);
}
static int selected(int argc,char **argv,const char *name) {
  return argc<2 || strcmp(argv[1],name)==0;
}
int main(int argc,char **argv) {
  TIFFSetErrorHandler(NULL);TIFFSetWarningHandler(NULL);
  if(selected(argc,argv,"valid")) {
    check("valid",1024,40,40960,"rC",5,1,1);
    check("valid",1024,40,40960,"rc",1,1,1);
  }
  if(selected(argc,argv,"truncated")) {
    check("truncated",23809,32769,1,"rC",32769,1,1);
    check("truncated",23809,32769,1,"rc",1,1,1);
  }
  /* YCbCr (photo 6) single uncompressed strip: libtiff 4.7.1 uses the vertical
   * subsampling value as the row block (tif_dirread.c:7886-7887) and the
   * subsampled sampling-row size as the block bytes (tif_dirread.c:7890 with
   * TIFFVTileSize64; stripped images get tile dims at tif_dirread.c:4877-4878),
   * so 16x680 with 2x2 subsampling chops into two 340-row strips. */
  if(selected(argc,argv,"ycbcr"))
    check("ycbcr",16,680,16320,"rC",2,6,3);
  /* The >1,000,000-strip source safeguard keeps one strip when the file is
   * far smaller than the chopped array would describe
   * (tif_dirread.c:7921-7926; the "offset" it compares is the strip byte
   * count, tif_dirread.c:7884). Width UINT32_MAX also makes the 2 GB-strip
   * follow-up return for gigantic width (tif_dirread.c:7968-7972), so
   * libtiff 4.7.1 keeps exactly one strip. */
  if(selected(argc,argv,"large-count"))
    check("large-count",4294967295u,1000001,1,"rC",1,1,1);
  if(selected(argc,argv,"zero-tail"))
    zero_tail();
  if(argc<2)puts("chop regression: explicit modes and metadata update OK");
  return 0;
}
