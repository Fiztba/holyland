// SPDX-License-Identifier: GPL-3.0-or-later
#if defined(_WIN32)
#include <windows.h>
#else
#include <sys/mman.h>
#include <unistd.h>
#endif

static uint32_t extended16(uint16_t raw, bool usn)
{
    return usn || raw < 32768u ? uint32_t(raw) : uint32_t(int32_t(raw) - 65536);
}
static std::vector<uint8_t> packet16(unsigned count, unsigned sourceCount, unsigned imm,
                                     bool masked, unsigned seed)
{
    std::vector<uint8_t> input;
    word(input, command(masked ? 0xFD : 0xED, count & 255u, imm));
    for (unsigned lane=0; lane!=sourceCount*4u; ++lane)
    {
        const uint16_t raw=uint16_t(seed+lane*33461u);
        input.push_back(uint8_t(raw)); input.push_back(uint8_t(raw>>8));
    }
    return input;
}
static void case16(PS2Memory &memory, unsigned &expectedFast, unsigned count,
                   unsigned cycle, unsigned start, unsigned flags, unsigned mode,
                   bool masked, uint32_t mask, unsigned seed)
{
    reset(memory);
    memory.vif1_regs.cycle=cycle; memory.vif1_regs.mode=mode; memory.vif1_regs.mask=mask;
    const unsigned cl=std::max(1u,cycle&255u),wl=std::max(1u,(cycle>>8u)&255u);
    const unsigned sourceCount=cl<wl ? count/wl*cl+std::min(count%wl,cl) : count;
    const auto input=packet16(count,sourceCount,start|(flags<<14u),masked,seed);
    std::array<uint8_t,PS2_VU1_DATA_SIZE> expected;
    std::memcpy(expected.data(),memory.getVU1Data(),expected.size());
    auto registers=memory.vif1_regs;
    std::memcpy(&registers.code,input.data(),4);
    registers.num=count&255u; registers.stat|=1u<<11u;
    const unsigned first=(start+((flags&2u)?registers.tops:0u))%1024u;
    unsigned source=0;
    for(unsigned n=0;n!=count;++n)
    {
        const unsigned slot=n%wl;
        const bool consumes=cl>=wl || slot<cl;
        const unsigned dest=(first+(cl>=wl ? n/wl*cl+slot : n))%1024u;
        for(unsigned lane=0;lane!=4;++lane)
        {
            unsigned selector=masked ? (mask>>(std::min(slot,3u)*8u+lane*2u))&3u : 0u;
            if(!consumes && selector==0) selector=1;
            if(selector==3) continue;
            uint32_t value;
            if(selector==1) value=registers.row[lane];
            else if(selector==2) value=registers.col[std::min(slot,3u)];
            else
            {
                uint16_t raw;std::memcpy(&raw,input.data()+4u+source*8u+lane*2u,2);
                value=extended16(raw,(flags&1u)!=0);
                if((mode&3u)==1u || (mode&3u)==2u) value+=registers.row[lane];
                if((mode&3u)==2u || (mode&3u)==3u) registers.row[lane]=value;
            }
            std::memcpy(expected.data()+dest*16u+lane*4u,&value,4);
        }
        if(consumes)++source;
    }
    const auto before=vifBulkUnpackCalls;
    memory.processVIF1Data(input.data(),uint32_t(input.size()));
    require(std::memcmp(memory.getVU1Data(),expected.data(),expected.size())==0,"V4-16 destination/full memory exact");
    require(std::memcmp(&memory.vif1_regs,&registers,sizeof(registers))==0,"V4-16 registers and ROW exact");
    const unsigned admitted=EXPECT_BULK_UNPACK && !masked && mode==0 && cl>=wl;
    require(vifBulkUnpackCalls-before==admitted,"V4-16 guard admission exact");
    expectedFast+=admitted;
}

static void testV4_16(PS2Memory &memory,unsigned &expectedFast)
{
    for(const unsigned count:{1u,2u,3u,7u,31u,256u})
    for(const unsigned cycle:{0u,0x0101u,0x0103u,0x0204u,0x01FFu,0xFFFFu,0x0203u,0x0308u,0x0301u,0x0703u,0xFF00u})
    for(const unsigned start:{0u,1u,511u,1000u,1020u,1023u})
    for(unsigned flags=0;flags!=4;++flags)
    for(const unsigned mode:{0u,1u,2u,3u,4u,0xFFFFFFFFu})
    for(unsigned masking=0;masking!=3;++masking)
        case16(memory,expectedFast,count,cycle,start,flags,mode,masking!=0,
               masking==2?0xE4E4E4E4u:0u,0x8000u);
    // A permutation covering every 16-bit value, in both widening modes.
    for(unsigned usn=0;usn!=2;++usn)
    for(unsigned block=0;block!=64;++block)
        case16(memory,expectedFast,256,0x0101,1023,usn,0,false,0,block*1024u*33461u);

    // Consecutive compressed/uncompressed commands and a following MARK prove
    // exact source advancement, including a wrapped odd-length span.
    reset(memory);
    auto stream=packet16(3,3,1023,false,0x8000);
    const auto second=packet16(2,2,37u|0x4000u,false,0xFFFF);
    stream.insert(stream.end(),second.begin(),second.end());
    word(stream,command(0x6C,1,43));
    for(unsigned lane=0;lane!=4;++lane)word(stream,0xDEAD0000u+lane);
    word(stream,command(0x07,0,0xABCD));
    memory.processVIF1Data(stream.data(),uint32_t(stream.size()));
    require(memory.vif1_regs.mark==0xABCD && memory.vif1_regs.code==command(0x07,0,0xABCD),"V4-16 following command exact");
    for(unsigned n=0;n!=3;++n)for(unsigned lane=0;lane!=4;++lane)
    {
        uint32_t value;std::memcpy(&value,memory.getVU1Data()+((1023u+n)%1024u)*16u+lane*4u,4);
        require(value==extended16(uint16_t(0x8000u+(n*4u+lane)*33461u),false),"V4-16 first chained command exact");
    }
    for(unsigned n=0;n!=2;++n)for(unsigned lane=0;lane!=4;++lane)
    {
        uint32_t value;std::memcpy(&value,memory.getVU1Data()+(37u+n)*16u+lane*4u,4);
        require(value==extended16(uint16_t(0xFFFFu+(n*4u+lane)*33461u),true),"V4-16 second chained command exact");
    }
    for(unsigned lane=0;lane!=4;++lane)
    {
        uint32_t value;std::memcpy(&value,memory.getVU1Data()+43u*16u+lane*4u,4);
        require(value==0xDEAD0000u+lane,"V4-32 chained command exact");
    }
    expectedFast+=EXPECT_BULK_UNPACK?3u:0u;

    // Direct source alias: original sequential reads may observe earlier writes.
    for(unsigned flags=0;flags!=2;++flags)
    {
        reset(memory);
        const auto packet=packet16(7,7,11u|(flags<<14u),false,0x8000u);
        std::memcpy(memory.getVU1Data()+160u,packet.data(),packet.size());
        std::array<uint8_t,PS2_VU1_DATA_SIZE> expected;
        std::memcpy(expected.data(),memory.getVU1Data(),expected.size());
        for(unsigned n=0;n!=7;++n)
        {
            uint32_t value[4];
            for(unsigned lane=0;lane!=4;++lane)
            {
                uint16_t raw;std::memcpy(&raw,expected.data()+164u+n*8u+lane*2u,2);
                value[lane]=extended16(raw,flags!=0);
            }
            std::memcpy(expected.data()+(11u+n)*16u,value,16);
        }
        const auto before=vifBulkUnpackCalls;
        memory.processVIF1Data(memory.getVU1Data()+160u,uint32_t(packet.size()));
        require(std::memcmp(memory.getVU1Data(),expected.data(),expected.size())==0,"V4-16 alias exact");
        require(vifBulkUnpackCalls==before,"V4-16 alias guard");
    }

    // A complete packet ends immediately at a protected page: wide tail reads
    // would fault, while the canonical parser and exact 8-byte tail are legal.
#if defined(_WIN32)
    SYSTEM_INFO info;GetSystemInfo(&info);const size_t page=info.dwPageSize;
    auto allocation=static_cast<uint8_t*>(VirtualAlloc(nullptr,page*2,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));
    require(allocation!=nullptr,"guard allocation");DWORD oldProtection;
    require(VirtualProtect(allocation+page,page,PAGE_NOACCESS,&oldProtection)!=0,"guard protection");
#else
    const size_t page=size_t(sysconf(_SC_PAGESIZE));
    auto allocation=static_cast<uint8_t*>(mmap(nullptr,page*2,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0));
    require(allocation!=MAP_FAILED,"guard allocation");
    require(mprotect(allocation+page,page,PROT_NONE)==0,"guard protection");
#endif
    for(unsigned flags=0;flags!=2;++flags)
    for(const unsigned count:{1u,2u,3u,7u})
    for(const unsigned start:{0u,1023u})
    {
        reset(memory);
        const auto packet=packet16(count,count,start|(flags<<14u),false,0x8000u);
        auto input=allocation+page-packet.size();std::memcpy(input,packet.data(),packet.size());
        memory.processVIF1Data(input,uint32_t(packet.size()));
        for(unsigned lane=0;lane!=count*4u;++lane)
        {
            uint16_t raw;std::memcpy(&raw,input+4u+lane*2u,2);
            uint32_t actual;std::memcpy(&actual,memory.getVU1Data()+((start*4u+lane)%4096u)*4u,4);
            require(actual==extended16(raw,flags!=0),"V4-16 protected tail exact");
        }
        expectedFast+=EXPECT_BULK_UNPACK?1u:0u;
    }
#if defined(_WIN32)
    VirtualFree(allocation,0,MEM_RELEASE);
#else
    munmap(allocation,page*2);
#endif
    // Every byte split leaves VU memory untouched until the complete payload.
    memory.setVu1CooperativeCallbacks([]{return false;},[]{});
    for(unsigned flags=0;flags!=2;++flags)
    {
        const auto packet=packet16(3,3,1023u|(flags<<14u),false,0x7FFFu);
        for(size_t split=0;split<=packet.size();++split)
        {
            reset(memory);
            memory.processVIF1Data(packet.data(),uint32_t(split));memory.serviceVif1Stream();
            if(split!=packet.size())
                for(unsigned n=0;n!=PS2_VU1_DATA_SIZE;++n)
                    require(memory.getVU1Data()[n]==0xA5,"V4-16 partial command cannot mutate");
            memory.processVIF1Data(packet.data()+split,uint32_t(packet.size()-split));memory.serviceVif1Stream();
            require(!memory.vif1WorkPending(),"V4-16 split consumed");
            for(unsigned lane=0;lane!=12;++lane)
            {
                uint16_t raw;std::memcpy(&raw,packet.data()+4u+lane*2u,2);
                uint32_t actual;std::memcpy(&actual,memory.getVU1Data()+((1023u*4u+lane)%4096u)*4u,4);
                require(actual==extended16(raw,flags!=0),"V4-16 split/wrap exact");
            }
            expectedFast+=EXPECT_BULK_UNPACK?1u:0u;
        }
    }
    memory.setVu1CooperativeCallbacks({},{});
}
