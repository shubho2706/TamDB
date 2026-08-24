#include "tamdb/distance.h"

#include <cmath>

namespace tamdb {

float cosine_similarity(std::span<const float> a, std::span<const float> b) {
    double dot = 0.0, norm_a = 0.0, norm_b = 0.0;

    for(size_t i = 0; i < a.size(); ++i) {
        dot += a[i] * b[i];
        norm_a += a[i] * a[i];
        norm_b += b[i] * b[i];
    }

    float denom = std::sqrt(norm_a) * std::sqrt(norm_b);
    if(denom == 0.0f)
        return 0.0f;
    return dot / denom;

}

float l2_distance(std::span<const float> a, std::span<const float> b) {
    double dist = 0.0;
    for(size_t i = 0; i < a.size(); ++i) {
        dist += (a[i] - b[i]) * (a[i] - b[i]);
    }

    return std::sqrt(dist);
}

float dot_product(std::span<const float> a, std::span<const float> b) {
    double dot = 0.0;
    for(size_t i = 0; i < a.size(); ++i) {
        dot += a[i] * b[i];
    }

    return dot;
}

} // namespace tamdb
