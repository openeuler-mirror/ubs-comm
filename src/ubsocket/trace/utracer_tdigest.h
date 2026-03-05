/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026-2026. All rights reserved.
 * Description: Provide the utility for cli message, etc
 * Author:
 * Create: 2026-03-02
 * Note:
 * History: 2026-03-02
*/

#ifndef UTRACER_3RDPARTY_T_DIGEST_H
#define UTRACER_3RDPARTY_T_DIGEST_H

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <iterator>
#include <limits>
#include <utility>
#include <vector>
#include "utracer_def.h"

namespace Statistics {

enum class InsertResultCode {
    NO_NEED_COMPERSS,
    NEED_COMPERSS
};

class Centroid {
public:
    Centroid(double newMean, uint32_t newWeight) : mean(newMean), weight(newWeight) {}
    bool operator < (const Centroid &centroid) const
    {
        return this->mean < centroid.mean;
    }
    bool operator > (const Centroid &centroid) const
    {
        return this->mean > centroid.mean;
    }
    double GetMean() const
    {
        return mean;
    }
    uint32_t GetWeight() const
    {
        return weight;
    }

private:
    double mean;
    uint32_t weight;
};

class CentroidList {
public:
    explicit CentroidList(size_t size)
    {
        centroids.reserve(size);
        totalWeight = 0;
    }

    InsertResultCode Insert(double mean, uint32_t weight)
    {
        if (mean < 0 || mean > UINT32_MAX) {
            return InsertResultCode::NO_NEED_COMPERSS;
        }
        Centroid centroid(mean, weight);
        centroids.emplace_back(centroid);
        totalWeight += weight;
        if (centroids.size() < centroids.capacity()) {
            return InsertResultCode::NO_NEED_COMPERSS;
        }
        return InsertResultCode::NEED_COMPERSS;
    }
    
    void Reset()
    {
        centroids.clear();
        totalWeight = 0;
    }

    size_t GetCentroidCount() const
    {
        return centroids.size();
    }

    size_t GetTotalWeight() const
    {
        return totalWeight;
    }

    std::vector<Centroid>& GetAndSetCentroids()
    {
        return centroids;
    }

private:
    std::vector<Centroid> centroids;
    uint32_t totalWeight;
};

inline bool RelativelyEqual(double a, double b, double relEpsilon = 1e-8)
{
    double diff = fabs(a - b);
    if (diff < relEpsilon) return true;
    return diff / (fabs(a) + fabs(b)) < relEpsilon;
}

inline double ComputeNormalizer(double compression, double n)
{
    const uint32_t NN_NO21 = 21;
    if (RelativelyEqual(compression, 0)) {
        return 0;
    }
    return compression / (NN_NO4 * std::log(n / compression) + NN_NO21);
}

inline double QuantileToScale(double q, double normalizer)
{
    const double qMin = 1e-15;
    const double qMax = 1 - qMin;
    const double qMid = 0.5;
    
    if (q < qMin) {
        return (NN_NO2 * QuantileToScale(qMin, normalizer));
    } else if (q > qMax) {
        return (NN_NO2 * QuantileToScale(qMax, normalizer));
    }
    
    if (q <= qMid) {
        return log(NN_NO2 * q) * normalizer;
    } else {
        return -log(NN_NO2 * (1 - q)) * normalizer;
    }
}

inline double ScaleToQuantile(double k, double normalizer)
{
    if (RelativelyEqual(normalizer, 0)) {
        return 0;
    }
    if (k <= 0) {
        return exp(k / normalizer) / NN_NO2;
    } else {
        return 1 - exp(-k / normalizer) / NN_NO2;
    }
}

inline double Lerp(double a, double b, double t) noexcept
{
    return a + t * (b - a);
}

struct CompressionState {
    double k1 = 0;
    double nextQLimitWeight = 0;
    double weightSoFar = 0;
    double weightToAdd = 0;
    double meanToAdd = 0;
    const uint32_t newTotalWeight = 0;
    const double normalizer = 0;

    CompressionState(uint32_t totalWeight, double norm)
        : newTotalWeight(totalWeight), normalizer(norm)
    {
        k1 = QuantileToScale(NN_NO0, normalizer);
        nextQLimitWeight = newTotalWeight * ScaleToQuantile(k1 + NN_NO1, normalizer);
        weightSoFar = 0;
        weightToAdd = 0;
        meanToAdd = 0;
    }
    
    void InitializeFirstCentroid(const Centroid& first)
    {
        weightToAdd = first.GetWeight();
        meanToAdd = first.GetMean();
    }

    void UpdateQuantileLimit()
    {
        double camelBack = static_cast<double>(weightSoFar) / static_cast<double>(newTotalWeight);
        k1 = QuantileToScale(camelBack, normalizer);
        nextQLimitWeight = newTotalWeight * QuantileToScale(k1 + 1, normalizer);
    }
};

class Tdigest {
public:
    explicit Tdigest(size_t size)
        : one(size), two(size), buffer(NN_NO2 * size), active(&one),
          minValue(std::numeric_limits<double>::max()),
          maxValue(std::numeric_limits<double>::lowest()) {}

    void Insert(double value, uint32_t weight = 1)
    {
        auto insert_result = buffer.Insert(value, weight);
        if (insert_result == InsertResultCode::NEED_COMPERSS) {
            Merge();
        }
    }

    void Reset()
    {
        one.Reset();
        two.Reset();
        buffer.Reset();
        active = &one;
        minValue = std::numeric_limits<double>::max();
        maxValue = std::numeric_limits<double>::lowest();
    }

    void Merge()
    {
        std::vector<Centroid> &input = buffer.GetAndSetCentroids();
        if (input.empty() || NULL == active) {
            return;
        }
        PrepareAndSortData(input);
        UpdateMinMaxValues(input);
        auto &inactive = (&one == active) ? two : one;
        CompressData(input, inactive);
        CleanUpAndPrepareNextRound(input, inactive);
    }

    double Quantile(double p) const
    {
        if (p < 0 || p > NN_NO100) {
            return 0.0;
        }
        if (nullptr == active) {
            return 0.0;
        }
        if (active->GetAndSetCentroids().empty()) {
            return 0.0;
        }
        if (active->GetAndSetCentroids().size() == 1) {
            return (active->GetAndSetCentroids().front().GetMean());
        }
        uint32_t index = (p / NN_NO100) * active->GetTotalWeight();
        if (index < NN_NO1) {
            return minValue;
        }
        if (index > active->GetTotalWeight() - NN_NO1) {
            return maxValue;
        }
        const auto &first = active->GetAndSetCentroids().front();
        if (first.GetWeight() > NN_NO1 && index < (first.GetWeight() / NN_NO2)) {
            return (Lerp(minValue, first.GetMean(),
                static_cast<double>(index - NN_NO1) / (first.GetWeight() / NN_NO2 - NN_NO1)));
        }

        const auto &last = active->GetAndSetCentroids().back();
        if (last.GetWeight() > NN_NO1 && active->GetTotalWeight() - index <= last.GetWeight() / NN_NO2) {
            return (maxValue - static_cast<double>(active->GetTotalWeight() - index - NN_NO1) /
                    (last.GetWeight() / NN_NO2 - NN_NO1) * (maxValue - last.GetMean()));
        }

        uint32_t currentWeight = first.GetWeight() / 2;
        for (size_t i = 0; i < active->GetAndSetCentroids().size() - 1; i++) {
            const auto &left = active->GetAndSetCentroids()[i];
            const auto &right = active->GetAndSetCentroids()[i + 1];
            uint32_t segmentWeight = (left.GetWeight() + right.GetWeight()) / 2;
            if (currentWeight + segmentWeight > index) {
                uint32_t lower = index - currentWeight;
                uint32_t upper = currentWeight + segmentWeight - index;
                return (left.GetMean() * upper + right.GetMean() *lower) / (lower + upper);
            }
            currentWeight += segmentWeight;
        }
        return active->GetAndSetCentroids().back().GetMean();
    }

private:
    void PrepareAndSortData(std::vector<Centroid> &input)
    {
        if (forward) {
            std::sort(input.begin(), input.end(), std::less<Centroid>());
        } else {
            std::sort(input.begin(), input.end(), std::greater<Centroid>());
        }
    }
    void UpdateMinMaxValues(const std::vector<Centroid> &input)
    {
        if (forward) {
            UpdateMinMaxForward(input);
        } else {
            UpdateMinMaxBackward(input);
        }
    }
    void UpdateMinMaxForward(const std::vector<Centroid> &input)
    {
        minValue = std::min(minValue,
            input.front().GetWeight() == NN_NO1 ? input.front().GetMean() :
            std::numeric_limits<double>::max());
        maxValue = std::max(maxValue,
            input.back().GetWeight() == NN_NO1 ? input.back().GetMean() :
            std::numeric_limits<double>::min());
    }
    void UpdateMinMaxBackward(const std::vector<Centroid> &input)
    {
        minValue = std::min(minValue,
            input.back().GetWeight() == NN_NO1 ? input.back().GetMean() :
            std::numeric_limits<double>::max());
        maxValue = std::max(maxValue,
            input.front().GetWeight() == NN_NO1 ? input.front().GetMean() :
            std::numeric_limits<double>::min());
    }
    void CompressData(const std::vector<Centroid> &input, CentroidList &inactive)
    {
        const uint32_t newTotalWeight = buffer.GetTotalWeight() + active->GetTotalWeight();
        const double normalizer  = ComputeNormalizer(inactive.GetAndSetCentroids().capacity(), newTotalWeight);
        CompressionState state(newTotalWeight, normalizer);
        state.InitializeFirstCentroid(input.front());
        for (auto it = input.begin() + 1; it != input.end(); ++it) {
            ProcessCentroid(*it, state, inactive);
        }
        if (!RelativelyEqual(state.weightToAdd, 0)) {
            if (std::is_integral<double>::value) {
                state.meanToAdd = std::round(state.meanToAdd);
            }
            inactive.Insert(state.meanToAdd, state.weightToAdd);
        }
    }
    void CleanUpAndPrepareNextRound(std::vector<Centroid> &input, CentroidList &inactive)
    {
        if (!forward) {
            std::sort(inactive.GetAndSetCentroids().begin(), inactive.GetAndSetCentroids().end());
        }
        forward = !forward;
        buffer.Reset();
        input.assign(inactive.GetAndSetCentroids().begin(), inactive.GetAndSetCentroids().end());
        auto newInactive = active;
        active = &inactive;
        newInactive->Reset();
    }
    void ProcessCentroid(const Centroid &current, CompressionState &state, CentroidList &inactive)
    {
        if ((state.weightSoFar + state.weightToAdd + current.GetWeight()) <= state.nextQLimitWeight) {
            state.weightToAdd += current.GetWeight();
            state.meanToAdd = state.meanToAdd +
                (current.GetMean() - state.meanToAdd) * current.GetWeight() / state.weightToAdd;
        } else {
            state.weightSoFar += state.weightToAdd;
            state.UpdateQuantileLimit();
            if (std::is_integral<double>::value) {
                state.meanToAdd = std::round(state.meanToAdd);
            }
            inactive.Insert(state.meanToAdd, state.weightToAdd);
            state.meanToAdd = current.GetMean();
            state.weightToAdd = current.GetWeight();
        }
    }

private:
    CentroidList one;
    CentroidList two;
    CentroidList buffer;
    CentroidList *active;
    double minValue;
    double maxValue;
    bool forward = true;
};

}

#endif  // UTRACER_3RDPARTY_T_DIGEST_H