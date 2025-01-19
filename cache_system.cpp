#include <iostream>
#include <vector>
#include <cmath>
#include <string>
#include <list>
#include <fstream>
#include <sstream>
static unsigned globalWriteBackAddress = 0;
static bool globalWriteBackDirty = false;
static bool globall1evictornot = false;
class Cache {
public:
    struct CacheBlock {
        unsigned tag;
        bool valid;
        bool dirty;
    };

    unsigned size_power2;
    unsigned blockSize_power2;
    unsigned associativity_power2;
    unsigned numSets;
    unsigned setIndexBits;
    unsigned cycleTime;
    bool writeAllocate;

    std::vector<std::vector<CacheBlock> > cacheSets;
    std::vector<std::list<unsigned> > lruLists;

    unsigned hits;
    unsigned misses;

    Cache* l1Cache = nullptr;

    void calculateNumSetsAndBits() {
        unsigned size = 1 << size_power2;
        unsigned blockSize = 1 << blockSize_power2;
        unsigned associativity = 1 << associativity_power2;
        numSets = size / (blockSize * associativity);
        setIndexBits = log2(numSets);
        cacheSets.resize(numSets, std::vector<CacheBlock>(associativity, {0, false, false}));
        lruLists.resize(numSets);
        for (unsigned i = 0; i < numSets; i++) {
            for (unsigned j = 0; j < associativity; j++) {
                lruLists[i].push_back(j);
            }
        }
    }

    unsigned getSetIndex(unsigned address) {
        unsigned mask = (1 << setIndexBits) - 1;
        return (address >> blockSize_power2) & mask;
    }

    unsigned getTag(unsigned address) {
        return address >> (blockSize_power2 + setIndexBits);
    }

    unsigned getLRUBlockIndex(unsigned setIndex) {
        return lruLists[setIndex].back();
    }

    void updateLRU(unsigned setIndex, unsigned blockIndex) {
        lruLists[setIndex].remove(blockIndex);
        lruLists[setIndex].push_front(blockIndex);
    }

    Cache(unsigned size_power2, unsigned blockSize_power2, unsigned associativity_power2, unsigned cycleTime, bool writeAllocate)
        : size_power2(size_power2), blockSize_power2(blockSize_power2), associativity_power2(associativity_power2), cycleTime(cycleTime), writeAllocate(writeAllocate), hits(0), misses(0) {
        calculateNumSetsAndBits();
    }

    void setL1Cache(Cache* l1CachePtr) {
        l1Cache = l1CachePtr;
    }

    void invalidateBlock(unsigned address) {
        unsigned setIndex = getSetIndex(address);
        unsigned tag = getTag(address);

        for (auto& block : cacheSets[setIndex]) {
            if (block.valid && block.tag == tag) {
                block.valid = false;
                return;
            }
        }
    }

    void writeBackBlock(unsigned address, Cache* nextLevelCache) {
        if (nextLevelCache) {
            nextLevelCache->updateBlock(address, false);
        }
    }

    void updateBlock(unsigned address, bool updateLRU = true) {
        unsigned setIndex = getSetIndex(address);
        unsigned tag = getTag(address);

        for (unsigned i = 0; i < cacheSets[setIndex].size(); i++) {
            if (cacheSets[setIndex][i].valid && cacheSets[setIndex][i].tag == tag) {
                cacheSets[setIndex][i].dirty = true;
                if (updateLRU) {
                    globalWriteBackAddress = address;
                    globalWriteBackDirty = true;
                    this->updateLRU(setIndex, i);
                }
                return;
            }
        }
    }

    bool access(unsigned address, char operation, Cache* nextLevelCache = NULL) {
        unsigned setIndex = getSetIndex(address);
        unsigned tag = getTag(address);

        for (unsigned i = 0; i < cacheSets[setIndex].size(); i++) {
            if (cacheSets[setIndex][i].valid && cacheSets[setIndex][i].tag == tag) {
                hits++;
                updateLRU(setIndex, i);
                if (operation == 'w') {
                    cacheSets[setIndex][i].dirty = true;
                }
                if(l1Cache){
                    if(globalWriteBackDirty){
                        this->updateBlock(globalWriteBackAddress, true);
                        globalWriteBackAddress = 0;
                        globalWriteBackDirty = false;
                    }
                }
                if(nextLevelCache){
                    globall1evictornot = false;
                }
                return true;
            }
        }

        misses++;

        if (operation == 'r' || (operation == 'w' && writeAllocate)) {
            for (unsigned i = 0; i < cacheSets[setIndex].size(); i++) {
                if (!cacheSets[setIndex][i].valid) {
                    cacheSets[setIndex][i] = {tag, true, (operation == 'w')};
                    updateLRU(setIndex, i);
                    if(l1Cache){
                        if(globalWriteBackDirty){
                            updateBlock(globalWriteBackAddress, true);
                            globalWriteBackAddress = 0;
                            globalWriteBackDirty = false;
                        }
                    }
                    if(nextLevelCache){
                        globall1evictornot = false;
                    }
                    return false;
                }
            }

            unsigned lruIndex = getLRUBlockIndex(setIndex);
            CacheBlock& evictedBlock = cacheSets[setIndex][lruIndex];

            unsigned evictedAddress = (evictedBlock.tag << (blockSize_power2 + setIndexBits)) | (setIndex << blockSize_power2);

            if (nextLevelCache) {
                globall1evictornot = true;
                if(evictedBlock.dirty){
                    globalWriteBackAddress = evictedAddress;
                    globalWriteBackDirty = true;
                }else{
                        globalWriteBackAddress = 0;
                        globalWriteBackDirty = false;
                }
            }

            if (l1Cache) {
                l1Cache->invalidateBlock(evictedAddress);
                if (globall1evictornot && globalWriteBackDirty) {
                    updateBlock(globalWriteBackAddress, true);
                }
            }

            cacheSets[setIndex][lruIndex] = {tag, true, (operation == 'w')};
            updateLRU(setIndex, lruIndex);
        }

        return false;
    }

    double getMissRate() const {
        return (misses == 0) ? 0.0 : (double)misses / (hits + misses);
    }

    unsigned getHits() const {
        return hits;
    }

    unsigned getMisses() const {
        return misses;
    }

    unsigned getCycleTime() const {
        return cycleTime;
    }
};

class CacheSystem {
public:

    Cache l1Cache;
    Cache l2Cache;
    unsigned memCycleTime;
    unsigned totalAccesses;
    double totalAccessTime;

    CacheSystem(unsigned l1Size, unsigned l1BlockSize, unsigned l1Assoc, unsigned l1Cycle, 
                unsigned l2Size, unsigned l2BlockSize, unsigned l2Assoc, unsigned l2Cycle, 
                unsigned memCycle, bool writeAllocate)
        : l1Cache(l1Size, l1BlockSize, l1Assoc, l1Cycle, writeAllocate),
          l2Cache(l2Size, l2BlockSize, l2Assoc, l2Cycle, writeAllocate),
          memCycleTime(memCycle), totalAccesses(0), totalAccessTime(0.0) {
        l2Cache.setL1Cache(&l1Cache);
    }

    void accessMemory(unsigned address, char operation) {
        totalAccesses++;
        if (l1Cache.access(address, operation, &l2Cache)) {
            totalAccessTime += l1Cache.getCycleTime();
        } else if (l2Cache.access(address, operation)) {
            totalAccessTime += l1Cache.getCycleTime() + l2Cache.getCycleTime();
        } else {
            totalAccessTime += l1Cache.getCycleTime() + l2Cache.getCycleTime() + memCycleTime;
        }
    }

    void printStats() const {
        double l1MissRate = l1Cache.getMissRate();
        double l2MissRate = l2Cache.getMissRate();
        double avgAccessTime = totalAccesses > 0 ? totalAccessTime / totalAccesses : 0.0;

        std::cout << "L1miss=" << l1MissRate << " ";
        std::cout << "L2miss=" << l2MissRate << " ";
        std::cout << "AccTimeAvg=" << avgAccessTime << std::endl;
    }
};
