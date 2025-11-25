#ifndef __MEM_CACHE_REPLACEMENT_POLICIES_THREE_Q_RP_HH__
#define __MEM_CACHE_REPLACEMENT_POLICIES_THREE_Q_RP_HH__

#include "base/types.hh"
#include "mem/cache/replacement_policies/base.hh"
#include "mem/cache/tags/base_set_assoc.hh"
#include "debug/ThreeQ.hh"
#include "base/trace.hh"

// namespace gem5
// {

struct ThreeQRPParams;

// GEM5_DEPRECATED_NAMESPACE(ReplacementPolicy, replacement_policy);
// namespace replacement_policy
// {

class ThreeQRP : public BaseReplacementPolicy
{
  protected:
    /**specific implementation of replacement data. */
    struct ThreeQReplData : ReplacementData
    { 
        /*enum class of which queue this data belongs to*/
        enum class Position: uint8_t {None = 0, S = 1, M = 2};

        Position pos = Position::None;

        /** Tick on which the entry was inserted. */
        Tick tickInserted;
        
        /*saturating counter 0-3 */
        uint8_t freq = 0;

        /*tag and set number of this way*/
        uint32_t setIdx = 0;
        Addr tag = 0;

        /**
         * Default constructor. Invalidate data.
         */
        ThreeQReplData() : tickInserted(0) {}
    };
  
  private:
    /*how many bits for one cacheline(offset)*/
    const unsigned lineBits;
    /*how many index bits*/
    const unsigned indexBits;
    /*assoc of single set*/
    const unsigned assoc;
    /*ratio of size of small queue*/
    const double smallRatio;
    /*small queue size*/
    const unsigned smallQueueCap;
    /*main queue size*/
    const unsigned mainQueueCap;
    /*ghost queue size per set*/
    const unsigned ghostQueueCap;
    /*ghost queue. This one is global*/
    mutable std::unordered_map<uint32_t,std::list<Addr>> ghostQ;
    /*Internal time stamp*/
    mutable Tick stamp = 0;

    /*Help methods*/
    inline uint32_t calcSetIdx(Addr a) const {
        return (a >> lineBits) & ((1u << indexBits) - 1u);
    }

    inline Addr calcTag(Addr a) const {
        return (a >> (lineBits + indexBits));
    }

    bool ghostHas(uint32_t s, Addr t) const;

    void ghostDel(uint32_t s, Addr t) const;

    void ghostAdd(uint32_t s, Addr t) const;

    inline Tick nextStamp() const {return ++stamp;}

  public:
    typedef ThreeQRPParams Params;
    ThreeQRP(const Params *p);
    ~ThreeQRP() = default;

    /**
     * Invalidate replacement data to set it as the next probable victim.
     *
     * @param replacement_data Replacement data to be invalidated.
     */
    void invalidate(const std::shared_ptr<ReplacementData>& replacement_data)
                                                                    const override;

    /**
     * Touch an entry to update its replacement data.
     *
     * @param replacement_data Replacement data to be touched.
     * @param pkt Packet that generated this hit.
     */
    void touch(const std::shared_ptr<ReplacementData>& replacement_data,
        const PacketPtr pkt) override;
    void touch(const std::shared_ptr<ReplacementData>& replacement_data) const
        override;                                                              
    

    /**
     * Reset replacement data. Used when an entry is inserted.
     *
     * @param replacement_data Replacement data to be reset.
     * @param pkt Packet that generated this miss.
     */
    void reset(const std::shared_ptr<ReplacementData>& replacement_data,
        const PacketPtr pkt) override;
    void reset(const std::shared_ptr<ReplacementData>& replacement_data) const
        override;

    /**
     * Find replacement victim using insertion timestamps.
     *
     * @param cands Replacement candidates, selected by indexing policy.
     * @return Replacement entry to be replaced.
     */
    ReplaceableEntry* getVictim(const ReplacementCandidates& candidates) const
                                                                     override;

    /**
     * Instantiate a replacement data entry.
     *
     * @return A shared pointer to the new replacement data.
     */
    std::shared_ptr<ReplacementData> instantiateEntry() override;
};

// } // namespace replacement_policy
// } // namespace gem5

#endif // __MEM_CACHE_REPLACEMENT_POLICIES_FIFO_RP_HH__