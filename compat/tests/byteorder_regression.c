/* Run unchanged against the adapter and pinned libtiff: encoded strip reads
 * deliver native-order samples for non-native-endian files, per the swap
 * width each engine documents or implements. For plain sample formats both
 * engines swap per BitsPerSample word width (libtiff tif_dir.c:265, applied
 * in tif_read.c:558). For complex SampleFormat 5/6 the adapter's documented
 * contract reverses per component, preserving real/imaginary order
 * (docs/reader-features.md), while pinned libtiff 4.7.1 swaps the whole
 * sample word on directory reads (tif_dir.c:512's per-component narrowing
 * does not fire); both policies are pinned per engine below. */
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
static void b16(unsigned char *p,uint16_t v){p[0]=v>>8;p[1]=v;}
static void b32(unsigned char *p,uint32_t v){b16(p,v>>16);b16(p+2,v);}
static void field(unsigned char *p,uint16_t tag,uint16_t type,uint32_t val) {
  b16(p,tag);b16(p+2,type);b32(p+4,1);
  /* Big-endian inline values are left-justified in the value field. */
  if(type==3)b32(p+8,val<<16);else b32(p+8,val);
}
/* Swap width for one sample: the adapter reverses per component for complex
 * formats (bits/16, no swap for 16-bit complex), everything else per word
 * width; pinned libtiff swaps per word width for every format. */
static unsigned swap_width(uint16_t bits,uint32_t sf) {
#ifdef REFERENCE_LIB
  (void)sf;
  if(bits==16) return 2;
  if(bits==24) return 3;
  if(bits==32) return 4;
  if(bits==64||bits==128) return 8;
  return 1;
#else
  unsigned w=sf==5||sf==6?bits/16:bits/8;
  if(bits==128) w=8;
  return w;
#endif
}
static void check(const char *name,uint16_t bits,uint16_t compression,uint32_t sf) {
  uint32_t w=4,h=2,bpb=bits/8,rowbytes=w*bpb,bytes=rowbytes*h;
  unsigned pack=compression==32773;
  size_t enc=pack?(rowbytes+1)*h:bytes;
  size_t n=134+enc;unsigned char *p=calloc(1,n);assert(p);
  p[0]='M';p[1]='M';b16(p+2,42);b32(p+4,8);b16(p+8,10);
  uint16_t tags[]={256,257,258,259,262,273,277,278,279,339};
  uint16_t types[]={4,4,3,3,3,4,3,4,4,3};
  uint32_t values[]={w,h,bits,compression,1,134,1,h,(uint32_t)enc,sf};
  for(int i=0;i<10;i++)field(p+10+12*i,tags[i],types[i],values[i]);
  unsigned char raw[8192],b[8192],e[8192];
  for(uint32_t i=0;i<bytes;i++)raw[i]=(unsigned char)(0x11*((i%17)+1));
  for(uint32_t r=0;r<h;r++){
    if(pack){p[134+r*(rowbytes+1)]=(unsigned char)(rowbytes-1);
      memcpy(p+134+r*(rowbytes+1)+1,raw+r*rowbytes,rowbytes);}
    else memcpy(p+134+r*rowbytes,raw+r*rowbytes,rowbytes);
  }
  struct memory m={p,n,0};
  TIFF *t=TIFFClientOpen(name,"rc",&m,rd,wr,sk,cl,sz,NULL,NULL);assert(t);
  assert(TIFFIsByteSwapped(t));
  tmsize_t got=TIFFReadEncodedStrip(t,0,b,sizeof(b));
  assert(got==(tmsize_t)bytes);
  unsigned wd=swap_width(bits,sf);
  for(uint32_t i=0;i<bytes;i++)e[i]=raw[i];
  for(size_t i=0;(size_t)i+wd<=(size_t)bytes;i+=wd)
    for(unsigned k=0;k<wd/2;k++){unsigned char v=e[i+k];e[i+k]=e[i+wd-1-k];e[i+wd-1-k]=v;}
  for(uint32_t i=0;i<bytes;i++)
    assert(b[i]==e[i]&&"strip sample bytes must arrive in native order");
  TIFFClose(t);free(p);
  printf("%s bits=%u compression=%u width=%u OK\n",name,bits,compression,wd);fflush(stdout);
}
int main(void) {
  TIFFSetErrorHandler(NULL);TIFFSetWarningHandler(NULL);
  check("byteorder-be16",16,1,1);
  check("byteorder-be32",32,1,1);
  check("byteorder-be16-packbits-complexieee",16,32773,5);
  check("byteorder-be32-packbits-complexint",32,32773,6);
  puts("byteorder regression: encoded reads in native order OK");
}
