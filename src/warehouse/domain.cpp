#include "warehouse/domain.hpp"

#include <cctype>
#include <stdexcept>

namespace warehouse {
namespace {

// 标识符合法性：非空且不含首尾空白字符（§1.2 标识与 SKU 规则）。
bool is_space(char value) {
    return std::isspace(static_cast<unsigned char>(value)) != 0;
}

bool valid_identifier(const std::string& id) {
    if (id.empty()) {
        return false;
    }
    return !is_space(id.front()) && !is_space(id.back());
}

}  // namespace

void Inventory::add_station(const std::string& station_id, int stock) {
    if (!valid_identifier(station_id)) {
        throw std::invalid_argument(
            "station id must be non-empty without leading/trailing "
            "whitespace: '" + station_id + "'");
    }
    if (stock < 0) {
        throw std::invalid_argument(
            "station stock must be a non-negative integer: " +
            std::to_string(stock));
    }
    if (stations_.find(station_id) != stations_.end()) {
        throw std::invalid_argument(
            "station already registered: " + station_id);
    }
    PickStation station;
    station.id = station_id;
    station.stock = stock;
    station.robot_state = RobotState::idle;
    station.current_order_id.clear();
    // std::map 按 key 字典序保存，站点清单天然有序。
    stations_.emplace(station_id, station);
}

std::optional<PickStation> Inventory::station(
    const std::string& station_id) const {
    const auto found = stations_.find(station_id);
    if (found == stations_.end()) {
        return std::nullopt;
    }
    return found->second;
}

std::vector<PickStation> Inventory::stations() const {
    std::vector<PickStation> snapshot;
    snapshot.reserve(stations_.size());
    for (const auto& entry : stations_) {
        snapshot.push_back(entry.second);
    }
    return snapshot;
}

std::vector<PickStation> Inventory::available_for(const Order& order) const {
    std::vector<PickStation> candidates;
    if (order.quantity <= 0) {
        // 数量非法的订单没有合法候选站。
        return candidates;
    }
    for (const auto& entry : stations_) {
        const PickStation& candidate = entry.second;
        if (candidate.robot_state == RobotState::idle &&
            candidate.stock >= order.quantity) {
            candidates.push_back(candidate);
        }
    }
    return candidates;
}

bool Inventory::reserve(const Order& order, const std::string& station_id) {
    // 先完成全部条件检查，再一次性提交状态转移（原子派单，失败无副作用）。
    if (order.quantity <= 0) {
        return false;
    }
    const auto found = stations_.find(station_id);
    if (found == stations_.end()) {
        return false;
    }
    PickStation& target = found->second;
    if (target.robot_state != RobotState::idle) {
        return false;
    }
    if (target.stock < order.quantity) {
        return false;
    }
    target.stock -= order.quantity;
    target.robot_state = RobotState::busy;
    target.current_order_id = order.id;
    return true;
}

bool Inventory::finish(const std::string& station_id,
                       const std::string& order_id) {
    // 完成条件：站点存在 + 机器人 busy + 当前订单 ID 完全一致。
    const auto found = stations_.find(station_id);
    if (found == stations_.end()) {
        return false;
    }
    PickStation& target = found->second;
    if (target.robot_state != RobotState::busy) {
        return false;
    }
    if (target.current_order_id != order_id) {
        return false;
    }
    // 库存不恢复（货物已被拣出），仅释放机器人并清空当前订单。
    target.robot_state = RobotState::idle;
    target.current_order_id.clear();
    return true;
}

}  // namespace warehouse
