#include "dispatch/scheduler.hpp"

#include <cctype>
#include <optional>
#include <stdexcept>
#include <vector>

namespace dispatch {
namespace {

bool is_space(char value) {
    return std::isspace(static_cast<unsigned char>(value)) != 0;
}

// 标识符合法性：非空且不含首尾空白字符（§1.2 标识与 SKU 规则）。
bool valid_identifier(const std::string& id) {
    if (id.empty()) {
        return false;
    }
    return !is_space(id.front()) && !is_space(id.back());
}

}  // namespace

Scheduler::Scheduler(warehouse::Inventory& inventory,
                     std::unique_ptr<DispatchPolicy> policy,
                     audit::Trace& trace)
    : inventory_(inventory), policy_(std::move(policy)), trace_(trace) {
    if (!policy_) {
        throw std::invalid_argument("dispatch policy cannot be null");
    }
}

DispatchDecision Scheduler::submit(const warehouse::Order& order) {
    // 流程：先校验订单并取候选站（均不修改状态），再由策略选站，
    // 最后才执行原子扣减与状态更新；任何失败路径零副作用。
    DispatchDecision decision;
    decision.accepted = false;
    decision.order_id = order.id;

    if (order.quantity <= 0) {
        decision.reason = "invalid quantity";
        return decision;
    }
    if (!valid_identifier(order.id)) {
        decision.reason = "invalid order id";
        return decision;
    }
    if (!valid_identifier(order.sku)) {
        decision.reason = "invalid sku";
        return decision;
    }

    const std::vector<warehouse::PickStation> candidates =
        inventory_.available_for(order);
    const std::optional<std::string> selected =
        policy_->select(order, candidates);
    if (!selected.has_value()) {
        decision.reason = "no available station";
        return decision;
    }

    if (!inventory_.reserve(order, *selected)) {
        // 正常流程下候选站必定可预留；此分支为防御性兜底。
        decision.reason = "reserve rejected";
        return decision;
    }

    decision.accepted = true;
    decision.station_id = *selected;
    decision.reason = "accepted";
    trace_.append("DISPATCH " + order.id + " " + *selected);
    return decision;
}

std::vector<DispatchDecision> Scheduler::submit_all(
    const std::vector<warehouse::Order>& orders) {
    std::vector<DispatchDecision> decisions;
    decisions.reserve(orders.size());
    for (const auto& order : orders) {
        decisions.push_back(submit(order));
    }
    return decisions;
}

bool Scheduler::complete(const std::string& station_id,
                         const std::string& order_id,
                         std::string& failure_reason) {
    failure_reason.clear();

    // 路径 1：给出的 station_id 是真实站点时，按三条件精确完成
    // （站点存在 + 机器人 busy + 当前订单 ID 与请求完全一致）。
    if (inventory_.station(station_id).has_value() &&
        inventory_.finish(station_id, order_id)) {
        trace_.append("COMPLETE " + order_id + " " + station_id);
        return true;
    }

    // 路径 2：按订单 ID 匹配当前持有该订单的 busy 站点；
    // 仅当匹配结果唯一时才允许完成。
    std::optional<std::string> holder;
    for (const auto& candidate : inventory_.stations()) {
        if (candidate.robot_state == warehouse::RobotState::busy &&
            candidate.current_order_id == order_id) {
            if (holder.has_value()) {
                failure_reason = "order held by multiple stations";
                return false;
            }
            holder = candidate.id;
        }
    }
    if (holder.has_value() && inventory_.finish(*holder, order_id)) {
        trace_.append("COMPLETE " + order_id + " " + *holder);
        return true;
    }

    failure_reason = "no busy station holds order " + order_id;
    return false;
}

const warehouse::Inventory& Scheduler::inventory() const { return inventory_; }

const audit::Trace& Scheduler::trace() const { return trace_; }

}  // namespace dispatch
