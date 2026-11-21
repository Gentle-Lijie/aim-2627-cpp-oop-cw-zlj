#include "app/cli.hpp"
#include "app/contracts.hpp"
#include "audit/trace.hpp"
#include "dispatch/policy.hpp"
#include "dispatch/scheduler.hpp"
#include "warehouse/domain.hpp"

#include <iostream>
#include <string>

namespace {

const char* state_name(warehouse::RobotState state) {
    return state == warehouse::RobotState::idle ? "idle" : "busy";
}

void print_state(const warehouse::Inventory& inventory) {
    for (const auto& station : inventory.stations()) {
        std::cout << "  " << station.id << " stock=" << station.stock
                  << " robot=" << state_name(station.robot_state)
                  << " order="
                  << (station.current_order_id.empty()
                          ? "-"
                          : station.current_order_id)
                  << '\n';
    }
}

void run_scenario() {
    // 复现题面 §1.3：初始库存 S1=3, S2=2，两台机器人 idle，priority 策略。
    warehouse::Inventory inventory;
    inventory.add_station("S1", 3);
    inventory.add_station("S2", 2);
    audit::Trace trace;
    dispatch::Scheduler scheduler(
        inventory, dispatch::make_policy("priority"), trace);

    std::cout << "policy=priority\n";
    std::cout << "[init]\n";
    print_state(inventory);

    const warehouse::Order early_orders[] = {
        {"O1", app::normalize_sku(" sku-a "), 1},
        {"O2", "SKU-A", 1},
        {"O3", "SKU-A", 2},
    };
    for (const auto& order : early_orders) {
        const auto decision = scheduler.submit(order);
        std::cout << "[submit] ORDER " << decision.order_id << ' '
                  << order.sku << ' ' << order.quantity << " -> "
                  << (decision.accepted ? "ACCEPT" : "REJECT");
        if (decision.accepted) {
            std::cout << ' ' << decision.station_id;
        } else {
            std::cout << " (" << decision.reason << ")";
        }
        std::cout << '\n';
        print_state(inventory);
    }

    std::string failure;
    if (scheduler.complete("S1", "O1", failure)) {
        std::cout << "[complete] COMPLETE O1 S1 -> ok (stock not restored)\n";
    } else {
        std::cout << "[complete] COMPLETE O1 S1 -> failed: " << failure
                  << '\n';
    }
    print_state(inventory);

    const warehouse::Order late_orders[] = {
        {"O4", "SKU-A", 3},
    };
    for (const auto& order : late_orders) {
        const auto decision = scheduler.submit(order);
        std::cout << "[submit] ORDER " << decision.order_id << ' '
                  << order.sku << ' ' << order.quantity << " -> "
                  << (decision.accepted ? "ACCEPT" : "REJECT");
        if (decision.accepted) {
            std::cout << ' ' << decision.station_id;
        } else {
            std::cout << " (" << decision.reason << ")";
        }
        std::cout << '\n';
        print_state(inventory);
    }

    std::cout << "[trace]\n";
    for (const auto& event : trace.events()) {
        std::cout << "  " << event.text << '\n';
    }
}

}  // namespace

int main() {
    std::cout << app::version << '\n';
    run_scenario();
    return 0;
}
