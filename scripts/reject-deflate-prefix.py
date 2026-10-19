#!/usr/bin/env python3
"""Require the verified prefix cache and lookup to reject semantic mutations."""
import os,pathlib,shutil,subprocess
root=pathlib.Path(__file__).resolve().parents[1]
changes={
    'entry':('table[key]=(found*16+n) as u16;', 'table[key]=(found*16+n+1) as u16;', 'Huff::prefix_table'),
    'lookup':('bits.at=at+(skip+n)/8;bits.bit=(skip+n)%8;\n                return Ok(found);',
              'bits.at=at+(skip+n)/8;bits.bit=(skip+n)%8;\n                return Ok(0);', 'Huff::read'),
}
for name,(before,after,function) in changes.items():
    dest=root/'target'/('reject-deflate-prefix-'+name)
    shutil.copytree(root/'src',dest,dirs_exist_ok=True)
    p=dest/'deflate_bits.rs';source=p.read_text()
    assert source.count(before)==1,(name,source.count(before))
    p.write_text(source.replace(before,after))
    log=root/'target'/('reject-deflate-prefix-'+name+'.log')
    with log.open('w') as out:
        result=subprocess.run([os.environ['VERUS'],'--crate-type','lib','--triggers-mode','silent','--rlimit','100','--verify-only-module','deflate_bits','--verify-function',function,str(dest/'lib.rs')],cwd=root,stdout=out,stderr=subprocess.STDOUT)
    text=log.read_text()
    assert result.returncode!=0 and any(reason in text for reason in ['postcondition not satisfied','assertion failed']),(name,text)
    print('Deflate prefix gate: rejected broken '+name,flush=True)

