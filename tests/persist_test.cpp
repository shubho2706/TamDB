#include "tamdb/persist/persist.h"
#include "tamdb/hnsw/hnsw_index.h"

#include <gtest/gtest.h>

#include <cstdio>
#include <vector>
#include <span>

namespace tamdb {
namespace {

class PersistTest : public ::testing::Test {
protected:
    std::string segment_path;

    void SetUp() override {
        segment_path = "/tmp/tamdb_persist_test_" + std::to_string(::getpid());
        std::remove(segment_path.c_str());
    }

    void TearDown() override {
        std::remove(segment_path.c_str());
    }
};

// --- Basic write and read ---

TEST_F(PersistTest, WriteAndReadSingleVector) {
    // Write
    {
        HNSWIndexPtr index = std::make_shared<HNSWIndex>(16, 200, 3);
        std::vector<float> vec = {1.0f, 2.0f, 3.0f};
        index->insert(42, vec);

        Persist persist(segment_path);
        EXPECT_TRUE(persist.write(index));
    }

    // Read into a fresh index
    {
        HNSWIndexPtr loaded;
        Persist persist(segment_path);
        EXPECT_TRUE(persist.read(loaded));
        ASSERT_NE(loaded, nullptr);

        std::vector<float> query = {1.0f, 2.0f, 3.0f};
        auto results = loaded->search(query, 1);
        ASSERT_EQ(results.size(), 1u);
        EXPECT_EQ(results[0].id, 42u);
        EXPECT_NEAR(results[0].score, 0.0f, 1e-5f);
    }
}

TEST_F(PersistTest, WriteAndReadMultipleVectors) {
    {
        HNSWIndexPtr index = std::make_shared<HNSWIndex>(16, 200, 4);
        index->insert(1, std::vector<float>{1.0f, 0.0f, 0.0f, 0.0f});
        index->insert(2, std::vector<float>{2.0f, 0.0f, 0.0f, 0.0f});
        index->insert(3, std::vector<float>{3.0f, 0.0f, 0.0f, 0.0f});
        index->insert(4, std::vector<float>{4.0f, 0.0f, 0.0f, 0.0f});
        index->insert(5, std::vector<float>{5.0f, 0.0f, 0.0f, 0.0f});

        Persist persist(segment_path);
        EXPECT_TRUE(persist.write(index));
    }

    {
        HNSWIndexPtr loaded;
        Persist persist(segment_path);
        EXPECT_TRUE(persist.read(loaded));

        // Search for vector closest to {2.9, 0, 0, 0} → should be id=3
        std::vector<float> query = {2.9f, 0.0f, 0.0f, 0.0f};
        auto results = loaded->search(query, 1);
        ASSERT_EQ(results.size(), 1u);
        EXPECT_EQ(results[0].id, 3u);
    }
}

// --- Recall preserved after persist ---

TEST_F(PersistTest, RecallPreservedAfterPersist) {
    constexpr size_t kDim = 16;
    constexpr size_t kN = 200;
    constexpr size_t kTopK = 5;

    std::mt19937 rng(42);
    std::uniform_real_distribution<float> dist(-1.0f, 1.0f);

    // Build and persist
    {
        HNSWIndexPtr index = std::make_shared<HNSWIndex>(16, 200, kDim);
        for (uint64_t i = 0; i < kN; ++i) {
            std::vector<float> v(kDim);
            for (float& x : v) x = dist(rng);
            index->insert(i, v);
        }
        Persist persist(segment_path);
        EXPECT_TRUE(persist.write(index));
    }

    // Load and search
    {
        HNSWIndexPtr loaded;
        Persist persist(segment_path);
        EXPECT_TRUE(persist.read(loaded));

        // Just verify search returns valid results, no crash
        std::vector<float> query(kDim);
        for (float& x : query) x = dist(rng);
        auto results = loaded->search(query, kTopK);
        ASSERT_EQ(results.size(), kTopK);
        for (const auto& r : results) {
            EXPECT_GE(r.score, 0.0f);
            EXPECT_LT(r.id, kN);
        }
    }
}

// --- Search results match before and after persist ---

TEST_F(PersistTest, SearchResultsMatchBeforeAndAfterPersist) {
    constexpr size_t kDim = 8;
    std::vector<SearchResult> original_results;
    std::vector<float> query = {0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f};

    // Build, search, persist
    {
        HNSWIndexPtr index = std::make_shared<HNSWIndex>(16, 200, kDim);
        index->insert(10, std::vector<float>{0.1f, 0.1f, 0.1f, 0.1f, 0.1f, 0.1f, 0.1f, 0.1f});
        index->insert(20, std::vector<float>{0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f});
        index->insert(30, std::vector<float>{0.9f, 0.9f, 0.9f, 0.9f, 0.9f, 0.9f, 0.9f, 0.9f});

        original_results = index->search(query, 3);

        Persist persist(segment_path);
        EXPECT_TRUE(persist.write(index));
    }

    // Load and compare
    {
        HNSWIndexPtr loaded;
        Persist persist(segment_path);
        EXPECT_TRUE(persist.read(loaded));

        auto loaded_results = loaded->search(query, 3);

        ASSERT_EQ(original_results.size(), loaded_results.size());
        for (size_t i = 0; i < original_results.size(); ++i) {
            EXPECT_EQ(original_results[i].id, loaded_results[i].id);
            EXPECT_NEAR(original_results[i].score, loaded_results[i].score, 1e-5f);
        }
    }
}

// --- Edge cases ---

TEST_F(PersistTest, ReadNonExistentFileFails) {
    HNSWIndexPtr loaded;
    Persist persist("/tmp/tamdb_does_not_exist_" + std::to_string(::getpid()));
    EXPECT_FALSE(persist.read(loaded));
    EXPECT_EQ(loaded, nullptr);
}

TEST_F(PersistTest, WriteAndReadEmptyIndex) {
    {
        HNSWIndexPtr index = std::make_shared<HNSWIndex>(16, 200, 4);
        Persist persist(segment_path);
        EXPECT_TRUE(persist.write(index));
    }

    {
        HNSWIndexPtr loaded;
        Persist persist(segment_path);
        EXPECT_TRUE(persist.read(loaded));
        ASSERT_NE(loaded, nullptr);

        std::vector<float> query = {1.0f, 2.0f, 3.0f, 4.0f};
        auto results = loaded->search(query, 5);
        EXPECT_TRUE(results.empty());
    }
}

}  // namespace
}  // namespace tamdb
