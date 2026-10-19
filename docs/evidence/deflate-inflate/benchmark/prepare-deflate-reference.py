#!/usr/bin/env python3
"""Deflate-only subset of benchmarks/prepare-reader.py; no downloads."""
import hashlib,json,pathlib,subprocess,tarfile
root=pathlib.Path.cwd();out=root/'target/reader-benchmark';out.mkdir(exist_ok=True)
lock=json.loads((root/'toolchain-lock.json').read_text())
archive=root/'.tools'/('tiff-'+lock['libtiff']+'.tar.gz')
assert hashlib.sha256(archive.read_bytes()).hexdigest()==lock['libtiff_sha256']
source=out/('tiff-'+lock['libtiff'])
if not source.exists():
    with tarfile.open(archive) as t:t.extractall(out,filter='data')
args=['./configure','--disable-shared','--disable-docs','--disable-tools','--disable-tests','--disable-libdeflate',
      '--disable-lerc','--disable-zstd','--disable-webp','--disable-jpeg','--disable-old-jpeg','--disable-jbig','--disable-lzma']
with (out/'configure.log').open('w') as f:subprocess.run(args,cwd=source,stdout=f,stderr=subprocess.STDOUT,check=True)
with (out/'build-reference.log').open('w') as f:
    subprocess.run(['make','-C','libtiff','clean'],cwd=source,stdout=f,stderr=subprocess.STDOUT,check=True)
    subprocess.run(['make','-C','libtiff','-j1'],cwd=source,stdout=f,stderr=subprocess.STDOUT,check=True)
config=(source/'libtiff/tif_config.h').read_text()+(source/'libtiff/tiffconf.h').read_text()
assert '#define ZIP_SUPPORT 1' in config
assert '#define LIBDEFLATE_SUPPORT 1' not in config
print('Fresh pinned Deflate benchmark reference:',source)
print('Configure command:',args)
