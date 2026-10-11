// SPDX-License-Identifier: GPL-3.0-or-later
// Source-authored loops only; generated instruction words remain build output.
uint64_t vuPrivateGeometryIterationsForReplay();
uint64_t vuPrivateBoundedRegionCount(uint32_t);
uint64_t vuPrivateBoundedBatchCount();
#include "authored_catalog.inc"
#include "catalog.bounded.h"
namespace {
void boundedLoad(Pair &p)
{
    std::memcpy(p.cm->getVU1Code(),authoredVU::pairs,sizeof(authoredVU::pairs));
    std::memcpy(p.rm->getVU1Code(),authoredVU::pairs,sizeof(authoredVU::pairs));
    p.instruction(0,authoredVU::pairs[0][0],authoredVU::pairs[0][1]);
}
void authoredNativeSynthetic()
{
    Pair p;
    uint32_t cases=0;
    const uint64_t initialIterations=vuPrivateGeometryIterationsForReplay(),initialBatches=vuPrivateBoundedBatchCount();
    p.check(vu_bounded::regionCount==3,"bounded-discovered-count");
    for(uint32_t index=0;index<vu_bounded::regionCount;++index) {
        const auto&region=vu_bounded::regions[index];
        p.check(region.head!=authoredVU::vf0,"bounded-vf0-not-admitted");
        if(region.head==authoredVU::pipeline)p.check(!region.canRepeat,"bounded-nonstationary-refusal");
        std::vector<uint32_t>budgets{1,2,3};
        for(uint32_t n=0;n<4;++n) {
            const uint32_t edge=region.cycles+n*region.repeatInterval;
            for(uint32_t delta:{0u,1u,2u})if(edge+delta>1u)budgets.push_back(edge+delta-1u);
        }
        std::sort(budgets.begin(),budgets.end());budgets.erase(std::unique(budgets.begin(),budgets.end()),budgets.end());
        for(uint32_t seed=0;seed<4;++seed)for(int32_t counter:{0,1,2,-1})for(uint32_t budget:budgets) {
            p.reset();p.seed(317u+seed);boundedLoad(p);
            for(uint32_t i=1;i<16;++i)p.c->state().vi[i]=counter;
            p.c->state().status=0xfeed0fffu;
            std::memcpy(&p.r->state(),&p.c->state(),sizeof(VU1State));
            std::mt19937 random(45001u+seed);std::array<uint32_t,4096>words;
            for(auto&word:words)word=random();p.data(0,words.data(),sizeof(words));
            const uint64_t before=vuPrivateBoundedRegionCount(index);
            p.run(false,budget,region.head);
            p.check(budget>=region.cycles?vuPrivateBoundedRegionCount(index)>before:vuPrivateBoundedRegionCount(index)==before,"bounded-budget-admission");
            for(uint32_t i=0;i<5;++i)p.run(true,1);
            const uint32_t address=(static_cast<uint32_t>(p.c->state().vi[2])*16u)&0x3ff0u;
            uint32_t replacement[4];for(auto&v:replacement)v=random();p.data(address,replacement,16);
            p.run(true,region.cycles+4u);++cases;
        }
    }
    // Canonical upper writes VF0 transiently; the paired lower SQ must see
    // the sum. The generic loop refuses this shape, and ordinary dispatch
    // preserves the actual interpreter behavior.
    p.reset();boundedLoad(p);
    for(uint32_t lane=0;lane<4;++lane) {
        p.c->state().vf[1][lane]=float(lane+2u);
        p.c->state().vf[2][lane]=float(lane+3u);
    }
    p.c->state().vi[3]=0x31;p.c->state().vi[4]=0;
    std::memcpy(&p.r->state(),&p.c->state(),sizeof(VU1State));
    const auto before=vuPrivateGeometryIterationsForReplay();
    p.run(false,8,authoredVU::vf0);
    p.check(vuPrivateGeometryIterationsForReplay()==before,"bounded-vf0-fallback");
    float stored[4];std::memcpy(stored,p.cm->getVU1Data()+0x310,16);
    for(uint32_t lane=0;lane<4;++lane)p.check(stored[lane]==float(5u+2u*lane),"bounded-vf0-store-semantics");
    p.check(vuPrivateGeometryIterationsForReplay()-initialIterations>vuPrivateBoundedBatchCount()-initialBatches,"bounded-actual-repeat");
    std::cout<<"PASS bounded cases="<<cases<<" iterations="<<vuPrivateGeometryIterationsForReplay()-initialIterations<<" batches="<<vuPrivateBoundedBatchCount()-initialBatches<<'\n';
}
void boundedPTailControl()
{
    Pair p;p.reset();p.seed(42);boundedLoad(p);
    for(uint32_t reg=0;reg<32;++reg)for(uint32_t lane=0;lane<4;++lane)p.c->state().vf[reg][lane]=float(reg+lane+1u);
    for(uint32_t i=1;i<16;++i)p.c->state().vi[i]=1;
    p.c->state().p=0.25f;std::memcpy(&p.r->state(),&p.c->state(),sizeof(VU1State));
    const vu_bounded::Region*region=nullptr;
    for(uint32_t i=0;i<vu_bounded::regionCount;++i)if(vu_bounded::regions[i].head==authoredVU::repeated)region=&vu_bounded::regions[i];
    p.check(region!=nullptr,"bounded-P-region");
    const auto before=vuPrivateGeometryIterationsForReplay();p.run(false,region->cycles,region->head);
    p.check(vuPrivateGeometryIterationsForReplay()>before,"bounded-P-admission");
    const uint64_t relative=p.c->delayPForTests();p.check(relative==4,"bounded-P-tail-present");
    for(uint32_t i=0;i<relative+2;++i)p.run(true,1);
    throw std::runtime_error("P deadline control was not detected");
}
}

namespace {
void boundedForcedFallback()
{
    Pair p;
    p.check(vu_bounded::regionCount==0,"bounded-forced-empty-regions");
    uint32_t cases=0;
    for(uint32_t head:{authoredVU::repeated,authoredVU::pipeline,authoredVU::alias,authoredVU::vf0})
    for(uint32_t seed=0;seed<2;++seed)
    for(uint32_t budget:{1u,8u,19u,33u,65u}) {
        p.reset();p.seed(710u+seed);boundedLoad(p);
        for(uint32_t i=1;i<16;++i)p.c->state().vi[i]=seed?3:-1;
        std::memcpy(&p.r->state(),&p.c->state(),sizeof(VU1State));
        std::mt19937 random(823u+seed);std::array<uint32_t,4096>words;
        for(auto&word:words)word=random();p.data(0,words.data(),sizeof(words));
        p.run(false,budget,head);
        for(uint32_t i=0;i<5;++i)p.run(true,1);
        uint32_t replacement[4];for(auto&word:replacement)word=random();
        p.data((static_cast<uint32_t>(p.c->state().vi[2])*16u)&0x3ff0u,replacement,16);
        p.run(true,17);++cases;
    }
    p.check(vuPrivateGeometryIterationsForReplay()==0&&vuPrivateBoundedBatchCount()==0,
        "bounded-forced-no-admission");
    std::cout<<"PASS forced target fallback cases="<<cases<<" checks="<<checks<<" calls="<<calls<<'\n';
}
}
