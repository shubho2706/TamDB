#include "tamdb/distance.h"

#include <gtest/gtest.h>

#include <cmath>
#include <vector>

namespace tamdb {
namespace {

// ============================================================================
// Dot Product Tests
// ============================================================================

TEST(DotProductTest, IdenticalVectors) {
    std::vector<float> a = {1.0f, 2.0f, 3.0f};
    float result = dot_product(a, a);
    EXPECT_FLOAT_EQ(result, 14.0f);  // 1*1 + 2*2 + 3*3 = 14
}

TEST(DotProductTest, OrthogonalVectors) {
    std::vector<float> a = {1.0f, 0.0f, 0.0f};
    std::vector<float> b = {0.0f, 1.0f, 0.0f};
    float result = dot_product(a, b);
    EXPECT_FLOAT_EQ(result, 0.0f);
}

TEST(DotProductTest, OppositeVectors) {
    std::vector<float> a = {1.0f, 2.0f, 3.0f};
    std::vector<float> b = {-1.0f, -2.0f, -3.0f};
    float result = dot_product(a, b);
    EXPECT_FLOAT_EQ(result, -14.0f);
}

TEST(DotProductTest, ZeroVector) {
    std::vector<float> a = {1.0f, 2.0f, 3.0f};
    std::vector<float> zero = {0.0f, 0.0f, 0.0f};
    float result = dot_product(a, zero);
    EXPECT_FLOAT_EQ(result, 0.0f);
}

TEST(DotProductTest, SingleDimension) {
    std::vector<float> a = {3.0f};
    std::vector<float> b = {4.0f};
    float result = dot_product(a, b);
    EXPECT_FLOAT_EQ(result, 12.0f);
}

TEST(DotProductTest, HighDimensional) {
    const size_t dim = 128;
    std::vector<float> a(dim, 1.0f);
    std::vector<float> b(dim, 2.0f);
    float result = dot_product(a, b);
    EXPECT_FLOAT_EQ(result, 256.0f);  // 128 * 1 * 2
}

// ============================================================================
// L2 Distance Tests
// ============================================================================

TEST(L2DistanceTest, IdenticalVectors) {
    std::vector<float> a = {1.0f, 2.0f, 3.0f};
    float result = l2_distance(a, a);
    EXPECT_FLOAT_EQ(result, 0.0f);
}

TEST(L2DistanceTest, KnownDistance) {
    std::vector<float> a = {0.0f, 0.0f, 0.0f};
    std::vector<float> b = {3.0f, 4.0f, 0.0f};
    float result = l2_distance(a, b);
    EXPECT_FLOAT_EQ(result, 5.0f);  // 3-4-5 triangle
}

TEST(L2DistanceTest, UnitVectors) {
    std::vector<float> a = {1.0f, 0.0f};
    std::vector<float> b = {0.0f, 1.0f};
    float result = l2_distance(a, b);
    EXPECT_NEAR(result, std::sqrt(2.0f), 1e-6f);
}

TEST(L2DistanceTest, Symmetry) {
    std::vector<float> a = {1.0f, 2.0f, 3.0f};
    std::vector<float> b = {4.0f, 5.0f, 6.0f};
    EXPECT_FLOAT_EQ(l2_distance(a, b), l2_distance(b, a));
}

TEST(L2DistanceTest, SingleDimension) {
    std::vector<float> a = {3.0f};
    std::vector<float> b = {7.0f};
    float result = l2_distance(a, b);
    EXPECT_FLOAT_EQ(result, 4.0f);
}

TEST(L2DistanceTest, NegativeValues) {
    std::vector<float> a = {-1.0f, -2.0f};
    std::vector<float> b = {1.0f, 2.0f};
    float result = l2_distance(a, b);
    // sqrt((2)^2 + (4)^2) = sqrt(4 + 16) = sqrt(20)
    EXPECT_NEAR(result, std::sqrt(20.0f), 1e-6f);
}

TEST(L2DistanceTest, TriangleInequality) {
    std::vector<float> a = {0.0f, 0.0f};
    std::vector<float> b = {1.0f, 1.0f};
    std::vector<float> c = {2.0f, 0.0f};
    float ab = l2_distance(a, b);
    float bc = l2_distance(b, c);
    float ac = l2_distance(a, c);
    EXPECT_LE(ac, ab + bc + 1e-6f);  // triangle inequality
}

// ============================================================================
// Cosine Similarity Tests
// ============================================================================

TEST(CosineSimilarityTest, IdenticalVectors) {
    std::vector<float> a = {1.0f, 2.0f, 3.0f};
    float result = cosine_similarity(a, a);
    EXPECT_NEAR(result, 1.0f, 1e-6f);
}

TEST(CosineSimilarityTest, OppositeVectors) {
    std::vector<float> a = {1.0f, 2.0f, 3.0f};
    std::vector<float> b = {-1.0f, -2.0f, -3.0f};
    float result = cosine_similarity(a, b);
    EXPECT_NEAR(result, -1.0f, 1e-6f);
}

TEST(CosineSimilarityTest, OrthogonalVectors) {
    std::vector<float> a = {1.0f, 0.0f};
    std::vector<float> b = {0.0f, 1.0f};
    float result = cosine_similarity(a, b);
    EXPECT_NEAR(result, 0.0f, 1e-6f);
}

TEST(CosineSimilarityTest, ScaledVectorsSameDirection) {
    std::vector<float> a = {1.0f, 2.0f, 3.0f};
    std::vector<float> b = {2.0f, 4.0f, 6.0f};  // 2*a
    float result = cosine_similarity(a, b);
    EXPECT_NEAR(result, 1.0f, 1e-6f);  // same direction, magnitude doesn't matter
}

TEST(CosineSimilarityTest, ZeroVectorReturnsZero) {
    std::vector<float> a = {1.0f, 2.0f, 3.0f};
    std::vector<float> zero = {0.0f, 0.0f, 0.0f};
    float result = cosine_similarity(a, zero);
    EXPECT_FLOAT_EQ(result, 0.0f);
}

TEST(CosineSimilarityTest, BothZeroVectorsReturnZero) {
    std::vector<float> zero = {0.0f, 0.0f, 0.0f};
    float result = cosine_similarity(zero, zero);
    EXPECT_FLOAT_EQ(result, 0.0f);
}

TEST(CosineSimilarityTest, Symmetry) {
    std::vector<float> a = {1.0f, 3.0f, 5.0f};
    std::vector<float> b = {2.0f, 4.0f, 6.0f};
    EXPECT_FLOAT_EQ(cosine_similarity(a, b), cosine_similarity(b, a));
}

TEST(CosineSimilarityTest, KnownAngle45Degrees) {
    // cos(45) = 1/sqrt(2) ~ 0.7071
    std::vector<float> a = {1.0f, 0.0f};
    std::vector<float> b = {1.0f, 1.0f};
    float result = cosine_similarity(a, b);
    EXPECT_NEAR(result, 1.0f / std::sqrt(2.0f), 1e-6f);
}

TEST(CosineSimilarityTest, RangeIsBetweenNegativeOneAndOne) {
    std::vector<float> a = {1.0f, -3.0f, 5.0f, -7.0f};
    std::vector<float> b = {-2.0f, 4.0f, -6.0f, 8.0f};
    float result = cosine_similarity(a, b);
    EXPECT_GE(result, -1.0f);
    EXPECT_LE(result, 1.0f);
}

}  // namespace
}  // namespace tamdb
