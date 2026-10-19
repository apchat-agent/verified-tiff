#!/usr/bin/env python3
import json,sys
r=json.load(open(sys.argv[1]));candidate=sys.argv[2] if len(sys.argv)>2 else 'final'
assert r['rounds']>=9
assert len(r['records'])==r.get('case_count',len(r['records']))
failed=[]
for c in r['records']:
 assert all(len(v)==r['rounds'] for v in c['samples'].values())
 ratio=c['median_ms'][candidate]/c['median_ms']['baseline']
 print(c['name'],f'{ratio:.6f}', 'FAIL' if ratio>1.05 else 'PASS')
 if ratio>1.05:failed.append(c['name'])
assert not failed,failed
