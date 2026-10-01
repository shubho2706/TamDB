#include "tamdb/hnsw/hnsw_index.h"
#include "tamdb/naive/flat_index.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <random>
#include <span>
#include <unordered_set>
#include <vector>
#include <thread>

namespace tamdb {
namespace {

// Build a reproducible random vector in [-1, 1]^dim.
std::vector<float> random_vector(std::mt19937& rng, size_t dim) {
    std::uniform_real_distribution<float> dist(-1.0f, 1.0f);
    std::vector<float> v(dim);
    for (float& x : v) {
        x = dist(rng);
    }
    return v;
}

// ============================================================================
// Construction / empty index
// ============================================================================

TEST(HNSWIndexTest, SearchEmptyIndexReturnsEmpty) {
    HNSWIndex index(16, 200, 4);
    std::vector<float> query = {1.0f, 2.0f, 3.0f, 4.0f};
    auto results = index.search(query, 5, 50);
    EXPECT_TRUE(results.empty());
}

// ============================================================================
// Insert
// ============================================================================

TEST(HNSWIndexTest, InsertSingleAndFindIt) {
    HNSWIndex index(16, 200, 3);
    std::vector<float> v = {1.0f, 2.0f, 3.0f};
    index.insert(42, v);

    auto results = index.search(v, 1, 50);
    ASSERT_EQ(results.size(), 1u);
    EXPECT_EQ(results[0].id, 42u);
    EXPECT_NEAR(results[0].score, 0.0f, 1e-4f);  // distance to itself ~ 0
}

TEST(HNSWIndexTest, InsertDimensionMismatchThrows) {
    HNSWIndex index(16, 200, 3);
    std::vector<float> wrong_dim = {1.0f, 2.0f};  // expected 3
    EXPECT_THROW(index.insert(1, wrong_dim), std::invalid_argument);
}

// ============================================================================
// Search basics
// ============================================================================

TEST(HNSWIndexTest, TopKGreaterThanSize) {
    HNSWIndex index(16, 200, 2);
    std::vector<float> a = {0.0f, 0.0f};
    std::vector<float> b = {1.0f, 1.0f};
    index.insert(1, a);
    index.insert(2, b);

    std::vector<float> query = {0.0f, 0.0f};
    auto results = index.search(query, 10, 50);  // ask for 10, only 2 exist
    EXPECT_EQ(results.size(), 2u);
}

TEST(HNSWIndexTest, ResultsSortedByAscendingDistance) {
    HNSWIndex index(16, 200, 2);
    std::vector<float> v1 = {10.0f, 0.0f};
    std::vector<float> v2 = {1.0f, 0.0f};
    std::vector<float> v3 = {5.0f, 0.0f};
    std::vector<float> v4 = {0.5f, 0.0f};
    index.insert(1, v1);
    index.insert(2, v2);
    index.insert(3, v3);
    index.insert(4, v4);

    std::vector<float> query = {0.0f, 0.0f};
    auto results = index.search(query, 4, 50);

    ASSERT_EQ(results.size(), 4u);
    for (size_t i = 1; i < results.size(); ++i) {
        EXPECT_LE(results[i - 1].score, results[i].score);
    }
}

TEST(HNSWIndexTest, DuplicateVectors) {
    HNSWIndex index(16, 200, 2);
    std::vector<float> v = {1.0f, 1.0f};
    index.insert(1, v);
    index.insert(2, v);
    index.insert(3, v);

    auto results = index.search(v, 3, 50);
    ASSERT_EQ(results.size(), 3u);
    for (const auto& r : results) {
        EXPECT_NEAR(r.score, 0.0f, 1e-4f);
    }
}

TEST(HNSWIndexTest, FindsNearestNeighborDeterministic) {
    HNSWIndex index(16, 200, 2);
    std::vector<float> near_v = {0.1f, 0.1f};     // closest to origin
    std::vector<float> mid_v = {1.0f, 1.0f};      // medium
    std::vector<float> far_v = {10.0f, 10.0f};    // farthest
    index.insert(100, near_v);
    index.insert(200, mid_v);
    index.insert(300, far_v);

    std::vector<float> query = {0.0f, 0.0f};
    auto results = index.search(query, 1, 50);

    ASSERT_EQ(results.size(), 1u);
    EXPECT_EQ(results[0].id, 100u);
}

// ============================================================================
// Recall against the brute-force FlatIndex (the correctness oracle).
//
// HNSW is approximate, so we don't demand exact matches -- we assert that its
// top-k overlaps the exact top-k by a high fraction on average.
// ============================================================================

TEST(HNSWIndexTest, RecallAgainstFlatIndex) {
    constexpr size_t kDim = 12;
    constexpr size_t kNumVectors = 300;
    constexpr size_t kNumQueries = 50;
    constexpr size_t kTopK = 10;
    constexpr size_t kEfSearch = 64;

    std::mt19937 rng(12345);  // fixed seed -> reproducible dataset

    HNSWIndex hnsw(16, 200, kDim);
    FlatIndex flat(kDim);

    for (size_t i = 0; i < kNumVectors; ++i) {
        std::vector<float> v = random_vector(rng, kDim);
        hnsw.insert(static_cast<uint64_t>(i), v);
        flat.insert(static_cast<uint64_t>(i), v);
    }

    double total_recall = 0.0;
    for (size_t q = 0; q < kNumQueries; ++q) {
        std::vector<float> query = random_vector(rng, kDim);

        auto truth = flat.search(query, kTopK);           // exact top-k
        auto approx = hnsw.search(query, kTopK, kEfSearch);  // approximate top-k

        std::unordered_set<uint64_t> truth_ids;
        for (const auto& r : truth) {
            truth_ids.insert(r.id);
        }

        size_t hits = 0;
        for (const auto& r : approx) {
            if (truth_ids.count(r.id) > 0) {
                ++hits;
            }
        }
        total_recall += static_cast<double>(hits) / static_cast<double>(kTopK);
    }

    double avg_recall = total_recall / static_cast<double>(kNumQueries);
    // A correctly built HNSW graph clears this comfortably (~0.95+ for these
    // params); a broken graph (bad edges, wrong distances) drops well below.
    EXPECT_GE(avg_recall, 0.85) << "avg recall@" << kTopK << " = " << avg_recall;
}

TEST(HNSWIndexTest, HigherEfSearchDoesNotReduceRecall) {
    constexpr size_t kDim = 12;
    constexpr size_t kNumVectors = 300;
    constexpr size_t kTopK = 10;

    std::mt19937 rng(999);

    HNSWIndex hnsw(16, 200, kDim);
    FlatIndex flat(kDim);
    for (size_t i = 0; i < kNumVectors; ++i) {
        std::vector<float> v = random_vector(rng, kDim);
        hnsw.insert(static_cast<uint64_t>(i), v);
        flat.insert(static_cast<uint64_t>(i), v);
    }

    auto recall_at = [&](size_t ef_search) {
        double total = 0.0;
        constexpr size_t kNumQueries = 30;
        std::mt19937 qrng(7);
        for (size_t q = 0; q < kNumQueries; ++q) {
            std::vector<float> query = random_vector(qrng, kDim);
            auto truth = flat.search(query, kTopK);
            auto approx = hnsw.search(query, kTopK, ef_search);
            std::unordered_set<uint64_t> truth_ids;
            for (const auto& r : truth) truth_ids.insert(r.id);
            size_t hits = 0;
            for (const auto& r : approx) hits += truth_ids.count(r.id) > 0 ? 1 : 0;
            total += static_cast<double>(hits) / static_cast<double>(kTopK);
        }
        return total / static_cast<double>(kNumQueries);
    };

    double low = recall_at(16);
    double high = recall_at(128);
    // A wider beam should never do worse than a narrow one (small slack for ties).
    EXPECT_GE(high, low - 0.05) << "low(ef=16)=" << low << " high(ef=128)=" << high;
}


TEST(HNSWIndexTest, ConcurrentInsertAndSearch) {
    constexpr size_t kDim = 16;
    HNSWIndex index(16, 200, kDim);

    // Insert a seed vector so search has something to find
    std::vector<float> seed(kDim, 0.5f);
    index.insert(0, seed);

    // Writer thread: insert 100 vectors
    std::thread writer([&]() {
        std::mt19937 rng(42);
        std::uniform_real_distribution<float> dist(-1.0f, 1.0f);
        for (uint64_t i = 1; i <= 100; ++i) {
            std::vector<float> v(kDim);
            for (auto& x : v) x = dist(rng);
            index.insert(i, v);
        }
    });

    // Reader thread: search 100 times concurrently
    std::thread reader([&]() {
        std::mt19937 rng(99);
        std::uniform_real_distribution<float> dist(-1.0f, 1.0f);
        for (int i = 0; i < 100; ++i) {
            std::vector<float> q(kDim);
            for (auto& x : q) x = dist(rng);
            auto results = index.search(q, 5, 50);
            // Just verify no crash and results are valid
            for (const auto& r : results) {
                EXPECT_GE(r.score, 0.0f);
            }
        }
    });

    writer.join();
    reader.join();

    // No crash, no deadlock = pass
}
}  // namespace
}  // namespace tamdb
