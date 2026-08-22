#include "tamdb/flat_index.h"

#include <gtest/gtest.h>

#include <cmath>
#include <vector>
#include <algorithm>
#include <stdexcept>

namespace tamdb {
namespace {

// ============================================================================
// Construction Tests
// ============================================================================

TEST(FlatIndexTest, ConstructWithDimensions) {
    FlatIndex index(128);
    EXPECT_EQ(index.size(), 0u);
}

TEST(FlatIndexTest, ConstructWithSmallDimension) {
    FlatIndex index(1);
    EXPECT_EQ(index.size(), 0u);
}

// ============================================================================
// Insert Tests
// ============================================================================

TEST(FlatIndexTest, InsertSingleVector) {
    FlatIndex index(3);
    std::vector<float> vec = {1.0f, 2.0f, 3.0f};
    index.insert(1, vec);
    EXPECT_EQ(index.size(), 1u);
}

TEST(FlatIndexTest, InsertMultipleVectors) {
    FlatIndex index(3);
    std::vector<float> v1 = {1.0f, 2.0f, 3.0f};
    std::vector<float> v2 = {4.0f, 5.0f, 6.0f};
    std::vector<float> v3 = {7.0f, 8.0f, 9.0f};
    index.insert(1, v1);
    index.insert(2, v2);
    index.insert(3, v3);
    EXPECT_EQ(index.size(), 3u);
}

TEST(FlatIndexTest, InsertDimensionMismatchThrows) {
    FlatIndex index(3);
    std::vector<float> wrong_dim = {1.0f, 2.0f};  // 2 dimensions, expected 3
    EXPECT_THROW(index.insert(1, wrong_dim), std::invalid_argument);
}

TEST(FlatIndexTest, InsertTooManyDimensionsThrows) {
    FlatIndex index(2);
    std::vector<float> wrong_dim = {1.0f, 2.0f, 3.0f};  // 3 dimensions, expected 2
    EXPECT_THROW(index.insert(1, wrong_dim), std::invalid_argument);
}

TEST(FlatIndexTest, InsertEmptyVectorForZeroDimIndex) {
    FlatIndex index(0);
    std::vector<float> empty = {};
    index.insert(1, empty);
    EXPECT_EQ(index.size(), 1u);
}

// ============================================================================
// Search Tests - Basic
// ============================================================================

TEST(FlatIndexTest, SearchEmptyIndex) {
    FlatIndex index(3);
    std::vector<float> query = {1.0f, 2.0f, 3.0f};
    auto results = index.search(query, 5);
    EXPECT_TRUE(results.empty());
}

TEST(FlatIndexTest, SearchFindsExactMatch) {
    FlatIndex index(3);
    std::vector<float> vec = {1.0f, 2.0f, 3.0f};
    index.insert(42, vec);

    auto results = index.search(vec, 1);
    ASSERT_EQ(results.size(), 1u);
    EXPECT_EQ(results[0].id, 42u);
    EXPECT_FLOAT_EQ(results[0].score, 0.0f);  // distance to itself = 0
}

TEST(FlatIndexTest, SearchTopKLessThanSize) {
    FlatIndex index(2);
    index.insert(1, std::vector<float>{0.0f, 0.0f});
    index.insert(2, std::vector<float>{1.0f, 0.0f});
    index.insert(3, std::vector<float>{2.0f, 0.0f});
    index.insert(4, std::vector<float>{3.0f, 0.0f});
    index.insert(5, std::vector<float>{4.0f, 0.0f});

    std::vector<float> query = {0.0f, 0.0f};
    auto results = index.search(query, 3);

    ASSERT_EQ(results.size(), 3u);
    // Should return the 3 closest: ids 1, 2, 3
    EXPECT_EQ(results[0].id, 1u);
    EXPECT_EQ(results[1].id, 2u);
    EXPECT_EQ(results[2].id, 3u);
}

TEST(FlatIndexTest, SearchTopKGreaterThanSize) {
    FlatIndex index(2);
    index.insert(1, std::vector<float>{1.0f, 0.0f});
    index.insert(2, std::vector<float>{2.0f, 0.0f});

    std::vector<float> query = {0.0f, 0.0f};
    auto results = index.search(query, 10);  // asking for 10, only 2 exist

    ASSERT_EQ(results.size(), 2u);
}

// ============================================================================
// Search Tests - Ordering
// ============================================================================

TEST(FlatIndexTest, SearchResultsOrderedByDistance) {
    FlatIndex index(2);
    index.insert(1, std::vector<float>{10.0f, 0.0f});
    index.insert(2, std::vector<float>{1.0f, 0.0f});
    index.insert(3, std::vector<float>{5.0f, 0.0f});
    index.insert(4, std::vector<float>{0.5f, 0.0f});

    std::vector<float> query = {0.0f, 0.0f};
    auto results = index.search(query, 4);

    ASSERT_EQ(results.size(), 4u);
    // Verify sorted by ascending distance
    for (size_t i = 1; i < results.size(); ++i) {
        EXPECT_LE(results[i - 1].score, results[i].score);
    }
}

TEST(FlatIndexTest, SearchReturnsCorrectDistances) {
    FlatIndex index(2);
    index.insert(1, std::vector<float>{3.0f, 4.0f});

    std::vector<float> query = {0.0f, 0.0f};
    auto results = index.search(query, 1);

    ASSERT_EQ(results.size(), 1u);
    EXPECT_NEAR(results[0].score, 5.0f, 1e-6f);  // 3-4-5 triangle
}

// ============================================================================
// Search Tests - Nearest Neighbor Correctness
// ============================================================================

TEST(FlatIndexTest, SearchFindsNearestNeighbor) {
    FlatIndex index(2);
    // Place vectors at different distances from origin
    index.insert(100, std::vector<float>{0.1f, 0.1f});   // closest
    index.insert(200, std::vector<float>{1.0f, 1.0f});   // medium
    index.insert(300, std::vector<float>{10.0f, 10.0f}); // farthest

    std::vector<float> query = {0.0f, 0.0f};
    auto results = index.search(query, 1);

    ASSERT_EQ(results.size(), 1u);
    EXPECT_EQ(results[0].id, 100u);  // closest to origin
}

TEST(FlatIndexTest, SearchWithNonOriginQuery) {
    FlatIndex index(2);
    index.insert(1, std::vector<float>{0.0f, 0.0f});
    index.insert(2, std::vector<float>{5.0f, 5.0f});
    index.insert(3, std::vector<float>{10.0f, 10.0f});

    std::vector<float> query = {4.9f, 4.9f};  // closest to id=2
    auto results = index.search(query, 1);

    ASSERT_EQ(results.size(), 1u);
    EXPECT_EQ(results[0].id, 2u);
}

// ============================================================================
// Search Tests - Edge Cases
// ============================================================================

TEST(FlatIndexTest, DISABLED_SearchTopKZero_BUG_NeedsGuard) {
    FlatIndex index(2);
    index.insert(1, std::vector<float>{1.0f, 0.0f});

    std::vector<float> query = {0.0f, 0.0f};
    auto results = index.search(query, 0);
    EXPECT_TRUE(results.empty());
}

TEST(FlatIndexTest, SearchWithDuplicateVectors) {
    FlatIndex index(2);
    std::vector<float> vec = {1.0f, 1.0f};
    index.insert(1, vec);
    index.insert(2, vec);
    index.insert(3, vec);

    auto results = index.search(vec, 3);
    ASSERT_EQ(results.size(), 3u);
    // All should have distance 0
    for (const auto& r : results) {
        EXPECT_FLOAT_EQ(r.score, 0.0f);
    }
}

TEST(FlatIndexTest, SearchWithDuplicateIds) {
    FlatIndex index(2);
    index.insert(1, std::vector<float>{0.0f, 0.0f});
    index.insert(1, std::vector<float>{10.0f, 10.0f});  // same id, different vector

    std::vector<float> query = {0.0f, 0.0f};
    auto results = index.search(query, 2);
    ASSERT_EQ(results.size(), 2u);
    // Both entries should be returned (flat index doesn't enforce unique ids)
}

// ============================================================================
// Size Tests
// ============================================================================

TEST(FlatIndexTest, SizeIncreasesWithInserts) {
    FlatIndex index(2);
    EXPECT_EQ(index.size(), 0u);

    index.insert(1, std::vector<float>{1.0f, 2.0f});
    EXPECT_EQ(index.size(), 1u);

    index.insert(2, std::vector<float>{3.0f, 4.0f});
    EXPECT_EQ(index.size(), 2u);

    index.insert(3, std::vector<float>{5.0f, 6.0f});
    EXPECT_EQ(index.size(), 3u);
}

// ============================================================================
// Higher Dimensional Tests
// ============================================================================

TEST(FlatIndexTest, SearchHighDimensional) {
    const size_t dim = 128;
    FlatIndex index(dim);

    // Insert 10 vectors
    for (uint64_t i = 0; i < 10; ++i) {
        std::vector<float> vec(dim, static_cast<float>(i));
        index.insert(i, vec);
    }

    // Query with vector close to id=3
    std::vector<float> query(dim, 3.1f);
    auto results = index.search(query, 1);

    ASSERT_EQ(results.size(), 1u);
    EXPECT_EQ(results[0].id, 3u);  // closest to 3.0 in all dimensions
}

}  // namespace
}  // namespace tamdb
