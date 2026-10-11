# SPDX-License-Identifier: GPL-3.0-or-later
"""Source-authored loop admission, including transient VF0 refusal."""
import struct
import authored_program as authored
import vu_bounded_loops as loops
import vu_bounded_expressions as expressions

pairs,entries=authored.program()
image=b''.join(struct.pack('<II',*pair) for pair in pairs)
plans,rejected=loops.discover(expressions,image)
assert {p['head'] for p in plans}=={entries['repeated'],entries['pipeline'],entries['alias']}
assert any(p['head']==entries['vf0'] and p['reason']=='transient upper VF0 write' for p in rejected)
assert next(p for p in plans if p['head']==entries['repeated'])['can_repeat']
assert not next(p for p in plans if p['head']==entries['pipeline'])['can_repeat']

for word,expected in [
    (authored.upper(0x28,fd=0),True),(authored.upper(0x2b,fd=0),True),
    (authored.special(0x10,15,0,1),True),(authored.special(0x14,15,0,1),True),
    (authored.special(0x1d,15,0,1),True),(authored.upper(0x28,mask=0,fd=0),False),
    (authored.special(0x28,15,0,1),False),(authored.special(0x1f,15,0,1),False),
    (authored.UPPER_NOP,False)]:
    pair=expressions.PairInfo(struct.pack('<II',authored.LOWER_NOP,word),0)
    assert loops.writes_vf0(pair)==expected,hex(word)

# Prove the unsafe source is otherwise an admitted loop, so removing this
# gate cannot silently turn the regression into an unrelated refusal.
guard=loops.writes_vf0
try:
    loops.writes_vf0=lambda pair:False
    unchecked,_=loops.discover(expressions,image)
    assert entries['vf0'] in {p['head'] for p in unchecked}
finally:
    loops.writes_vf0=guard
print('PASS authored recurrence, nonstationary fallback, VF0 admission and gate control')
