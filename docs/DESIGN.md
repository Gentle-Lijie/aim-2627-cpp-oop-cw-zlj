# Design and debugging record

## Class responsibilities

四个命名空间的职责边界与依赖方向（依赖只能自上而下，箭头方向即 include 方向）：

```
app（cli/application/contracts）        最外层：输入规范化、命令行入口
 └──> dispatch（policy/scheduler）      调度层：策略选择 + 派单/完成协调
        ├──> warehouse（domain）        领域层：订单、拣货站、库存与机器人状态
        └──> audit（trace）             审计层：成功事件按序记录
```

| 命名空间.类 | 职责 | 关键设计点 |
|---|---|---|
| `warehouse::Order` | 订单值对象：`id`、`sku`、`quantity`（一单一 SKU，不混装） | 纯数据，无行为 |
| `warehouse::PickStation` | 拣货站快照：`id`、`stock`、`robot_state`(idle/busy)、`current_order_id` | 一站对应一台机器人 |
| `warehouse::Inventory` | 站点注册（`add_station` 校验 ID 格式与库存非负，违规抛 `std::invalid_argument`）、按 ID 查询、全量列表、候选过滤（`available_for`）、原子派单（`reserve`）、完成（`finish`） | 内部用 `std::map<std::string, PickStation>`，遍历天然按站点 ID 字典序，候选列表无需二次排序；外部拿到的均为 `PickStation` 值拷贝，无法绕过 Inventory 改内部状态 |
| `dispatch::DispatchPolicy` | 抽象策略接口：`select(order, candidates) -> optional<station_id>` | 无状态；只读候选快照，**不触碰库存与机器人状态** |
| `dispatch::PriorityPolicy` / `StockPolicy` | 两种具体策略（见下节） | 在 `policy.cpp` 匿名命名空间中定义，仅经 `make_policy(name)` 工厂暴露 |
| `dispatch::Scheduler` | 派单协调：校验 → 取候选 → 策略选站 → 原子 `reserve` → 写 Trace；完成协调：精确站点匹配或按订单 ID 匹配唯一持有站 | 策略选择前**零状态修改**；持有 `Inventory&` 与 `Trace&` 引用，自身不拥有状态 |
| `audit::Trace` | 成功转移的按插入序记录：`DISPATCH O S` / `COMPLETE O S` | 只做记录与回放，不校验、不调度；失败路径永不追加 |
| `app` | `normalize_sku`（去首尾空白 + 转大写）、`valid_station_id`/`valid_order_id`（拒空串与首尾空白）、CLI 入口 | 仅做输入规范化与展示，不含任何库存/状态规则 |

单向依赖的硬约束：`warehouse/domain.hpp` 只包含 `<map> <optional> <string> <vector>` 标准头，不 include 任何 `dispatch/`、`audit/`、`app/` 或 ROS 头；领域层因此可被核心库三方（demo、public_tests、ROS 节点）独立复用。所有头文件均有完整 include guard，头文件与源文件中均无 `using namespace`。同一份 `aim_oop_core`（`src/` 下五个 .cpp）被 `core_demo`、`public_tests` 与 ROS 2 的 `warehouse_dispatcher`（经 `ros2_ws` CMake 直接编入）共同链接，ROS 节点内不存在第二套库存扣减或状态机逻辑。

## Strategy choice

两种策略输入相同：**已按站点 ID 字典序升序排列的合格候选快照**。

| 策略 | 选择规则 | 平局规则 |
|---|---|---|
| `priority` | 取候选列表第一个（字典序最小的合格站） | 无需平局规则，字典序全序 |
| `stock` | 取当前库存最多的候选站 | 库存并列最多时取站点 ID 字典序较小者 |

实现细节：`StockPolicy` 遍历时仅在库存**严格大于**当前最优时才替换（`candidate.stock > best->stock`）。由于候选列表已按 ID 升序，并列时先出现者（ID 较小）自然保留，平局规则由遍历顺序 + 严格大于两个不变量共同保证，无需额外比较。

同一初始状态选不同站点的对照示例（`boundary 7` 实测）：S1 库存 1、S2 库存 3，均 idle，订单 `O1 SKU-A 1`：

```
priority -> S1    （字典序最小，无视库存）
stock    -> S2    （库存 3 > 1）
```

stock 平局示例（`boundary 6` 实测）：S1=2、S2=2，均 idle，订单数量 1 → `stock` 选 `S1`（并列最多，ID 较小）。

## State transitions

| 操作情形 | 库存变化 | 机器人状态 | 当前订单 ID | Trace 变化 |
|---|---|---|---|---|
| 派单成功 | 选中站点减订单数量（结果 ≥ 0） | 选中站 idle → busy | 选中站记录该订单 ID | 追加 `DISPATCH O S` |
| 派单失败（非法数量/非法 ID/无候选） | 所有站点不变 | 所有站点不变 | 所有站点不变 | 不追加 |
| 完成成功 | 目标站不变（库存不恢复，货已拣出） | 目标站 busy → idle | 目标站清空 | 追加 `COMPLETE O S` |
| 重复完成 | 不变 | 不变 | 不变 | 不追加 |
| 错误完成（订单不存在/未被持有/不匹配） | 不变 | 不变 | 不变 | 不追加 |

原子性由 `Inventory::reserve` 的"先全部条件检查、后一次性提交三项更新（扣库存/置 busy/记订单）"保证；全局不变量"任意站点库存 ≥ 0"与"同一机器人同一时刻至多一张订单"由 `reserve` 的 idle + 库存前置检查共同维护。

`Scheduler::complete` 的两段匹配（题面 §1.1.1 "order-ID-based station matching"）：

1. 给定 `station_id` 是真实站点时，按三条件精确完成：站点存在 ∧ 机器人 busy ∧ 当前订单 ID 与请求完全一致（对应 `Inventory::finish`）；
2. 否则按订单 ID 在全部站点中找 busy 且持有该订单的站点，**匹配唯一**才完成。给定 ROS 节点以 `complete("O1","O1",…)` 调用时走此路径命中 S1。
   任一路径失败均填 `failure_reason` 且零副作用。

## Debugging evidence

**复现输入**（`src/debug/main.cpp` 固定场景，S1=3、S2=5，均 idle）：提交 01(数量 2) → 提交 02(数量 6) → 完成 01 → 提交 03(数量 1)，打印五字段。

**错误现象**（修复前版本 `git show <修复前commit>:src/debug/main.cpp` 编译运行实录）：

```
$ ./debug_before
S1 no -1 yes 0
```

第三字段 `-1`：订单 02 被拒绝后 S2 库存竟为负。

**根因**：旧 `submit` 在空闲站上**先执行 `item.second -= quantity` 再检查** `item.second < 0`：

```cpp
if (!busy_[station_id]) {
    item.second -= quantity;          // 先扣减（缺陷）
    if (item.second < 0) {
        return {false, "", "insufficient stock"};   // 后判失败：副作用已发生
    }
    ...
}
```

数量 6 落在 idle 的 S2（库存 5）上：先扣成 -1，再发现不足返回拒绝——拒绝路径污染了库存。旧代码还缺少数量合法性检查（`quantity <= 0` 时甚至会给库存做加法）。

**修复**：校验与修改严格两段（先全查、后全改），并补数量前置检查；类结构、公开方法与打印格式不动：

```cpp
if (quantity <= 0) {
    return {false, "", "invalid quantity"};         // 新增：非法数量零副作用
}
if (!busy_[station_id]) {
    if (item.second < quantity) {
        return {false, "", "insufficient stock"};   // 先判足，未扣减
    }
    item.second -= quantity;                        // 全部条件满足后才提交
    busy_[station_id] = true;
    orders_[station_id] = order_id;
    return {true, station_id, "accepted"};
}
```

**修复后输出**（`./build/debug_task` 实录）：

```
$ ./build/debug_task
S1 no 5 yes 0
```

S2 库存保持 5，S1 最终 3−2−1=0，与题面 §4.2.1 目标一致。

## 边界验证记录

以下记录来自独立验证程序 `docs/verify/boundary_checks.cpp`（复用仓库核心库，28 项断言全部 PASS）。复现命令：

```bash
$ c++ -std=c++17 -Wall -Wextra -I include docs/verify/boundary_checks.cpp \
      src/warehouse/domain.cpp src/dispatch/policy.cpp src/dispatch/scheduler.cpp \
      src/audit/trace.cpp src/app/cli.cpp -o /tmp/boundary_checks
$ /tmp/boundary_checks   # 末行输出 ALL BOUNDARY CHECKS PASSED
```

表中"实际"列为该程序真实输出（节选），核心库行为亦可由 `./build/core_demo` 与 `./build/public_tests` 交叉印证。

| # | 边界条件 | 输入 | 预期 | 实际 | 结论 |
|---|---|---|---|---|---|
| 1 | 库存恰好等于订单数量 | S1 库存 2；`ORDER O1 SKU-A 2` | 接受，S1 库存恰为 0，绝不为负 | `accepted station=S1`；实测 `S1 stock=0 robot=busy order=O1` | PASS |
| 2 | 库存不足 | S1 库存 2；`ORDER O1 SKU-A 3` | 拒绝；库存/状态/Trace 全不变 | `rejected (no available station)`；实测 `S1 stock=2 robot=idle order=-`，Trace 0 条 | PASS |
| 3 | 忙碌站点被跳过 | S1、S2 各库存 5；O1(4) 占 S1；O2(9) 超额被拒；O3(5) 再派 | O1→S1；O2 拒绝零副作用；O3 跳过 busy 的 S1 落到 S2，库存 5→0 | 实测 `S1 stock=1 busy O1 \| S2 stock=0 busy O3` | PASS |
| 4 | 重复完成 / 错误订单完成 | O1 在 S1 完成后再次 `COMPLETE O1`，以及 `COMPLETE O9` | 均失败，零副作用，Trace 不增 | 实测均 `no busy station holds order …`；Trace 恰为 `DISPATCH O1 S1`+`COMPLETE O1 S1` 两条；S1 idle、库存 2 未恢复 | PASS |
| 5 | 非法数量 / 非法标识 | `qty=0`、`qty=-2`、订单 ID `" "`、SKU `" "` | 全部拒绝，状态与 Trace 不变 | 实测 4 项全拒，S1 库存仍 3，Trace 0 条 | PASS |
| 6 | stock 策则平局 | S1=2、S2=2 均 idle；`stock` 策略派数量 1 | 并列最多 → 选 ID 较小的 S1 | 实测选中 `S1` | PASS |
| 7 | 完成后同站再派单 | S1 库存 2；O1(1)→完成→O2(1) | O2 仍由 S1 接受；库存不恢复，2−1−1=0 | 实测 `accepted station=S1`，`S1 stock=0` | PASS |

补充：`add_station` 负库存与首尾空白 ID 均抛 `std::invalid_argument`（实测 PASS）；`app::normalize_sku(" sku-a ") == "SKU-A"`、`valid_station_id("S1 ")`/`valid_order_id("")` 为假（实测 PASS，`public_tests` 亦覆盖）。
