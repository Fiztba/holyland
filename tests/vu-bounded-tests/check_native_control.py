# SPDX-License-Identifier: GPL-3.0-or-later
"""Require the exact delayed-P architectural mismatch."""
import argparse
import re
import subprocess
import sys
parser=argparse.ArgumentParser();parser.add_argument('--executable',required=True)
args=parser.parse_args()
result=subprocess.run([args.executable,'--bounded-p-control'],capture_output=True,text=True,timeout=120)
sys.stdout.write(result.stdout);sys.stderr.write(result.stderr)
if result.returncode!=1 or not re.search(r'^MISMATCH call=5 category=registers$',result.stderr,re.M):
    raise SystemExit('Expected exit 1 and registers mismatch at call 5')
print('PASS pending-P deadline corruption detected at the exact visibility boundary')
