#!/usr/bin/env python3
"""Find compiled library functions inside the retail .text.

    tools/find_lib_matches.py baserom/us/SLUS_203.18 <objdir> [out.tsv]

<objdir> holds unlinked .o files, e.g. ICO's sce/ tree compiled with
tools/compile_c.sh. For every function symbol in every object, the words that
carry relocations are masked (jal targets, %hi/%lo, gp-relative offsets) and
the rest are searched for in the target's .text. Prints per-library hit
counts; the TSV lists library, function and address (or '-').

Short functions can match lookalikes: check anything that lands in game code.
"""
import sys, os, re, struct
from elftools.elf.elffile import ELFFile
from elftools.elf.relocation import RelocationSection
from collections import defaultdict

target, objdir = sys.argv[1], sys.argv[2]
with open(target,'rb') as f:
    te = ELFFile(f); ts = te.get_section_by_name('.text')
    tbase = ts['sh_addr']; tdata = ts.data()
twords = struct.unpack('<%dI' % (len(tdata)//4), tdata)
# index by first unmasked-opcode word for speed: map word->positions
idx = defaultdict(list)
for i,w in enumerate(twords): idx[w].append(i)

def mask_for(rtype):
    return {4:0xFC000000, 5:0xFFFF0000, 6:0xFFFF0000, 7:0xFFFF0000}.get(rtype, 0)  # 26, HI16, LO16, GPREL16

res = defaultdict(dict)  # lib -> {func: addr or None}
for root,_,files in os.walk(objdir):
    for fn in files:
        if not fn.endswith('.o'): continue
        p = os.path.join(root,fn); lib = os.path.relpath(root,objdir)
        with open(p,'rb') as f:
            e = ELFFile(f)
            secs = {i:s for i,s in enumerate(e.iter_sections())}
            symtab = e.get_section_by_name('.symtab')
            for si,sec in secs.items():
                if not sec.name.startswith('.text') or sec['sh_size']==0: continue
                data = sec.data(); n=len(data)//4
                words = list(struct.unpack('<%dI'%n, data)); masks=[0xFFFFFFFF]*n
                for rs in e.iter_sections():
                    if isinstance(rs,RelocationSection) and rs['sh_info']==si:
                        for r in rs.iter_relocations():
                            k=r['r_offset']//4
                            if k<n: masks[k]=mask_for(r['r_info_type']) if mask_for(r['r_info_type']) else 0
                funcs = sorted([(s['st_value'],s.name, s['st_size']) for s in symtab.iter_symbols()
                                if s['st_shndx']==si and s['st_info']['type'] in ('STT_FUNC','STT_NOTYPE') and s.name and not s.name.startswith(('.','$'))])
                for j,(off,name,size) in enumerate(funcs):
                    end = funcs[j+1][0] if j+1<len(funcs) else len(data)
                    if size: end = off+size
                    a,b = off//4, end//4
                    if b-a < 3: continue
                    # find an anchor word fully unmasked
                    anc = next((k for k in range(a,b) if masks[k]==0xFFFFFFFF and words[k]!=0), None)
                    hit=None
                    if anc is not None:
                        for pos in idx.get(words[anc],[]):
                            st = pos-(anc-a)
                            if st<0 or st+(b-a)>len(twords): continue
                            if all((twords[st+k-a]&masks[k])==(words[k]&masks[k]) for k in range(a,b)):
                                hit = tbase+st*4; break
                    res[lib][name]=hit
tot=found=0
for lib in sorted(res):
    d=res[lib]; h=sum(1 for v in d.values() if v); tot+=len(d); found+=h
    print(f"{lib:20s} {h:4d}/{len(d):4d}")
print("TOTAL", found, tot)
if len(sys.argv)>3:
    with open(sys.argv[3],'w') as f:
        for lib in sorted(res):
            for n,v in sorted(res[lib].items(), key=lambda x:(x[1] or 0)):
                f.write(f"{lib}\t{n}\t{hex(v) if v else '-'}\n")
