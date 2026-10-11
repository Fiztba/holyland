
// Test-only canonical snapshot: unused queue slots and expired readiness have
// no architectural meaning. Keep equal-cycle flag order from the real slots.
void VU1Interpreter::verifyScheduler(uint64_t *out) const
{
    std::fill_n(out, 512, 0ull);
    uint32_t pos=0;
    for (auto ready : m_ready) out[pos++]=std::max(ready,m_cycle);
    out[pos++]=std::max(m_writebackHorizon,m_cycle);
    out[pos++]=std::max(m_efuResourceReady,m_cycle);
    out[pos++]=m_workingClip;
    out[pos++]=m_viBranchBackupValid;
    if(m_viBranchBackupValid) {
        out[pos++]=m_viBranchBackupReg;
        out[pos++]=static_cast<uint32_t>(m_viBranchBackupValue);
    } else pos+=2;
    out[pos++]=m_jobPendingPairValid;
    if(m_jobPendingPairValid) {
        out[pos++]=m_jobPendingPair.upper;
        out[pos++]=m_jobPendingPair.lower;
    } else pos+=2;
    out[pos++]=m_jobActive;
    out[pos++]=m_jobDraining;
    out[pos++]=static_cast<uint32_t>(m_jobStatus);
    std::array<uint32_t,kMaxFlagEntries> order{};
    uint32_t count=0;
    for(uint32_t i=0;i<kMaxFlagEntries;++i) if(m_flagMask&(1u<<i)) order[count++]=i;
    std::stable_sort(order.begin(),order.begin()+count,[&](auto a,auto b){return m_flagPipeline[a].readyCycle<m_flagPipeline[b].readyCycle;});
    out[pos++]=count;
    for(uint32_t n=0;n<count;++n) {
        const auto&e=m_flagPipeline[order[n]];
        out[pos++]=e.readyCycle;out[pos++]=e.writesMac?e.mac:0;
        out[pos++]=e.statusAndMask;out[pos++]=e.statusOrBits;
        out[pos++]=e.writesClip?e.clip:0;
        out[pos++]=uint32_t(e.writesMac)|(uint32_t(e.writesClip)<<1);
    }
    auto scalar=[&](const auto&e){out[pos++]=e.valid;if(e.valid){out[pos++]=e.readyCycle;uint32_t bits;std::memcpy(&bits,&e.value,4);out[pos++]=bits;out[pos++]=e.statusDi;}};
    scalar(m_fdiv);
    std::array<uint32_t,2> scalarOrder{};
    uint32_t scalarCount=0;
    for(uint32_t i=0;i<2;++i)if(m_efuMask&(1u<<i))scalarOrder[scalarCount++]=i;
    std::stable_sort(scalarOrder.begin(),scalarOrder.begin()+scalarCount,[&](auto a,auto b){return m_efu[a].readyCycle<m_efu[b].readyCycle;});
    out[pos++]=scalarCount;
    for(uint32_t i=0;i<scalarCount;++i)scalar(m_efu[scalarOrder[i]]);
    for(uint32_t i=0;i<kMaxPendingStores;++i) if(m_storeMask&(1u<<i)){
        const auto&e=m_storePipeline[i];out[pos++]=e.readyCycle;out[pos++]=e.address;out[pos++]=e.laneMask;
        for(uint32_t lane=0;lane<4;++lane)out[pos++]=(e.laneMask&(1u<<lane))?e.words[lane]:0;
    }
}
