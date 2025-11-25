/**
 * Copyright (c) 2018 Inria
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met: redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer;
 * redistributions in binary form must reproduce the above copyright
 * notice, this list of conditions and the following disclaimer in the
 * documentation and/or other materials provided with the distribution;
 * neither the name of the copyright holders nor the names of its
 * contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "mem/cache/replacement_policies/three_q_rp.hh"

#include <cassert>
#include <memory>

#include "params/ThreeQRP.hh"
#include "sim/core.hh"
#include "base/intmath.hh"

// namespace gem5
// {

// GEM5_DEPRECATED_NAMESPACE(ReplacementPolicy, replacement_policy);
// namespace replacement_policy
// {

ThreeQRP::ThreeQRP(const Params *p)
  : BaseReplacementPolicy(p), 
    lineBits(floorLog2(p->system->cacheLineSize())),
    indexBits(p->index_bit),
    assoc(p->assoc),
    smallRatio(p->small_queue_percent),
    smallQueueCap(std::min((unsigned)std::floor(p->small_queue_percent * p->assoc), (unsigned)p->assoc)),
    mainQueueCap(p->assoc - smallQueueCap),
    ghostQueueCap(mainQueueCap)
{
    std::cout << "lineBits=" << lineBits << "  indexBits=" << indexBits << std::endl;
    std::cout << "Cache Assoc=" << assoc << std::endl;
    std::cout << "sq ratio=" << smallRatio << "  sq capacity=" << smallQueueCap << std::endl;
    std::cout << "mq cap=" << mainQueueCap << "  gq capacity=" << ghostQueueCap << std::endl;
}

/**
 * set that one to invalid
 */
void
ThreeQRP::invalidate(const std::shared_ptr<ReplacementData>& replacement_data) const
{
    // TODO
    auto d = std::static_pointer_cast<ThreeQReplData>(replacement_data);
    d->pos = ThreeQReplData::Position::None;
    d->freq = 0;
    d->tickInserted = 0;
    DPRINTF(ThreeQ, "invalidate: called for set=%u, tag=%#llx\n",
            d->setIdx, (unsigned long long)d->tag);
}

/**
 * pkt: pkt generates this hit
 * hit in S or M
 * increase the frequency counter
 */
void
ThreeQRP::touch(const std::shared_ptr<ReplacementData>& replacement_data,
              const PacketPtr pkt) 
{
    auto d = std::static_pointer_cast<ThreeQReplData>(replacement_data);
    if(d->pos == ThreeQReplData::Position::None) return;
    if(d->freq < 3) d->freq++;
    DPRINTF(ThreeQ, "touch: called for set=%u tag=%#llx freq=%u\n",
        d->setIdx, (unsigned long long)d->tag, static_cast<unsigned>(d->freq));
}

void
ThreeQRP::touch(const std::shared_ptr<ReplacementData>& replacement_data)
    const
{
    panic("Shouldn't reach here since we need more info for 3q.");
}

/**
 * pkt: packet that generates this miss
 * if x in G -> insert at head of M -> remove from G
 * if x not in G -> insert at head of S 
 * set freq(x) = 0
 */
void
ThreeQRP::reset(const std::shared_ptr<ReplacementData>& replacement_data,
    const PacketPtr pkt)
{
    auto d = std::static_pointer_cast<ThreeQReplData>(replacement_data);
    d->freq = 0;
    d->tickInserted = nextStamp();

    const Addr a = pkt->getAddr();
    const auto set = calcSetIdx(a);
    const auto tag = calcTag(a);

    d->setIdx = set;
    d->tag = tag;

    DPRINTF(ThreeQ, "reset: called for set=%u tag=%#llx\n",
        set, (unsigned long long)tag);

    if(ghostHas(set, tag)) {
        d->pos = ThreeQReplData::Position::M;
        ghostDel(set,tag);
        DPRINTF(ThreeQ, "ghost has this tag. put into M\n");
    }
    else {
        d->pos = ThreeQReplData::Position::S;
        DPRINTF(ThreeQ, "ghost miss -> put into S\n");
    }
}

void
ThreeQRP::reset(const std::shared_ptr<ReplacementData>& replacement_data)
    const
{
    panic("Shouldn't reach here since we need more info for 3q.");
}


/**
 * if invalid, choose this one
 * whether to evict from S or M
 * change the S and M
 * if S is evicted from S to G -> victim. if S is evicted from M to nowhere -> victim.
 */
ReplaceableEntry*
ThreeQRP::getVictim(const ReplacementCandidates& candidates) const
{    
    // if there is invalid entry. make it the victim
    for(auto* e: candidates) {
        auto* d = static_cast<ThreeQReplData*>(e->replacementData.get());
        if(d->pos == ThreeQReplData::Position::None) {
            DPRINTF(ThreeQ, "getVictim: found invalid entry -> choose as victim\n");
            return e;
        }
    }
    // collect how many entries in the S and M
    unsigned sCnt =0, mCnt = 0;
    for(auto* e: candidates) {
        auto* d = static_cast<ThreeQReplData*>(e->replacementData.get());
        if(d->pos == ThreeQReplData::Position::S) ++sCnt;
        else if(d->pos == ThreeQReplData::Position::M) ++mCnt;
    }
   DPRINTF(ThreeQ, "getVictim: sCnt=%u mCnt=%u\n", sCnt, mCnt);

    // helper method to find tail of a queue
    auto pickTail = [](const ReplacementCandidates& cs, ThreeQReplData::Position queue) 
        -> std::pair<ReplaceableEntry*, ThreeQReplData*>
    {
        ReplaceableEntry* res = nullptr;
        ThreeQReplData* rd = nullptr;
        Tick best = std::numeric_limits<Tick>::max();
        for(auto* e: cs ) {
            auto* d = static_cast<ThreeQReplData*>(e->replacementData.get());
            if(d->pos == queue && d->tickInserted < best) {
                best = d->tickInserted;
                res = e;
                rd = d;
            }
        }
        return {res, rd};
    };

    // three cases causing evict: 1. from S->G . 2. M is full and M-> none . 3. S ->M(cause M to be full)->None
    while(true) {
        if(sCnt >= smallQueueCap) {
            auto [sTailEntry, sTailData] = pickTail(candidates, ThreeQReplData::Position::S);
            if(sTailData->freq == 0) {
                // evict to G and return victim
                ghostAdd(sTailData->setIdx,sTailData->tag);
                DPRINTF(ThreeQ, "Victim from S->G. Set:%u. tag: %#llx\n", sTailData->setIdx, (unsigned long long)sTailData->tag);
                return sTailEntry;
            } else {
                sTailData->freq--;
                sTailData->pos = ThreeQReplData::Position::M;
                sTailData->tickInserted = nextStamp();
                ++mCnt;
                --sCnt;
                DPRINTF(ThreeQ, "Get Victim: evict %#llx from S to M. current mCnt%u sCnt %u\n", (unsigned long long)sTailData->tag, mCnt, sCnt);

                if(mCnt >= mainQueueCap) {
                    while(true) {
                        auto [mTailEntry, mTailData] = pickTail(candidates, ThreeQReplData::Position::M);
                        if(mTailData->freq > 0) {
                            mTailData->freq--;
                            mTailData->tickInserted = nextStamp();
                            DPRINTF(ThreeQ, "m tail re-insersion for tag %#llx. Tick=%llu\n", (unsigned long long)mTailData->tag, (unsigned long long)mTailData->tickInserted);
                            continue;
                        }
                        DPRINTF(ThreeQ, "Victim from M since S make M full. Set:%u. tag: %#llx\n", mTailData->setIdx, (unsigned long long)mTailData->tag);
                        return mTailEntry;
                    }
                }
                continue;
            }
        } else {
            /*evict M*/
            while(true) {
                auto [mTailEntry, mTailData] = pickTail(candidates, ThreeQReplData::Position::M);
                if(mTailData->freq > 0) {
                    mTailData->freq--;
                    mTailData->tickInserted = nextStamp();
                    continue;
                }
                DPRINTF(ThreeQ, "Victim from M. Set:%u. tag: %#llx\n", mTailData->setIdx, (unsigned long long)mTailData->tag);
                return mTailEntry;
            }
        }
    }
}


std::shared_ptr<ReplacementData>
ThreeQRP::instantiateEntry()
{
    return std::shared_ptr<ReplacementData>(new ThreeQReplData());
}


bool 
ThreeQRP::ghostHas(uint32_t s, Addr t) const {
    auto it = ghostQ.find(s);
    if(it == ghostQ.end()) return false;
    for(const auto &x : it->second) {
        if (x == t) return true;
    }
    return false;
}

void
ThreeQRP::ghostDel(uint32_t s, Addr t) const {
    auto it = ghostQ.find(s);
    if(it == ghostQ.end()) return;
    auto &lst = it->second;
    for(auto itr = lst.begin();itr != lst.end(); ++itr) {
        if(*itr == t) {
            lst.erase(itr); 
            break;
        }
    }
    if(lst.empty()) ghostQ.erase(it);
}

void
ThreeQRP::ghostAdd(uint32_t s, Addr t) const {
    auto &lst = ghostQ[s];
    //if exist then move to head
    for(auto itr = lst.begin(); itr != lst.end(); ++itr) {
        if(*itr == t) {
            lst.erase(itr);
            break;
        }
    }
    lst.push_front(t);
    while(lst.size() > ghostQueueCap) lst.pop_back();
}

ThreeQRP*
ThreeQRPParams::create()
{
    return new ThreeQRP(this);
}

// } // namespace replacement_policy
// } // namespace gem5