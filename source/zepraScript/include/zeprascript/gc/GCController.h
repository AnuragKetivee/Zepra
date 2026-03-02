/**
 * @file GCController.h
 * @brief Unified GC controller coordinating all GC components
 * 
 * Implements:
 * - GC scheduling and triggers
 * - Coordination between nursery/old-gen
 * - Coordination between nursery/old-gen
 * - Concurrent/incremental GC management
 */

#pragma once

#include "Nursery.h"
#include "OldGeneration.h"
#include "WriteBarrier.h"
#include "concurrent_gc.hpp"
#include <chrono>
#include <atomic>

namespace Zepra::GC {

/**
 * @brief GC trigger policy
 */
enum class GCTrigger : uint8_t {
    Allocation,     // Triggered by allocation pressure
    Timer,          // Periodic background GC
    Memory,         // System memory pressure  
    Explicit,       // Explicit GC.collect()
    Emergency       // OOM prevention
};

/**
 * @brief GC phase
 */
enum class GCPhase : uint8_t {
    Idle,
    MinorGC,
    MajorGCMarking,
    MajorGCSweeping,
    Compacting
};

/**
 * @brief Unified GC statistics
 */
struct GCControllerStats {
    size_t minorGCs = 0;
    size_t majorGCs = 0;
    size_t totalPauseMs = 0;
    size_t maxPauseMs = 0;
    size_t totalAllocated = 0;
    size_t totalReclaimed = 0;
    size_t heapSize = 0;
    size_t liveSize = 0;
};

/**
 * @brief GC scheduling parameters
 */
struct GCSchedule {
    // Minor GC triggers
    size_t nurseryThreshold = 3 * 1024 * 1024;  // 3MB
    
    // Major GC triggers  
    double heapGrowthFactor = 2.0;  // Trigger at 2x live size
    size_t minHeapForMajor = 8 * 1024 * 1024;  // 8MB
    
    // Concurrent GC
    bool enableConcurrent = true;
    unsigned concurrentWorkers = 2;
    
    // Compaction
    double fragmentationThreshold = 0.3;  // 30% fragmentation
    
    // Timing
    size_t targetPauseMs = 10;
    size_t maxPauseMs = 50;
};

/**
 * @brief Unified GC controller
 */
class GCController {
public:
    GCController() = default;
    ~GCController() = default;
    
    /**
     * @brief Initialize all GC components
     */
    bool init(size_t nurserySize = Nursery::DEFAULT_SIZE) {
        if (!nursery_.init(nurserySize)) return false;
        if (!oldGen_.init()) return false;
        
        // Initialize write barriers
        barriers_.init(nursery_.allocPtrAddress(), nurserySize);
        
        // Initialize concurrent GC if enabled
        if (schedule_.enableConcurrent) {
            concurrentGC_.init(nullptr);  // Would pass heap
            concurrentGC_.setWorkerCount(schedule_.concurrentWorkers);
        }
        
        phase_.store(GCPhase::Idle, std::memory_order_release);
        return true;
    }
    
    /**
     * @brief Allocate object
     */
    template<typename T, typename... Args>
    T* allocate(Args&&... args) {
        size_t size = sizeof(Runtime::ObjectHeader) + sizeof(T);
        
        // Try nursery first
        void* mem = nursery_.allocate(size);
        
        if (!mem) {
            // Trigger minor GC
            collectMinor();
            mem = nursery_.allocate(size);
        }
        
        if (!mem) {
            // Fall back to old gen
            mem = oldGen_.allocate(size);
        }
        
        if (!mem) {
            // Emergency major GC
            collectMajor(GCTrigger::Emergency);
            mem = oldGen_.allocate(size);
        }
        /**
 * @file GCController.h
 * @brief Unified GC controller coordinating all GC components
 * 
 * Implements:
 * - GC scheduling and triggers
 * - Coordination between nursery/old-gen
 * - Concurrent/incremental GC management
 * 
 * Based on V8/JSC GC schedulers
 */

#pragma once

#include "Nursery.h"
#include "OldGeneration.h"
#include "WriteBarrier.h"
#include "concurrent_gc.hpp"
#include <chrono>
#include <atomic>

namespace Zepra::GC {

/**
 * @brief GC trigger policy
 */
enum class GCTrigger : uint8_t {
    Allocation,     // Triggered by allocation pressure
    Timer,          // Periodic background GC
    Memory,         // System memory pressure  
    Explicit,       // Explicit GC.collect()
    Emergency       // OOM prevention
};

/**
 * @brief GC phase
 */
enum class GCPhase : uint8_t {
    Idle,
    MinorGC,
    MajorGCMarking,
    MajorGCSweeping,
    Compacting
};

/**
 * @brief Unified GC statistics
 */
struct GCControllerStats {
    size_t minorGCs = 0;
    size_t majorGCs = 0;
    size_t totalPauseMs = 0;
    size_t maxPauseMs = 0;
    size_t totalAllocated = 0;
    size_t totalReclaimed = 0;
    size_t heapSize = 0;
    size_t liveSize = 0;
};

/**
 * @brief GC scheduling parameters
 */
struct GCSchedule {
    // Minor GC triggers
    size_t nurseryThreshold = 3 * 1024 * 1024;  // 3MB
    
    // Major GC triggers  
    double heapGrowthFactor = 2.0;  // Trigger at 2x live size
    size_t minHeapForMajor = 8 * 1024 * 1024;  // 8MB
    
    // Concurrent GC
    bool enableConcurrent = true;
    unsigned concurrentWorkers = 2;
    
    // Compaction
    double fragmentationThreshold = 0.3;  // 30% fragmentation
    
    // Timing
    size_t targetPauseMs = 10;
    size_t maxPauseMs = 50;
};

/**
 * @brief Unified GC controller
 */
class GCController {
public:
    GCController() = default;
    ~GCController() = default;
    
    /**
     * @brief Initialize all GC components
     */
    bool init(size_t nurserySize = Nursery::DEFAULT_SIZE) {
        if (!nursery_.init(nurserySize)) return false;
        if (!oldGen_.init()) return false;
        
        // Initialize write barriers
        barriers_.init(nursery_.allocPtrAddress(), nurserySize);
        
        // Initialize concurrent GC if enabled
        if (schedule_.enableConcurrent) {
            concurrentGC_.init(nullptr);  // Would pass heap
            concurrentGC_.setWorkerCount(schedule_.concurrentWorkers);
        }
        
        phase_.store(GCPhase::Idle, std::memory_order_release);
        return true;
    }
    
    /**
     * @brief Allocate object
     */
    template<typename T, typename... Args>
    T* allocate(Args&&... args) {
        size_t size = sizeof(Runtime::ObjectHeader) + sizeof(T);
        
        void* mem = nursery_.allocate(size);
        
        if (!mem) {
            collectMinor();
            mem = nursery_.allocate(size);
        }
        
        if (!mem) {
            mem = oldGen_.allocate(size);
        }
        
        if (!mem) {
            collectMajor(GCTrigger::Emergency);
            mem = oldGen_.allocate(size);
        }
        
        if (!mem) return nullptr;
        
        auto* header = new (mem) Runtime::ObjectHeader();
        header->size = sizeof(T);
        header->generation = Runtime::Generation::Young;
        
        T* obj = new (header->object()) T(std::forward<Args>(args)...);
        
        stats_.totalAllocated += size;
        return obj;
    }
    
    /**
     * @brief Trigger minor GC (nursery scavenge)
     */
    void collectMinor();
    
    /**
     * @brief Trigger major GC
     */
    void collectMajor(GCTrigger trigger = GCTrigger::Allocation);
    
    /**
     * @brief Check if GC should be triggered
     */
    void maybeGC() {
        if (nursery_.needsScavenge()) {
            collectMinor();
        }
        
        if (shouldTriggerMajorGC()) {
            collectMajor();
        }
    }
    
    /**
     * @brief Write barrier (call when storing object reference)
     */
    void writeBarrier(Runtime::Object* source, Runtime::Object** slot, Runtime::Object* target) {
        // SATB barrier for concurrent GC
        if (concurrentGC_.isMarking()) {
            Runtime::Object* old = *slot;
            if (old) concurrentGC_.satbWriteBarrier(old);
        }
        
        // Generational barrier
        auto* srcHeader = Runtime::ObjectHeader::fromObject(source);
        if (srcHeader->generation == Runtime::Generation::Old && target) {
            auto* tgtHeader = Runtime::ObjectHeader::fromObject(target);
            if (tgtHeader->generation == Runtime::Generation::Young) {
                barriers_.barrierSlow(source, slot, target);
            }
        }
        
        *slot = target;
    }
    
    // Accessors
    Nursery& nursery() { return nursery_; }
    OldGeneration& oldGen() { return oldGen_; }
    WriteBarrierManager& barriers() { return barriers_; }
    GCPhase phase() const { return phase_.load(std::memory_order_acquire); }
    const GCControllerStats& stats() const { return stats_; }
    GCSchedule& schedule() { return schedule_; }
    
private:
    Nursery nursery_;
    OldGeneration oldGen_;
    WriteBarrierManager barriers_;
    ConcurrentGC concurrentGC_;
    std::unique_ptr<Scavenger> scavenger_;
    
    std::atomic<GCPhase> phase_{GCPhase::Idle};
    GCControllerStats stats_;
    GCSchedule schedule_;
    
    size_t lastLiveSize_ = 0;
    
    void markAll();
    bool shouldCompact() const;
    void compact();
    
    bool shouldTriggerMajorGC() {
        size_t heapSize = stats_.heapSize;
        size_t threshold = static_cast<size_t>(lastLiveSize_ * schedule_.heapGrowthFactor);
        return heapSize >= threshold && heapSize >= schedule_.minHeapForMajor;
    }
    
    void startConcurrentMajorGC() {
        if (!concurrentGC_.startCycle()) return;
        
        phase_.store(GCPhase::MajorGCMarking, std::memory_order_release);
        // Background marking will proceed
        // Sweeping happens after marking completes
    }
};

/**
 * @brief RAII scope for GC-safe code regions
 */
class GCSafeScope {
public:
    explicit GCSafeScope(GCController& gc) : gc_(gc) {
        // Register this thread as in safe-point
    }
    ~GCSafeScope() {
        // Unregister from safe-point
    }
private:
    GCController& gc_;
};

} // namespace Zepra::GC
