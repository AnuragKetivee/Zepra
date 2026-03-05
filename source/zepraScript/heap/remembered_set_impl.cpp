/**
 * @file remembered_set_impl.cpp
 * @brief Card table + hash-based remembered set for generational GC
 *
 * Two-tier remembered set:
 * 1. Coarse-grained: Card table (512-byte cards, 1 byte per card)
 * 2. Fine-grained: Hash set of exact slot addresses
 *
 * On write barrier fire:
 * - Mark card dirty in card table (O(1), no lock)
 * - If card is "hot" (dirtied frequently), promote to hash set
 *
 * On minor GC:
 * - Scan dirty cards for old→young references
 * - For fine-grained entries, visit exact slots
 * - Clear cards after scan
 *
 * Card states:
 * - CLEAN: no old→young references known
 * - DIRTY: contains old→young reference(s)
 * - YOUNG: card is in young generation (skip during scan)
 * - HOT: frequently dirtied, promoted to hash set
 */

#include <atomic>
#include <mutex>
#include <vector>
#include <functional>
#include <chrono>
#include <cstring>
#include <cassert>
#include <memory>
#include <algorithm>
#include <unordered_set>

namespace Zepra::Heap {

// =============================================================================
// Card Table Implementation
// =============================================================================

class CardTableImpl {
public:
    static constexpr size_t CARD_SIZE = 512;
    static constexpr uint8_t CLEAN = 0;
    static constexpr uint8_t DIRTY = 1;
    static constexpr uint8_t YOUNG = 2;
    static constexpr uint8_t HOT   = 3;

    CardTableImpl() = default;

    void initialize(void* heapBase, size_t heapSize) {
        base_ = static_cast<char*>(heapBase);
        heapSize_ = heapSize;
        cardCount_ = (heapSize + CARD_SIZE - 1) / CARD_SIZE;
        cards_ = std::make_unique<std::atomic<uint8_t>[]>(cardCount_);
        dirtyCount_ = std::make_unique<uint16_t[]>(cardCount_);
        for (size_t i = 0; i < cardCount_; i++) {
            cards_[i].store(CLEAN, std::memory_order_relaxed);
            dirtyCount_[i] = 0;
        }
    }

    void markDirty(void* addr) {
        size_t idx = cardIndex(addr);
        if (idx >= cardCount_) return;

        uint8_t prev = cards_[idx].load(std::memory_order_relaxed);
        if (prev == DIRTY || prev == HOT) return;

        cards_[idx].store(DIRTY, std::memory_order_relaxed);
        dirtyCount_[idx]++;
    }

    void markYoung(void* addr) {
        size_t idx = cardIndex(addr);
        if (idx < cardCount_) {
            cards_[idx].store(YOUNG, std::memory_order_relaxed);
        }
    }

    bool isDirty(size_t idx) const {
        if (idx >= cardCount_) return false;
        uint8_t s = cards_[idx].load(std::memory_order_relaxed);
        return s == DIRTY || s == HOT;
    }

    bool isHot(size_t idx) const {
        if (idx >= cardCount_) return false;
        return dirtyCount_[idx] > 8;
    }

    void clearCard(size_t idx) {
        if (idx >= cardCount_) return;
        cards_[idx].store(CLEAN, std::memory_order_relaxed);
    }

    void clearAll() {
        for (size_t i = 0; i < cardCount_; i++) {
            cards_[i].store(CLEAN, std::memory_order_relaxed);
        }
    }

    void forEachDirty(std::function<void(void* start, void* end, size_t cardIdx)> cb) {
        for (size_t i = 0; i < cardCount_; i++) {
            uint8_t s = cards_[i].load(std::memory_order_relaxed);
            if (s == DIRTY || s == HOT) {
                void* start = base_ + i * CARD_SIZE;
                void* end = base_ + std::min((i + 1) * CARD_SIZE, heapSize_);
                cb(start, end, i);
            }
        }
    }

    size_t dirtyCardCount() const {
        size_t count = 0;
        for (size_t i = 0; i < cardCount_; i++) {
            uint8_t s = cards_[i].load(std::memory_order_relaxed);
            if (s == DIRTY || s == HOT) count++;
        }
        return count;
    }

    size_t cardIndex(const void* addr) const {
        return static_cast<size_t>(static_cast<const char*>(addr) - base_) / CARD_SIZE;
    }

    void* cardAddress(size_t idx) const { return base_ + idx * CARD_SIZE; }
    size_t totalCards() const { return cardCount_; }

private:
    char* base_ = nullptr;
    size_t heapSize_ = 0;
    size_t cardCount_ = 0;
    std::unique_ptr<std::atomic<uint8_t>[]> cards_;
    std::unique_ptr<uint16_t[]> dirtyCount_;
};

// =============================================================================
// Slot Set (Fine-grained remembered set)
// =============================================================================

class SlotSet {
public:
    void insert(void** slot) {
        std::lock_guard<std::mutex> lock(mutex_);
        slots_.insert(slot);
    }

    void remove(void** slot) {
        std::lock_guard<std::mutex> lock(mutex_);
        slots_.erase(slot);
    }

    bool contains(void** slot) const {
        std::lock_guard<std::mutex> lock(mutex_);
        return slots_.count(slot) > 0;
    }

    void forEach(std::function<void(void** slot)> cb) {
        std::lock_guard<std::mutex> lock(mutex_);
        for (auto* slot : slots_) {
            cb(slot);
        }
    }

    void clear() {
        std::lock_guard<std::mutex> lock(mutex_);
        slots_.clear();
    }

    size_t size() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return slots_.size();
    }

private:
    std::unordered_set<void**> slots_;
    mutable std::mutex mutex_;
};

// =============================================================================
// Remembered Set
// =============================================================================

class RememberedSetImpl {
public:
    struct Config {
        size_t hotCardThreshold;
        bool enableSlotPromoting;

        Config()
            : hotCardThreshold(8)
            , enableSlotPromoting(true) {}
    };

    struct Stats {
        size_t cardsScanned = 0;
        size_t slotsScanned = 0;
        size_t referencesFound = 0;
        double scanMs = 0;
    };

    explicit RememberedSetImpl(const Config& config = Config{}) : config_(config) {}

    void initialize(void* heapBase, size_t heapSize) {
        cardTable_.initialize(heapBase, heapSize);
    }

    /**
     * @brief Record old→young write (called from write barrier)
     */
    void recordStore(void** slot, void* /*sourceObj*/) {
        cardTable_.markDirty(slot);

        size_t cardIdx = cardTable_.cardIndex(slot);
        if (config_.enableSlotPromoting && cardTable_.isHot(cardIdx)) {
            slotSet_.insert(slot);
        }
    }

    /**
     * @brief Scan for old→young references (called during minor GC)
     */
    Stats scan(std::function<bool(void*)> isYoung,
               std::function<void(void** slot)> visitor) {
        Stats stats;
        auto start = std::chrono::steady_clock::now();

        // Scan fine-grained slots first
        slotSet_.forEach([&](void** slot) {
            stats.slotsScanned++;
            if (slot && *slot && isYoung(*slot)) {
                visitor(slot);
                stats.referencesFound++;
            }
        });

        // Scan dirty cards
        cardTable_.forEachDirty([&](void* cardStart, void* cardEnd, size_t cardIdx) {
            stats.cardsScanned++;

            // Scan card range for pointer-sized values
            auto* start_ptr = static_cast<void**>(cardStart);
            auto* end_ptr = static_cast<void**>(cardEnd);
            for (auto* p = start_ptr; p < end_ptr; p++) {
                if (*p && isYoung(*p)) {
                    visitor(p);
                    stats.referencesFound++;
                }
            }

            cardTable_.clearCard(cardIdx);
        });

        stats.scanMs = std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - start).count();

        return stats;
    }

    void clear() {
        cardTable_.clearAll();
        slotSet_.clear();
    }

    CardTableImpl& cardTable() { return cardTable_; }
    SlotSet& slotSet() { return slotSet_; }

private:
    Config config_;
    CardTableImpl cardTable_;
    SlotSet slotSet_;
};

} // namespace Zepra::Heap
