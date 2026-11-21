# Operation and verification

本文档面向后续维护者，给出从干净目录复现本仓库全部本地行为的命令与预期输出。
所有命令均从**仓库根目录**开始执行；`$` 为 shell 提示符，其余行为程序真实输出。

## Build

前置条件：任意支持 C++17 与 CMake ≥ 3.16 的工具链（macOS clang / Linux gcc 均可），核心库仅依赖 C++ 标准库。

```bash
$ cmake -S . -B build
$ cmake --build build --parallel 4
$ ctest --test-dir build --output-on-failure
```

预期 `ctest` 输出：

```
1/2 Test #1: public_tests .....................   Passed
2/2 Test #2: debug_task .......................   Passed

100% tests passed, 0 tests failed out of 2
```

一键脚本等价于上述三步：

```bash
$ tools/check.sh
```

结构检查（四命名空间/头源分离/依赖方向所需文件齐备）：

```bash
$ python3 tools/validate_structure.py
Structure OK
```

演示程序复现题面 §1.3 的五步场景（S1=3、S2=2、priority 策略）：

```bash
$ ./build/core_demo
AIM OOP CW2 1.0
policy=priority
[init]
  S1 stock=3 robot=idle order=-
  S2 stock=2 robot=idle order=-
[submit] ORDER O1 SKU-A 1 -> ACCEPT S1
  S1 stock=2 robot=busy order=O1
  S2 stock=2 robot=idle order=-
[submit] ORDER O2 SKU-A 1 -> ACCEPT S2
  S1 stock=2 robot=busy order=O1
  S2 stock=1 robot=busy order=O2
[submit] ORDER O3 SKU-A 2 -> REJECT (no available station)
  S1 stock=2 robot=busy order=O1
  S2 stock=1 robot=busy order=O2
[complete] COMPLETE O1 S1 -> ok (stock not restored)
  S1 stock=2 robot=idle order=-
  S2 stock=1 robot=busy order=O2
[submit] ORDER O4 SKU-A 3 -> REJECT (no available station)
  S1 stock=2 robot=idle order=-
  S2 stock=1 robot=busy order=O2
[trace]
  DISPATCH O1 S1
  DISPATCH O2 S2
  COMPLETE O1 S1
```

退出码为 0。Trace 仅有成功转移记录，与题面 §1.3 表逐行一致。

## Debug output

Q4 独立调试程序（与 Q1 核心库相互独立）：

```bash
$ ./build/debug_task
S1 no 5 yes 0
```

五个字段含义（题面 §4.2.2）：订单 01(数量 2) 分配到的站点 `S1`；订单 02(数量 6) 派单结果 `no`；被拒后 S2 剩余库存 `5`；完成 01 后订单 03(数量 1) 派单结果 `yes`；S1 最终库存 `0`。

修复前（缺陷版本，先扣减后校验）的同场景输出为：

```
S1 no -1 yes 0
```

即拒绝 02 时 S2 库存被错误扣成 `-1`。复现方法：`git show <修复前commit>:src/debug/main.cpp` 编译运行即可得到上式；根因分析与修复差异见 `docs/DESIGN.md` 的 Q4 调试证据一节。

## ROS 2 demo

ROS 2 部分要求 Ubuntu 24.04 + ROS 2 Jazzy（Base 安装）。`ros2_ws/` 下的 simulator/dispatcher 节点代码为课程给定，复用仓库根的 `aim_oop_core` 核心库（`ros2_ws/src/warehouse_dispatch/CMakeLists.txt` 直接编译 `src/` 五个源文件并包含 `include/`）。

```bash
$ source /opt/ros/jazzy/setup.bash
$ cd ros2_ws
$ colcon build --symlink-install
$ source install/setup.bash
$ ros2 launch warehouse_dispatch warehouse_demo.launch.py
```

> 验证说明：本仓库开发机为 macOS，无法安装 ROS 2 Jazzy，以上命令取自题面 §6.3 原文，
> 未在本机实录。作为替代验证，已将给定 `dispatcher_node.cpp` 的 `handle_event`
> 分支逻辑原样复刻到 `docs/verify/ros_dispatch_sim.cpp`（剥离 ROS 层，初始 S1=3、
> S2=2、priority），用同一六事件序列驱动核心库，得到与下表逐条一致的输出；
> 复现命令与真实输出见下；真实闭环请在 Ubuntu 24.04 + Jazzy 中按上述命令复现。

```bash
$ c++ -std=c++17 -Wall -Wextra -I include docs/verify/ros_dispatch_sim.cpp \
      src/warehouse/domain.cpp src/dispatch/policy.cpp src/dispatch/scheduler.cpp \
      src/audit/trace.cpp src/app/cli.cpp -o /tmp/ros_dispatch_sim
$ /tmp/ros_dispatch_sim
ACCEPT O1 S1
ACCEPT O2 S2
REJECT O3
COMPLETE O1
REJECT O4
WAITED 1
```

（`REJECT` 行在实际终端输出中行尾有一个空格，来自给定节点代码
`decision.order_id + " " + decision.station_id` 的拼接方式，被拒时 `station_id`
为空串；`ros2_ws/` 为给定不可修改代码，属骨架既定行为，上表按题面格式省略。）

## ROS 2 预期输出对照

`warehouse_demo.launch.py` 启动后，`/dispatch_results`（终端 `output='screen'` 同步打印）按序出现且仅出现以下六条：

| # | 输入事件（/warehouse_events） | 调度逻辑 | 预期输出（/dispatch_results） |
|---|------------------------------|----------|------------------------------|
| 1 | `ORDER O1 SKU-A 1` | S1(3,idle)、S2(2,idle) 均合格；priority 选字典序最小的 S1 | `ACCEPT O1 S1` |
| 2 | `ORDER O2 SKU-A 1` | S1 busy 被排除；仅 S2(2,idle) 合格 | `ACCEPT O2 S2` |
| 3 | `ORDER O3 SKU-A 2` | 两站均 busy，候选为空 | `REJECT O3` |
| 4 | `COMPLETE O1` | 匹配到持有 O1 且 busy 的 S1（按订单 ID 匹配站点） | `COMPLETE O1` |
| 5 | `ORDER O4 SKU-A 3` | S1 idle 但库存 2 < 3；S2 busy；候选为空 | `REJECT O4` |
| 6 | `WAIT 1` | 控制命令，不触发任何仓库状态变化 | `WAITED 1` |

六条输出后 simulator 与 dispatcher 均自动退出，`ros2 launch` 返回 shell 提示符，无残留节点进程。

实现依据（核心库侧）：事件 4 由 `dispatch::Scheduler::complete` 的"按订单 ID 匹配持有站"路径处理——给定节点以 `complete("O1", "O1", …)` 调用，`Scheduler` 先尝试精确站点（"O1" 非站点），再按订单 ID 找到唯一持有站 S1 完成任务，因此输出 `COMPLETE O1` 且 Trace 记录 `COMPLETE O1 S1`。

## Failure notes

故障排查记录（两条均为可复现的具体问题）：

1. **`ros2 launch`/`colcon` 报 command not found。**
   原因：当前 shell 未加载 ROS 2 环境。修复：先 `source /opt/ros/jazzy/setup.bash`，工作区构建后再 `source ros2_ws/install/setup.bash`；两个 setup 都要在**同一个 shell 会话**里 source，开新终端需重新执行。
2. **`ros2 launch` 结束后有节点残留（再次 launch 行为错乱）。**
   原因：simulator 在发完全部事件后第 7 次 timer 回调调用 `rclcpp::shutdown()`；若前一次演示异常中断（如 Ctrl+C 时机不当），`warehouse_simulator`/`warehouse_dispatcher` 可能仍在后台。修复：`ps aux | grep warehouse_` 检查并用 `kill <pid>` 清理后重新 launch；正常路径无需手动清理。
