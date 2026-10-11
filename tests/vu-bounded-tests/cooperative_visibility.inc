// SPDX-License-Identifier: GPL-3.0-or-later
uint64_t vuPrivateBoundedBatchCount();
namespace {
void cooperativeVisibility()
{
    std::array<uint8_t,16384>image;
    std::memcpy(image.data(),authoredVU::pairs,image.size());
    Pair p;
    auto canonical=std::make_unique<VU1Interpreter>();
    uint64_t mutations=0,resumes=0,kernels=0;
    const uint64_t initialBatches=vuPrivateBoundedBatchCount();
    for(uint32_t regionIndex=0;regionIndex<vu_bounded::regionCount;++regionIndex)
    for(uint32_t seed=0;seed<4;++seed)
    for(uint32_t quantum:{1u,7u,uint32_t(vu_bounded::regions[regionIndex].cycles),uint32_t(vu_bounded::regions[regionIndex].cycles+3u), uint32_t(vu_bounded::regions[regionIndex].cycles+2u*vu_bounded::regions[regionIndex].repeatInterval+3u)}) {
        const auto&region=vu_bounded::regions[regionIndex];
        p.reset();p.seed(993u+seed);canonical->reset();canonical->verifyDisableNative=true;
        std::memcpy(p.cm->getVU1Code(),image.data(),image.size());
        std::memcpy(p.rm->getVU1Code(),image.data(),image.size());
        uint32_t first[2];std::memcpy(first,image.data(),8);p.instruction(0,first[0],first[1]);
        for(uint32_t i=1;i<16;++i)p.c->state().vi[i]=seed==0?1:seed==1?3:-1;
        canonical->state()=p.c->state();
        std::mt19937 random(731u+seed);
        std::array<uint32_t,4096>words;
        for(auto&word:words)word=random();p.data(0,words.data(),sizeof(words));
        const uint64_t before=vuPrivateBoundedRegionCount(regionIndex);
        const uint32_t budget=region.cycles*5u+11u;
        p.check(p.c->beginExecute(p.cm->getVU1Code(),16384,p.cm->getVU1Data(),16384,p.gs,p.cm.get(),region.head,0,0,budget),"cooperative-native-begin");
        p.check(canonical->beginExecute(p.rm->getVU1Code(),16384,p.rm->getVU1Data(),16384,p.gs,p.rm.get(),region.head,0,0,budget),"cooperative-canonical-begin");
        uint32_t step=0;
        while(p.c->jobActive()||canonical->jobActive()) {
            ++calls;
            auto a=p.c->stepJob(quantum),b=canonical->stepJob(quantum);
            p.check(a.cyclesAdvanced==b.cyclesAdvanced&&a.cyclesAdvanced<=quantum&&a.active==b.active&&a.reason==b.reason,"cooperative-quantum");
            std::memcpy(&p.r->state(),&canonical->state(),sizeof(VU1State));
            p.compare();
            std::array<uint64_t,512>sa{},sb{};
            p.c->verifyScheduler(sa.data());canonical->verifyScheduler(sb.data());
            if(sa!=sb){for(uint32_t i=0;i<sa.size();++i)if(sa[i]!=sb[i]){std::cerr<<"scheduler slot="<<i<<" native="<<sa[i]<<" canonical="<<sb[i]<<'\n';break;}}
            p.check(sa==sb,"cooperative-pending-state");
            ++resumes;
            if(!a.active)break;
            // Simulate VIF memory publication between host resumptions. Mutate
            // a next-load address, a wrapped address and an unrelated location.
            for(uint32_t address:{(static_cast<uint32_t>(p.c->state().vi[2])*16u)&0x3ff0u,0x3ff0u,0x700u}) {
                uint32_t replacement[4];for(auto&word:replacement)word=random();
                p.data(address,replacement,16);++mutations;
            }
            p.check(++step<2000,"cooperative-progress");
        }
        const uint64_t used=vuPrivateBoundedRegionCount(regionIndex)-before;
        p.check(quantum>=region.cycles?used>0:used==0,"cooperative-admission");
        kernels+=used;
    }
    std::cout<<"PASS cooperative visibility resumes="<<resumes<<" mutations="<<mutations<<" kernels="<<kernels<<" batches="<<vuPrivateBoundedBatchCount()-initialBatches<<" checks="<<checks<<" calls="<<calls<<'\n';
}
}
