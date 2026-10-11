# SPDX-License-Identifier: GPL-3.0-or-later
"""Test only: select the generated non-SSE branch without changing host macros."""
import argparse
from pathlib import Path

parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('--source',type=Path,required=True)
parser.add_argument('--destination',type=Path,required=True)
args=parser.parse_args()
source=args.source.resolve();destination=args.destination.resolve()
if source==destination:
    raise ValueError('Forced fallback must use a separate build-scratch file')
guard='#if (defined(__SSE2__) || defined(_M_X64) || (defined(_M_IX86_FP) && _M_IX86_FP >= 2)) && FLT_EVAL_METHOD == 0 // bounded native target'
text=source.read_text()
if text.count(guard)!=1:
    raise ValueError('Expected one generated architecture conditional')
text=text.replace(guard,'#if 0 // bounded native target: forced only in test scratch')
data=text.encode()
if not destination.exists() or destination.read_bytes()!=data:
    destination.parent.mkdir(parents=True,exist_ok=True)
    destination.write_bytes(data)
