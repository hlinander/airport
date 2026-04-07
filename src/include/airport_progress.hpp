#pragma once

#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>

namespace duckdb {

/// Global registry of active airport scans and their progress.
/// This allows external queries to check the progress of running scans.
class AirportProgressRegistry {
public:
    struct ScanProgress {
        std::atomic<double> progress{0.0};
        std::atomic<uint64_t> rows_processed{0};
        std::atomic<uint64_t> total_rows{0};
        std::atomic<uint64_t> peak_memory_bytes{0};
        std::atomic<uint64_t> current_memory_bytes{0};
        std::string description;
    };

    static AirportProgressRegistry& Instance() {
        static AirportProgressRegistry instance;
        return instance;
    }

    /// Register a new scan and return a shared_ptr to its progress tracker
    std::shared_ptr<ScanProgress> RegisterScan(const std::string& trace_id, const std::string& description = "") {
        std::lock_guard<std::mutex> lock(mutex_);
        auto progress = std::make_shared<ScanProgress>();
        progress->description = description;
        active_scans_[trace_id] = progress;
        return progress;
    }

    /// Unregister a scan when it completes
    void UnregisterScan(const std::string& trace_id) {
        std::lock_guard<std::mutex> lock(mutex_);
        active_scans_.erase(trace_id);
    }

    /// Get the combined progress of all active scans (0.0 to 1.0)
    double GetTotalProgress() const {
        std::lock_guard<std::mutex> lock(mutex_);
        if (active_scans_.empty()) {
            return -1.0; // No active scans
        }
        double total = 0.0;
        for (const auto& [id, progress] : active_scans_) {
            total += progress->progress.load(std::memory_order_relaxed);
        }
        return total / static_cast<double>(active_scans_.size());
    }

    /// Get progress for a specific scan
    double GetProgress(const std::string& trace_id) const {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = active_scans_.find(trace_id);
        if (it == active_scans_.end()) {
            return -1.0;
        }
        return it->second->progress.load(std::memory_order_relaxed);
    }

    /// Get the maximum peak memory across all active scans
    uint64_t GetMaxPeakMemory() const {
        std::lock_guard<std::mutex> lock(mutex_);
        uint64_t max_peak = 0;
        for (const auto& [id, progress] : active_scans_) {
            auto peak = progress->peak_memory_bytes.load(std::memory_order_relaxed);
            if (peak > max_peak) max_peak = peak;
        }
        return max_peak;
    }

    /// Get the maximum current memory across all active scans
    uint64_t GetTotalCurrentMemory() const {
        std::lock_guard<std::mutex> lock(mutex_);
        uint64_t max_current = 0;
        for (const auto& [id, progress] : active_scans_) {
            auto current = progress->current_memory_bytes.load(std::memory_order_relaxed);
            if (current > max_current) max_current = current;
        }
        return max_current;
    }

    /// Get the number of active scans
    size_t ActiveScanCount() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return active_scans_.size();
    }

    /// Get info about all active scans
    std::vector<std::tuple<std::string, double, std::string>> GetAllScans() const {
        std::lock_guard<std::mutex> lock(mutex_);
        std::vector<std::tuple<std::string, double, std::string>> result;
        result.reserve(active_scans_.size());
        for (const auto& [id, progress] : active_scans_) {
            result.emplace_back(id, progress->progress.load(std::memory_order_relaxed), progress->description);
        }
        return result;
    }

private:
    AirportProgressRegistry() = default;
    mutable std::mutex mutex_;
    std::unordered_map<std::string, std::shared_ptr<ScanProgress>> active_scans_;
};

} // namespace duckdb
