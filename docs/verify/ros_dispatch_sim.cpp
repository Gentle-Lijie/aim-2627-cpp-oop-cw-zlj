// 本地验证：复刻 ros2_ws/src/warehouse_dispatch/src/dispatcher_node.cpp
// 中 handle_event 的分支逻辑（剥离 ROS 层），用同样的六事件序列驱动
// 核心库，核对 /dispatch_results 输出。
#include "audit/trace.hpp"
#include "dispatch/policy.hpp"
#include "dispatch/scheduler.hpp"
#include "warehouse/domain.hpp"

#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace {

warehouse::Inventory make_inventory() {
    warehouse::Inventory inventory;
    inventory.add_station("S1", 3);
    inventory.add_station("S2", 2);
    return inventory;
}

class LocalDispatcher {
public:
    explicit LocalDispatcher(
        std::unique_ptr<dispatch::DispatchPolicy> dispatch_policy)
        : inventory_(make_inventory()),
          scheduler_(inventory_, std::move(dispatch_policy), trace_) {}

    void handle_event(const std::string& text) {
        std::istringstream input(text);
        std::vector<std::string> fields;
        std::string field;
        while (input >> field) {
            fields.push_back(field);
        }
        if (fields.size() == 2 && fields[0] == "COMPLETE") {
            std::string failure;
            const bool accepted = scheduler_.complete(
                fields[1], fields[1], failure);
            publish(accepted ? "COMPLETE" : "BAD_COMPLETE",
                    fields[1] + (accepted ? "" : " " + failure));
            return;
        }
        if (fields.size() == 2 && fields[0] == "WAIT") {
            publish("WAITED", fields[1]);
            return;
        }
        if (fields.size() != 4) {
            publish("BAD", text);
            return;
        }
        if (fields[0] == "ORDER") {
            int quantity = 0;
            try {
                quantity = std::stoi(fields[3]);
            } catch (const std::exception&) {
                publish("BAD", text);
                return;
            }
            const warehouse::Order order{fields[1], fields[2], quantity};
            const auto decision = scheduler_.submit(order);
            publish(decision.accepted ? "ACCEPT" : "REJECT",
                    decision.order_id + " " + decision.station_id);
        } else if (fields.size() == 4 && fields[0] == "COMPLETE") {
            std::string failure;
            const bool accepted = scheduler_.complete(
                fields[1], fields[2], failure);
            publish(accepted ? "COMPLETE" : "BAD_COMPLETE",
                    fields[2] + (accepted ? "" : " " + failure));
        } else {
            publish("BAD", text);
        }
    }

    void publish(const std::string& status, const std::string& detail) {
        std::cout << status << " " << detail << '\n';
    }

private:
    warehouse::Inventory inventory_;
    audit::Trace trace_;
    dispatch::Scheduler scheduler_;
};

}  // namespace

int main() {
    LocalDispatcher dispatcher(dispatch::make_policy("priority"));
    const std::string events[] = {
        "ORDER O1 SKU-A 1",
        "ORDER O2 SKU-A 1",
        "ORDER O3 SKU-A 2",
        "COMPLETE O1",
        "ORDER O4 SKU-A 3",
        "WAIT 1",
    };
    for (const auto& event : events) {
        dispatcher.handle_event(event);
    }
    return 0;
}
