#!/usr/bin/env python3
"""cargo-mutants Cargo shim: add the requested independent Deflate suite."""
import os, subprocess, sys
args=sys.argv[1:]
result=subprocess.run([os.environ['LANE_REAL_CARGO'],*args])
if result.returncode:
    raise SystemExit(result.returncode)
if args and args[0]=='test' and '--no-run' not in args:
    # Ensure the CLI used by the Python suite comes from this mutated tree.
    subprocess.run([os.environ['LANE_REAL_CARGO'],'build','--locked','--bin','verified-tiff'],check=True)
    subprocess.run(['python3','tests/deflate.py'],check=True)
