#!/usr/bin/env python3
"""Require bulk ring, stored cursor and exact-work refinements to reject mutations."""
import os,pathlib,shutil,subprocess
root=pathlib.Path(__file__).resolve().parents[1]
changes={
    'ring':('deflate.rs','dest.copy_from_slice(data);','dest.copy_from_slice(data);dest[0]=0;','deflate','Cursor::append_run'),
    'stored-position':('deflate.rs','if self.mode==1 {self.bits.at=self.bits.at+n;}','if self.mode==1 {self.bits.at=self.bits.at+n-1;}','deflate','Cursor::copy_run'),
    'work':('native_stream.rs','spend(budget,2*n)?;','spend(budget,n)?;','native_stream','Inflater::pull'),
}
for name,(file,before,after,module,function) in changes.items():
    dest=root/'target'/('reject-deflate-bulk-'+name)
    shutil.copytree(root/'src',dest,dirs_exist_ok=True)
    p=dest/file;source=p.read_text()
    assert source.count(before)==1,(name,source.count(before))
    p.write_text(source.replace(before,after))
    log=root/'target'/('reject-deflate-bulk-'+name+'.log')
    with log.open('w') as out:
        result=subprocess.run([os.environ['VERUS'],'--crate-type','lib','--triggers-mode','silent','--rlimit','100','--verify-only-module',module,'--verify-function',function,str(dest/'lib.rs')],cwd=root,stdout=out,stderr=subprocess.STDOUT)
    text=log.read_text()
    assert result.returncode!=0 and any(reason in text for reason in ['postcondition not satisfied','assertion failed','invariant not satisfied']),(name,text)
    print('Deflate bulk gate: rejected broken '+name,flush=True)

