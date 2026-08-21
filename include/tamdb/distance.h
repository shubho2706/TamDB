#pragma once

#include <cstddef>
#include <span>

namespace tamdb {

float cosine_similarity(std::span<const float> a, std::span<const float> b);
float l2_distance(std::span<const float> a, std::span<const float> b);
float dot_product(std::span<const float> a, std::span<const float> b);

} // namespace tamdb
