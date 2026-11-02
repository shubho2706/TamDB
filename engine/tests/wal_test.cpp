#include "tamdb/wal/wal.h"
#include "tamdb/hnsw/hnsw_index.h"

#include <gtest/gtest.h>

#include <cstdio>
#include <filesystem>
#include <vector>
#include <span>
#include <thread>
#include <mutex>

namespace tamdb {
namespace {

// Helper: unique temp file path per test, auto-cleaned
class WALTest : public ::testing::Test {
protected:
    std::string wal_path;

    void SetUp() override {
        wal_path = "/tmp/tamdb_wal_test_" + std::to_string(::getpid());
        // Clean up any leftover from a previous crashed run
        std::remove(wal_path.c_str());
    }

    void TearDown() override {
        std::remove(wal_path.c_str());
    }

    HNSWIndexPtr make_index(size_t dims = 4) {
        return std::make_shared<HNSWIndex>(16, 200, dims);
    }
};

// --- Write ---

TEST_F(WALTest, WriteSingleEntry) {
    WriteAheadLogger wal(wal_path);
    std::vector<float> vec = {1.0f, 2.0f, 3.0f, 4.0f};
    EXPECT_TRUE(wal.write(42, vec));
}

TEST_F(WALTest, WriteMultipleEntries) {
    WriteAheadLogger wal(wal_path);
    for (uint64_t i = 0; i < 10; ++i) {
        std::vector<float> vec = {float(i), float(i + 1), float(i + 2), float(i + 3)};
        EXPECT_TRUE(wal.write(i, vec));
    }
}

// --- Replay ---

TEST_F(WALTest, ReplaySingleEntry) {
    {
        WriteAheadLogger wal(wal_path);
        std::vector<float> vec = {1.0f, 2.0f, 3.0f, 4.0f};
        wal.write(42, vec);
    } // WAL destroyed, fd closed

    auto index = make_index();
    WriteAheadLogger wal(wal_path);
    EXPECT_TRUE(wal.replay(index));

    // Search for the inserted vector
    std::vector<float> query = {1.0f, 2.0f, 3.0f, 4.0f};
    auto results = index->search(query, 1);
    ASSERT_EQ(results.size(), 1u);
    EXPECT_EQ(results[0].id, 42u);
    EXPECT_NEAR(results[0].score, 0.0f, 1e-5f);
}

TEST_F(WALTest, ReplayMultipleEntries) {
    {
        WriteAheadLogger wal(wal_path);
        for (uint64_t i = 0; i < 5; ++i) {
            std::vector<float> vec = {float(i), 0.0f, 0.0f, 0.0f};
            wal.write(i, vec);
        }
    }

    auto index = make_index();
    WriteAheadLogger wal(wal_path);
    EXPECT_TRUE(wal.replay(index));

    // Search for vector closest to {3.0, 0, 0, 0} → should be id=3
    std::vector<float> query = {3.0f, 0.0f, 0.0f, 0.0f};
    auto results = index->search(query, 1);
    ASSERT_EQ(results.size(), 1u);
    EXPECT_EQ(results[0].id, 3u);
    EXPECT_NEAR(results[0].score, 0.0f, 1e-5f);
}

TEST_F(WALTest, ReplayEmptyFileSucceeds) {
    {
        WriteAheadLogger wal(wal_path); // creates the file
    }

    auto index = make_index();
    WriteAheadLogger wal(wal_path);
    EXPECT_TRUE(wal.replay(index));

    // Index should be empty
    std::vector<float> query = {1.0f, 2.0f, 3.0f, 4.0f};
    auto results = index->search(query, 1);
    EXPECT_TRUE(results.empty());
}

TEST_F(WALTest, ReplayPreservesInsertOrder) {
    {
        WriteAheadLogger wal(wal_path);
        // Insert 3 vectors at increasing distances from origin
        wal.write(10, std::vector<float>{1.0f, 0.0f, 0.0f, 0.0f});
        wal.write(20, std::vector<float>{2.0f, 0.0f, 0.0f, 0.0f});
        wal.write(30, std::vector<float>{3.0f, 0.0f, 0.0f, 0.0f});
    }

    auto index = make_index();
    WriteAheadLogger wal(wal_path);
    wal.replay(index);

    std::vector<float> query = {0.0f, 0.0f, 0.0f, 0.0f};
    auto results = index->search(query, 3);

    ASSERT_EQ(results.size(), 3u);
    EXPECT_EQ(results[0].id, 10u); // closest
    EXPECT_EQ(results[1].id, 20u);
    EXPECT_EQ(results[2].id, 30u); // farthest
}

// --- Write then replay simulates crash recovery ---

TEST_F(WALTest, CrashRecoverySimulation) {
    // "Session 1": insert vectors, then "crash" (destroy WAL + index)
    {
        WriteAheadLogger wal(wal_path);
        wal.write(1, std::vector<float>{0.1f, 0.2f, 0.3f, 0.4f});
        wal.write(2, std::vector<float>{0.5f, 0.6f, 0.7f, 0.8f});
        wal.write(3, std::vector<float>{0.9f, 1.0f, 1.1f, 1.2f});
    }

    // "Session 2": fresh index, replay WAL
    auto index = make_index();
    WriteAheadLogger wal(wal_path);
    EXPECT_TRUE(wal.replay(index));

    // All 3 vectors should be searchable
    std::vector<float> query = {0.5f, 0.6f, 0.7f, 0.8f};
    auto results = index->search(query, 3);
    ASSERT_EQ(results.size(), 3u);
    EXPECT_EQ(results[0].id, 2u); // exact match
    EXPECT_NEAR(results[0].score, 0.0f, 1e-5f);
}

// --- File correctness ---

TEST_F(WALTest, FileSizeMatchesExpected) {
    {
        WriteAheadLogger wal(wal_path);
        std::vector<float> vec = {1.0f, 2.0f, 3.0f, 4.0f};
        wal.write(1, vec);
        wal.write(2, vec);
    }

    // Each entry: 4 (size) + 8 (id) + 16 (4 floats) = 28 bytes
    auto file_size = std::filesystem::file_size(wal_path);
    EXPECT_EQ(file_size, 2 * 28u);
}


// --- Concurrent and advanced replay ---

TEST_F(WALTest, ConcurrentWrites) {
    constexpr int kThreads   = 4;
    constexpr int kPerThread = 25;

    {
        std::mutex wal_mutex;
        WriteAheadLogger wal(wal_path);
        std::vector<std::thread> threads;
        threads.reserve(kThreads);

        for (int t = 0; t < kThreads; ++t) {
            threads.emplace_back([&wal, &wal_mutex, t]() {
                for (int i = 0; i < kPerThread; ++i) {
                    uint64_t id = static_cast<uint64_t>(t * kPerThread + i);
                    std::vector<float> vec = {float(id), 0.0f, 0.0f, 0.0f};
                    std::lock_guard<std::mutex> lock(wal_mutex);
                    wal.write(id, vec);
                }
            });
        }
        for (auto& th : threads) th.join();
    } // WAL destroyed, fd closed

    auto index = make_index();
    WriteAheadLogger wal(wal_path);
    EXPECT_TRUE(wal.replay(index));

    IndexMetadata md = index->getIndexMetadata();
    EXPECT_EQ(md.node_count, static_cast<uint64_t>(kThreads * kPerThread));
}

TEST_F(WALTest, VariableDimensionEntries) {
    // Verifies that the WAL size field correctly records floats per entry
    {
        WriteAheadLogger wal(wal_path);
        wal.write(1, std::vector<float>{1.0f, 0.0f, 0.0f, 0.0f});
        wal.write(2, std::vector<float>{0.0f, 1.0f, 0.0f, 0.0f});
        wal.write(3, std::vector<float>{0.0f, 0.0f, 1.0f, 0.0f});
    }

    auto index = make_index(4);
    WriteAheadLogger wal(wal_path);
    EXPECT_TRUE(wal.replay(index));

    std::vector<float> query = {1.0f, 0.0f, 0.0f, 0.0f};
    auto results = index->search(query, 1);
    ASSERT_EQ(results.size(), 1u);
    EXPECT_EQ(results[0].id, 1u);
    EXPECT_NEAR(results[0].score, 0.0f, 1e-5f);
}

TEST_F(WALTest, ReplayIntoNonEmptyIndex) {
    // Write 3 WAL entries
    {
        WriteAheadLogger wal(wal_path);
        wal.write(4, std::vector<float>{4.0f, 0.0f, 0.0f, 0.0f});
        wal.write(5, std::vector<float>{5.0f, 0.0f, 0.0f, 0.0f});
        wal.write(6, std::vector<float>{6.0f, 0.0f, 0.0f, 0.0f});
    }

    // Index already contains 3 directly inserted vectors
    auto index = make_index();
    index->insert(1, std::vector<float>{1.0f, 0.0f, 0.0f, 0.0f});
    index->insert(2, std::vector<float>{2.0f, 0.0f, 0.0f, 0.0f});
    index->insert(3, std::vector<float>{3.0f, 0.0f, 0.0f, 0.0f});

    WriteAheadLogger wal(wal_path);
    EXPECT_TRUE(wal.replay(index));

    // Index should now hold all 6 vectors
    IndexMetadata md = index->getIndexMetadata();
    EXPECT_EQ(md.node_count, 6u);
}

TEST_F(WALTest, LargeWAL) {
    constexpr size_t kN = 1000;

    {
        WriteAheadLogger wal(wal_path);
        for (uint64_t i = 0; i < kN; ++i) {
            std::vector<float> vec = {float(i), 0.0f, 0.0f, 0.0f};
            wal.write(i, vec);
        }
    }

    auto index = make_index();
    WriteAheadLogger wal(wal_path);
    EXPECT_TRUE(wal.replay(index));

    IndexMetadata md = index->getIndexMetadata();
    EXPECT_EQ(md.node_count, kN);
}

}  // namespace
}  // namespace tamdb
