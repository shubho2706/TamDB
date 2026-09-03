#include <iostream>
#include <vector>
#include <cstdlib>
#include <ctime>
#include <span>
#include <chrono>

#include "tamdb/hnsw/hnsw_index.h"
#include "tamdb/naive/flat_index.h"

int main() {
    const size_t dimensions = 128;
    const size_t num_vectors = 1000;
    const size_t top_k = 5;

    std::srand(std::time(nullptr));

    // Generate random vectors
    std::vector<std::vector<float>> vectors(num_vectors, std::vector<float>(dimensions));
    for (auto& vec : vectors) {
        for (auto& val : vec) {
            val = static_cast<float>(std::rand()) / RAND_MAX;
        }
    }

    // --- HNSW Index ---
    std::cout << "=== HNSW Index ===" << std::endl;
    tamdb::HNSWIndex hnsw(16, 200, dimensions);

    auto start = std::chrono::high_resolution_clock::now();
    for (size_t i = 0; i < num_vectors; ++i) {
        hnsw.insert(i, std::span<float>(vectors[i]));
    }
    auto end = std::chrono::high_resolution_clock::now();
    auto insert_ms = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
    std::cout << "Inserted " << num_vectors << " vectors in " << insert_ms << "ms" << std::endl;

    // Search with a random query
    std::vector<float> query(dimensions);
    for (auto& val : query) {
        val = static_cast<float>(std::rand()) / RAND_MAX;
    }

    start = std::chrono::high_resolution_clock::now();
    auto hnsw_results = hnsw.search(std::span<const float>(query), top_k);
    end = std::chrono::high_resolution_clock::now();
    auto search_us = std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();

    std::cout << "Search took " << search_us << "us" << std::endl;
    std::cout << "Top " << top_k << " results:" << std::endl;
    for (const auto& result : hnsw_results) {
        std::cout << "  id=" << result.id << " dist=" << result.score << std::endl;
    }

    // --- Flat Index ---
    std::cout << "\n=== Flat Index ===" << std::endl;
    tamdb::FlatIndex flat(dimensions);

    start = std::chrono::high_resolution_clock::now();
    for (size_t i = 0; i < num_vectors; ++i) {
        flat.insert(i, std::span<const float>(vectors[i]));
    }
    end = std::chrono::high_resolution_clock::now();
    insert_ms = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
    std::cout << "Inserted " << num_vectors << " vectors in " << insert_ms << "ms" << std::endl;

    start = std::chrono::high_resolution_clock::now();
    auto flat_results = flat.search(std::span<const float>(query), top_k);
    end = std::chrono::high_resolution_clock::now();
    search_us = std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();

    std::cout << "Search took " << search_us << "us" << std::endl;
    std::cout << "Top " << top_k << " results:" << std::endl;
    for (const auto& result : flat_results) {
        std::cout << "  id=" << result.id << " dist=" << result.score << std::endl;
    }

    // --- Recall check ---
    std::cout << "\n=== Recall ===" << std::endl;
    size_t matches = 0;
    for (const auto& hr : hnsw_results) {
        for (const auto& fr : flat_results) {
            if (hr.id == fr.id) {
                matches++;
                break;
            }
        }
    }
    std::cout << "Recall: " << matches << "/" << top_k
              << " (" << (100.0 * matches / top_k) << "%)" << std::endl;

    return 0;
}
