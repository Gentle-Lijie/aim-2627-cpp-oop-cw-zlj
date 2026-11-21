// Q5 边界验证记录的程序化来源：逐条覆盖题面 1.5.4 列出的边界条件。
#include "app/cli.hpp"
#include "audit/trace.hpp"
#include "dispatch/policy.hpp"
#include "dispatch/scheduler.hpp"
#include "warehouse/domain.hpp"

#include <iostream>
#include <string>

namespace {

int failures = 0;

void check(bool condition, const std::string& label) {
    std::cout << (condition ? "PASS" : "FAIL") << "  " << label << '\n';
    if (!condition) {
        ++failures;
    }
}

const char* state_name(warehouse::RobotState state) {
    return state == warehouse::RobotState::idle ? "idle" : "busy";
}

std::string describe(const warehouse::Inventory& inventory,
                     const std::string& id) {
    const auto station = inventory.station(id);
    if (!station.has_value()) {
        return id + "=<missing>";
    }
    return id + " stock=" + std::to_string(station->stock) + " robot=" +
           state_name(station->robot_state) + " order=" +
           (station->current_order_id.empty() ? "-"
                                              : station->current_order_id);
}

std::size_t trace_size(const audit::Trace& trace) {
    return trace.events().size();
}

// 1) 库存恰好等于订单数量
void exact_stock() {
    std::cout << "\n[boundary 1] stock exactly equals order quantity\n";
    warehouse::Inventory inventory;
    inventory.add_station("S1", 2);
    audit::Trace trace;
    dispatch::Scheduler scheduler(
        inventory, dispatch::make_policy("priority"), trace);
    const auto decision = scheduler.submit({"O1", "SKU-A", 2});
    check(decision.accepted && decision.station_id == "S1",
          "order of qty=2 accepted by S1 with stock=2");
    check(inventory.station("S1")->stock == 0,
          "S1 stock drops to exactly 0 (never negative)");
    std::cout << "  state: " << describe(inventory, "S1") << '\n';
}

// 2) 库存不足：拒绝且零副作用
void insufficient_stock() {
    std::cout << "\n[boundary 2] insufficient stock\n";
    warehouse::Inventory inventory;
    inventory.add_station("S1", 2);
    audit::Trace trace;
    dispatch::Scheduler scheduler(
        inventory, dispatch::make_policy("priority"), trace);
    const auto decision = scheduler.submit({"O1", "SKU-A", 3});
    check(!decision.accepted, "order of qty=3 rejected by stock=2 station");
    check(decision.reason == "no available station",
          "failure reason: " + decision.reason);
    check(inventory.station("S1")->stock == 2 &&
              inventory.station("S1")->robot_state ==
                  warehouse::RobotState::idle,
          "S1 unchanged after rejection");
    check(trace_size(trace) == 0, "trace unchanged after rejection");
    std::cout << "  state: " << describe(inventory, "S1") << '\n';
}

// 3) 忙碌站点被跳过 + 失败派单后继续派单
void busy_skip_and_continue() {
    std::cout << "\n[boundary 3] busy station skipped, dispatch continues "
                 "after failure\n";
    warehouse::Inventory inventory;
    inventory.add_station("S1", 5);
    inventory.add_station("S2", 5);
    audit::Trace trace;
    dispatch::Scheduler scheduler(
        inventory, dispatch::make_policy("priority"), trace);
    check(scheduler.submit({"O1", "SKU-A", 4}).station_id == "S1",
          "first order takes S1 (S1 stock 5->1, busy)");
    check(scheduler.submit({"O2", "SKU-A", 9}).accepted == false,
          "oversized order rejected while S1 busy (S2 stock 5 < 9)");
    const auto after = scheduler.submit({"O3", "SKU-A", 5});
    check(after.accepted && after.station_id == "S2",
          "after a failed dispatch, next order still reaches idle S2");
    check(inventory.station("S2")->stock == 0, "S2 stock 5->0");
    std::cout << "  state: " << describe(inventory, "S1") << " | "
              << describe(inventory, "S2") << '\n';
}

// 4) 重复完成与错误完成
void completion_edges() {
    std::cout << "\n[boundary 4] duplicate / wrong / unknown completion\n";
    warehouse::Inventory inventory;
    inventory.add_station("S1", 3);
    audit::Trace trace;
    dispatch::Scheduler scheduler(
        inventory, dispatch::make_policy("priority"), trace);
    scheduler.submit({"O1", "SKU-A", 1});
    std::string failure;
    check(scheduler.complete("S1", "O1", failure), "first COMPLETE O1 ok");
    const bool second = scheduler.complete("S1", "O1", failure);
    check(!second, "duplicate completion rejected: " + failure);
    const bool wrong = scheduler.complete("S1", "O9", failure);
    check(!wrong, "wrong order id rejected: " + failure);
    const bool unknown = scheduler.complete("S9", "O1", failure);
    check(!unknown, "unknown station with no holder rejected: " + failure);
    check(trace_size(trace) == 2,
          "trace has exactly DISPATCH O1 S1 + COMPLETE O1 S1");
    check(inventory.station("S1")->stock == 2 &&
              inventory.station("S1")->robot_state ==
                  warehouse::RobotState::idle,
          "S1 idle with stock 2, nothing restored, no side effects");
    for (const auto& event : trace.events()) {
        std::cout << "  trace: " << event.text << '\n';
    }
}

// 5) 非法数量与非法标识
void invalid_inputs() {
    std::cout << "\n[boundary 5] invalid quantity / ids\n";
    warehouse::Inventory inventory;
    inventory.add_station("S1", 3);
    audit::Trace trace;
    dispatch::Scheduler scheduler(
        inventory, dispatch::make_policy("priority"), trace);
    check(!scheduler.submit({"O1", "SKU-A", 0}).accepted,
          "quantity 0 rejected");
    check(!scheduler.submit({"O2", "SKU-A", -2}).accepted,
          "negative quantity rejected");
    check(!scheduler.submit({" ", "SKU-A", 1}).accepted,
          "order id with edge whitespace rejected");
    check(!scheduler.submit({"O3", " ", 1}).accepted,
          "sku with edge whitespace rejected");
    check(trace_size(trace) == 0 && inventory.station("S1")->stock == 3,
          "no state or trace change for any invalid input");
}

// 6) stock 策略：最大库存与平局规则
void stock_policy_rules() {
    std::cout << "\n[boundary 6] stock policy: max stock and tie-break\n";
    warehouse::Inventory inventory;
    inventory.add_station("S1", 1);
    inventory.add_station("S2", 3);
    audit::Trace trace;
    dispatch::Scheduler scheduler(
        inventory, dispatch::make_policy("stock"), trace);
    check(scheduler.submit({"O1", "SKU-A", 1}).station_id == "S2",
          "stock policy picks S2 (3 > 1), not lexicographic S1");
    std::cout << "  state: " << describe(inventory, "S1") << " | "
              << describe(inventory, "S2") << '\n';

    warehouse::Inventory tied;
    tied.add_station("S1", 2);
    tied.add_station("S2", 2);
    audit::Trace tied_trace;
    dispatch::Scheduler tied_scheduler(
        tied, dispatch::make_policy("stock"), tied_trace);
    check(tied_scheduler.submit({"O1", "SKU-A", 1}).station_id == "S1",
          "stock tie (2 == 2) resolved to smaller id S1");
}

// 7) 策略差异对比示例（priority vs stock 同状态选不同站）
void policy_difference() {
    std::cout << "\n[boundary 7] same state, different policies\n";
    const warehouse::Order order{"O1", "SKU-A", 1};
    warehouse::Inventory for_priority;
    for_priority.add_station("S1", 1);
    for_priority.add_station("S2", 3);
    audit::Trace trace_p;
    dispatch::Scheduler priority_scheduler(
        for_priority, dispatch::make_policy("priority"), trace_p);
    warehouse::Inventory for_stock;
    for_stock.add_station("S1", 1);
    for_stock.add_station("S2", 3);
    audit::Trace trace_s;
    dispatch::Scheduler stock_scheduler(
        for_stock, dispatch::make_policy("stock"), trace_s);
    const auto by_priority = priority_scheduler.submit(order);
    const auto by_stock = stock_scheduler.submit(order);
    std::cout << "  priority -> " << by_priority.station_id << '\n'
              << "  stock    -> " << by_stock.station_id << '\n';
    check(by_priority.station_id == "S1" && by_stock.station_id == "S2",
          "priority picks S1 (lexicographic), stock picks S2 (most stock)");
}

// 8) add_station 参数校验与完成后再派单
void registration_and_redispatch() {
    std::cout << "\n[boundary 8] registration guards, redispatch after "
                 "completion\n";
    bool threw = false;
    try {
        warehouse::Inventory inventory;
        inventory.add_station("S1", -1);
    } catch (const std::invalid_argument&) {
        threw = true;
    }
    check(threw, "add_station with negative stock throws invalid_argument");
    threw = false;
    try {
        warehouse::Inventory inventory;
        inventory.add_station(" S1", 1);
    } catch (const std::invalid_argument&) {
        threw = true;
    }
    check(threw, "add_station with whitespace id throws invalid_argument");

    warehouse::Inventory inventory;
    inventory.add_station("S1", 2);
    audit::Trace trace;
    dispatch::Scheduler scheduler(
        inventory, dispatch::make_policy("priority"), trace);
    scheduler.submit({"O1", "SKU-A", 1});
    std::string failure;
    scheduler.complete("S1", "O1", failure);
    const auto again = scheduler.submit({"O2", "SKU-A", 1});
    check(again.accepted && again.station_id == "S1",
          "station accepts a new order after completion (stock not "
          "restored: 1 left)");
    check(inventory.station("S1")->stock == 0, "S1 stock 2-1-1 = 0");

    check(app::normalize_sku(" sku-a ") == "SKU-A", "normalize_sku trims+"
                                                    "uppercases");
    check(!app::valid_station_id("S1 ") && !app::valid_order_id(""),
          "id validators reject whitespace/empty");
}

}  // namespace

int main() {
    exact_stock();
    insufficient_stock();
    busy_skip_and_continue();
    completion_edges();
    invalid_inputs();
    stock_policy_rules();
    policy_difference();
    registration_and_redispatch();
    std::cout << "\n" << (failures == 0 ? "ALL BOUNDARY CHECKS PASSED"
                                        : "FAILURES: " +
                                              std::to_string(failures))
              << '\n';
    return failures == 0 ? 0 : 1;
}
