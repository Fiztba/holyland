# SPDX-License-Identifier: GPL-3.0-or-later
"""Instrument the actual parser in build scratch and derive strict controls."""
from pathlib import Path
import argparse
import hashlib
import json

parser = argparse.ArgumentParser()
parser.add_argument('--runtime', type=Path, required=True)
parser.add_argument('--output', type=Path, required=True)
args = parser.parse_args()
source = args.runtime / 'src/lib/ps2_vif1_interpreter.cpp'
original = source.read_text()

def replace(text, old, new, count):
    if text.count(old) != count:
        raise SystemExit('Review VIF fixture marker against current parser: ' + old)
    return text.replace(old, new)

# The counter exists only in the fixture. It proves eligible operations enter
# the fast path and the excluded cases retain fallback, even for identical data.
candidate = replace(original, '#include <cstring>',
    '#include <cstring>\nextern unsigned vifBulkUnpackCalls;', 1)
candidate = replace(candidate, '                    uint32_t remaining = writeVectorCount;',
    '                    ++vifBulkUnpackCalls;\n                    uint32_t remaining = writeVectorCount;', 1)
scalar = replace(candidate,
    '#if defined(__SSE2__) || defined(_M_X64) || (defined(_M_IX86_FP) && _M_IX86_FP >= 2)',
    '#if 0', 2)
outputs = {
    'candidate.cpp': candidate,
    'scalar.cpp': scalar,
    'bad_sign.cpp': replace(candidate, '_mm_srai_epi16(packed, 15)', '_mm_setzero_si128()', 2),
    'bad_mask.cpp': replace(candidate, '&& !maskEnable && vif1_regs.mode == 0u',
                            '&& vif1_regs.mode == 0u', 1),
    'bad_skip.cpp': replace(candidate, 'dest = (dest + cl) & 0x3FFu;',
                            'dest = (dest + cl) & 0x3FEu;', 1),
}
args.output.mkdir(parents=True, exist_ok=True)
manifest = {'parser_sha256': hashlib.sha256(source.read_bytes()).hexdigest(), 'controls': {}}
for name, text in outputs.items():
    target = args.output / name
    target.write_text(text, newline='\n')
    manifest['controls'][name] = hashlib.sha256(target.read_bytes()).hexdigest()
(args.output / 'manifest.json').write_text(json.dumps(manifest, indent=2) + '\n', newline='\n')
