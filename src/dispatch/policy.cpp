#include "dispatch/policy.hpp"

#include <stdexcept>

namespace dispatch {
namespace {

class PriorityPolicy final : public DispatchPolicy {
public:
    std::string name() const override;
    std::optional<std::string> select(
        const warehouse::Order& order,
        const std::vector<warehouse::PickStation>& candidates) const override;
};

class StockPolicy final : public DispatchPolicy {
public:
    std::string name() const override;
    std::optional<std::string> select(
        const warehouse::Order& order,
        const std::vector<warehouse::PickStation>& candidates) const override;
};

}  // namespace

std::unique_ptr<DispatchPolicy> make_policy(const std::string& name) {
    if (name == "priority") {
        return std::make_unique<PriorityPolicy>();
    }
    if (name == "stock") {
        return std::make_unique<StockPolicy>();
    }
    throw std::invalid_argument("unknown dispatch policy: " + name);
}

std::string PriorityPolicy::name() const { return "priority"; }

std::optional<std::string> PriorityPolicy::select(
    const warehouse::Order& order,
    const std::vector<warehouse::PickStation>& candidates) const {
    (void)order;
    // 候选列表已按站点 ID 字典序升序排列，直接取第一个。
    if (candidates.empty()) {
        return std::nullopt;
    }
    return candidates.front().id;
}

std::string StockPolicy::name() const { return "stock"; }

std::optional<std::string> StockPolicy::select(
    const warehouse::Order& order,
    const std::vector<warehouse::PickStation>& candidates) const {
    (void)order;
    if (candidates.empty()) {
        return std::nullopt;
    }
    // 选择库存最多的候选站；并列时取站点 ID 字典序较小者。
    // 候选列表已按 ID 升序，因此仅在库存严格大于当前最优时才更新。
    const warehouse::PickStation* best = &candidates.front();
    for (const auto& candidate : candidates) {
        if (candidate.stock > best->stock) {
            best = &candidate;
        }
    }
    return best->id;
}

}  // namespace dispatch
