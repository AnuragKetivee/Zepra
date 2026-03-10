// Copyright (c) 2025 KetiveeAI. All rights reserved.
// Licensed under KPL-2.0. See LICENSE file for details.
// ZepraScript — gc_block_directory.cpp — Block occupancy bitmap, block-level sweep

#include <cstdint>
#include <cstdio>
#include <cassert>
#include <cstring>
#include <mutex>
#include <vector>
#include <functional>
#include <atomic>

namespace Zepra::Heap {

static constexpr size_t kMaxBlocksPerDirectory = 4096;

struct BlockEntry {
    uintptr_t base;
    uint32_t cellCount;
    uint32_t liveCells;
    uint32_t markedCells;
    uint16_t sizeClass;
    uint8_t  flags;
    uint8_t  reserved;

    static constexpr uint8_t kFlagFull       = 0x01;
    static constexpr uint8_t kFlagEmpty      = 0x02;
    static constexpr uint8_t kFlagSweepDone  = 0x04;
    static constexpr uint8_t kFlagPendingSweep = 0x08;

    bool isFull() const { return flags & kFlagFull; }
    bool isEmpty() const { return flags & kFlagEmpty; }
    bool sweepDone() const { return flags & kFlagSweepDone; }
    bool pendingSweep() const { return flags & kFlagPendingSweep; }

    void setFlag(uint8_t f) { flags |= f; }
    void clearFlag(uint8_t f) { flags &= ~f; }

    double occupancy() const {
        return cellCount > 0 ? static_cast<double>(liveCells) / cellCount : 0;
    }

    bool isActive() const { return base != 0; }
};

class BlockDirectory {
public:
    using SweepFn = std::function<size_t(uintptr_t blockBase, uint16_t sizeClass)>;

    explicit BlockDirectory(uint16_t sizeClass)
        : sizeClass_(sizeClass), blockCount_(0), totalLive_(0), totalCapacity_(0) {
        memset(occupancy_, 0, sizeof(occupancy_));
    }

    uint32_t addBlock(uintptr_t base, uint32_t cellCount) {
        std::lock_guard<std::mutex> lock(mutex_);

        if (blockCount_ >= kMaxBlocksPerDirectory) return UINT32_MAX;

        uint32_t idx = blockCount_++;
        blocks_[idx].base = base;
        blocks_[idx].cellCount = cellCount;
        blocks_[idx].liveCells = 0;
        blocks_[idx].markedCells = 0;
        blocks_[idx].sizeClass = sizeClass_;
        blocks_[idx].flags = 0;

        totalCapacity_ += cellCount;
        updateOccupancy(idx);
        return idx;
    }

    void removeBlock(uint32_t index) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (index >= blockCount_) return;

        totalCapacity_ -= blocks_[index].cellCount;
        totalLive_ -= blocks_[index].liveCells;
        blocks_[index] = {};
        clearOccupancyBit(index);
    }

    const BlockEntry* block(uint32_t index) const {
        return index < blockCount_ ? &blocks_[index] : nullptr;
    }

    // Find first block with free space (using occupancy bitmap).
    uint32_t findNonFull() const {
        std::lock_guard<std::mutex> lock(mutex_);
        for (size_t word = 0; word < kMaxBlocksPerDirectory / 64; word++) {
            uint64_t bits = ~occupancy_[word];  // Invert to find non-full (0 bits = has space).
            if (bits == 0) continue;
            uint32_t bit = __builtin_ctzll(bits);
            uint32_t idx = static_cast<uint32_t>(word * 64 + bit);
            if (idx < blockCount_ && blocks_[idx].isActive() && !blocks_[idx].isFull()) {
                return idx;
            }
        }
        return UINT32_MAX;
    }

    void recordAllocation(uint32_t blockIndex) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (blockIndex >= blockCount_) return;

        blocks_[blockIndex].liveCells++;
        totalLive_++;

        if (blocks_[blockIndex].liveCells >= blocks_[blockIndex].cellCount) {
            blocks_[blockIndex].setFlag(BlockEntry::kFlagFull);
            blocks_[blockIndex].clearFlag(BlockEntry::kFlagEmpty);
            setOccupancyBit(blockIndex);
        }
    }

    void recordFree(uint32_t blockIndex) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (blockIndex >= blockCount_) return;
        if (blocks_[blockIndex].liveCells > 0) blocks_[blockIndex].liveCells--;
        if (totalLive_ > 0) totalLive_--;

        blocks_[blockIndex].clearFlag(BlockEntry::kFlagFull);
        clearOccupancyBit(blockIndex);

        if (blocks_[blockIndex].liveCells == 0) {
            blocks_[blockIndex].setFlag(BlockEntry::kFlagEmpty);
        }
    }

    // Sweep all blocks, return total freed bytes.
    size_t sweepAll(SweepFn fn) {
        std::lock_guard<std::mutex> lock(mutex_);
        size_t totalFreed = 0;

        for (uint32_t i = 0; i < blockCount_; i++) {
            if (!blocks_[i].isActive()) continue;
            if (blocks_[i].sweepDone()) continue;

            totalFreed += fn(blocks_[i].base, blocks_[i].sizeClass);
            blocks_[i].setFlag(BlockEntry::kFlagSweepDone);
        }

        return totalFreed;
    }

    // Mark all blocks as needing sweep.
    void markAllForSweep() {
        std::lock_guard<std::mutex> lock(mutex_);
        for (uint32_t i = 0; i < blockCount_; i++) {
            if (blocks_[i].isActive()) {
                blocks_[i].setFlag(BlockEntry::kFlagPendingSweep);
                blocks_[i].clearFlag(BlockEntry::kFlagSweepDone);
            }
        }
    }

    // Collect empty blocks for release.
    std::vector<uint32_t> emptyBlocks() const {
        std::lock_guard<std::mutex> lock(mutex_);
        std::vector<uint32_t> result;
        for (uint32_t i = 0; i < blockCount_; i++) {
            if (blocks_[i].isActive() && blocks_[i].isEmpty()) {
                result.push_back(i);
            }
        }
        return result;
    }

    uint16_t sizeClass() const { return sizeClass_; }
    uint32_t blockCount() const { return blockCount_; }
    size_t totalLive() const { return totalLive_; }
    size_t totalCapacity() const { return totalCapacity_; }

    double overallOccupancy() const {
        return totalCapacity_ > 0 ? static_cast<double>(totalLive_) / totalCapacity_ : 0;
    }

    template<typename Fn>
    void forEachBlock(Fn&& fn) {
        std::lock_guard<std::mutex> lock(mutex_);
        for (uint32_t i = 0; i < blockCount_; i++) {
            if (blocks_[i].isActive()) fn(i, &blocks_[i]);
        }
    }

private:
    void setOccupancyBit(uint32_t idx) {
        occupancy_[idx / 64] |= (1ULL << (idx % 64));
    }

    void clearOccupancyBit(uint32_t idx) {
        occupancy_[idx / 64] &= ~(1ULL << (idx % 64));
    }

    void updateOccupancy(uint32_t idx) {
        if (blocks_[idx].isFull()) setOccupancyBit(idx);
        else clearOccupancyBit(idx);
    }

    uint16_t sizeClass_;
    uint32_t blockCount_;
    size_t totalLive_;
    size_t totalCapacity_;
    mutable std::mutex mutex_;
    BlockEntry blocks_[kMaxBlocksPerDirectory];
    uint64_t occupancy_[kMaxBlocksPerDirectory / 64];
};

} // namespace Zepra::Heap
