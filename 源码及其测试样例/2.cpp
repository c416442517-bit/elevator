#include <iostream>
#include <vector>
#include <queue>
#include <stack>
#include <algorithm>
#include <cmath>
#include <climits>
#include <fstream>
#include <string>
#include <sstream>
#include <filesystem>

using namespace std;
namespace fs = std::filesystem;

// ======================= 全局可配置常量 =======================
int g_minFloor = 1;         // 最低楼层（用户输入）
int g_maxFloor = 10;        // 最高楼层（用户输入）

// 紧急模式类型枚举（增加地震）
enum EmergType { FIRE, FAULT, FLOOD, EARTHQUAKE };

// 电梯运行方向常量
const int DIR_UP = 1;       // 上行
const int DIR_DOWN = -1;    // 下行
const int DIR_STOP = 0;     // 停止/空闲

// 全局变量：电梯数量（运行时由用户输入，支持2~6部）
int g_elevatorNum = 0;

// 全局紧急状态标志（紧急模式下禁止新请求）
bool g_emergencyActive = false;

// ======================= 数据结构定义 =======================

/**
 * 外部呼叫请求结构体
 * 当乘客在某一楼层按下上行/下行按钮时产生
 * 动态分配版本新增字段：assignedElevator（当前分配的电梯编号，-1表示未分配）
 *                    completed（该请求是否已完成服务）
 */
struct ExternalReq {
    int floor;               // 请求所在楼层
    int dir;                 // 请求方向：DIR_UP 或 DIR_DOWN
    int assignedElevator;    // 动态分配：当前负责该请求的电梯编号（-1为未分配）
    bool completed;          // 动态分配：服务是否已完成（完成后将被清除）
};

/**
 * 乘客信息：仅记录目标楼层（体重由电梯自动按平均值计算）
 */
struct Passenger {
    int target;      // 目标楼层
};

/**
 * 电梯类：封装单部电梯的所有属性和行为
 * 动态分配版本：任务列表中的负数表示外部接客任务，正数为内部送客任务
 */
class Elevator {
public:
    int id;                 // 电梯编号（0开始，显示时+1）
    int curFloor;           // 当前所在楼层
    int direction;          // 当前运动方向（上/下/停）
    bool isOpen;            // 门状态：true开，false关
    vector<int> tasks;      // 任务列表：需要停靠的楼层（正数=内部送客，负数=-外部接客楼层）

    // 载重相关
    static constexpr double MAX_LOAD = 1000.0;        // 最大承重(kg)，可根据需要修改
    static constexpr double AVG_WEIGHT_PER_PERSON = 70.0;  // 人均体重(kg)，模拟传感器
    vector<Passenger> passengers;                     // 电梯内乘客（仅目标楼层）
    double currentLoad;                               // 当前总载重(kg)（人数 × 人均体重）

    /**
     * 构造函数：初始化电梯，默认停在1层（若1不可达则停在最低层），门关，无任务
     * @param i 电梯编号（从0开始）
     */
    Elevator(int i) : id(i), direction(DIR_STOP), isOpen(false), currentLoad(0.0) {
        if (1 >= g_minFloor && 1 <= g_maxFloor)
            curFloor = 1;
        else
            curFloor = g_minFloor;
    }

    // ---------- 乘客管理方法 ----------
    /** 是否有乘客 */
    bool hasPassenger() const { return !passengers.empty(); }

    /** 当前载客人数 */
    int passengerCount() const { return (int)passengers.size(); }

    /** 判断电梯是否已满载（无法再容纳至少一名标准体重乘客） */
    bool isFull() const {
        return currentLoad + AVG_WEIGHT_PER_PERSON > MAX_LOAD;
    }

    /**
     * 尝试让一名乘客上梯（自动按人均体重计算载重增量）
     * @param targetFloor 乘客目标楼层
     * @return 上梯是否成功（未超重且楼层合法）
     */
    bool boardPassenger(int targetFloor) {
        if (currentLoad + AVG_WEIGHT_PER_PERSON > MAX_LOAD) {
            cout << "电梯 " << id+1 << " 超重！当前载重 " << currentLoad 
                 << " kg（约 " << passengerCount() << " 人），上限 " << MAX_LOAD << " kg，无法上客。\n";
            return false;
        }
        passengers.push_back({targetFloor});
        currentLoad += AVG_WEIGHT_PER_PERSON;   // 传感器自动增加平均体重
        addTask(targetFloor);                    // 自动加入送客任务（正数楼层）
        return true;
    }

    /**
     * 在指定楼层下客（所有目标为该楼层的乘客下车，按人均体重减重）
     * @param floor 当前楼层
     */
    void alightPassengersAt(int floor) {
        auto it = remove_if(passengers.begin(), passengers.end(),
                            [floor](const Passenger& p) { return p.target == floor; });
        if (it != passengers.end()) {
            int alightedCount = (int)(passengers.end() - it);
            passengers.erase(it, passengers.end());
            double reducedWeight = alightedCount * AVG_WEIGHT_PER_PERSON;
            currentLoad -= reducedWeight;
            cout << "电梯 " << id+1 << " 在 " << floor << " 层下客 " << alightedCount 
                 << " 人，减少载重 " << reducedWeight << " kg，当前载重 " << currentLoad << " kg\n";
        }
    }

    /** 清空所有乘客（紧急疏散时使用） */
    void clearPassengers() {
        passengers.clear();
        currentLoad = 0.0;
    }

    /**
     * 添加一个新任务（楼层），并重新规划任务顺序以提高效率
     * 如果请求楼层等于当前楼层，则直接开门完成服务，不加入任务列表
     * @param floor      目标楼层（正数=内部请求，负数=外部请求）
     * @param isExternal 是否为外部请求（用于将楼层转为负数存储）
     */
    void addTask(int floor, bool isExternal = false) {
        int taskFloor = isExternal ? -floor : floor;   // 外部请求存储为负数，内部请求保持正数
        if (taskFloor == curFloor || -taskFloor == curFloor) {
            // 同层请求：直接开门完成
            if (!isOpen) {
                isOpen = true;
                cout << "电梯 " << id+1 << " 在 " << curFloor << " 层已开门（同层响应）\n";
                // 外部请求会在 moveOneStep 中标记完成，此处仅模拟开门
                isOpen = false;
                cout << "电梯 " << id+1 << " 关门（服务完成）\n";
            }
            return;
        }
        tasks.push_back(taskFloor);
        optimizeTasks();    // 每次添加后优化顺序
    }

    /**
     * 专门添加外部请求任务（传入正数楼层，内部转为负数）
     */
    void addExternalTask(int floor) {
        addTask(floor, true);
    }

    /**
     * 优化任务列表顺序：根据当前楼层和方向，将同向任务优先执行
     * 采用简化的LOOK算法：先处理与当前方向同向的任务，再反向
     * 动态分配版本：任务可能为负数（外部请求），按绝对值处理
     */
    void optimizeTasks() {
        if (tasks.empty()) return;

        // 先按绝对值升序排序
        sort(tasks.begin(), tasks.end(), [](int a, int b) {
            return abs(a) < abs(b);
        });

        // 将任务分为上行任务（绝对值大于当前楼层）和下行任务（绝对值小于当前楼层）
        vector<int> up, down;
        for (int f : tasks) {
            int absF = abs(f);
            if (absF > curFloor) up.push_back(f);
            else if (absF < curFloor) down.push_back(f);
            // 等于当前楼层的任务忽略（已在当前层，后续到达时会立即处理）
        }

        // 分别排序：上行升序（按绝对值），下行降序（按绝对值）
        sort(up.begin(), up.end(), [](int a, int b) { return abs(a) < abs(b); });
        sort(down.begin(), down.end(), [](int a, int b) { return abs(a) > abs(b); });

        tasks.clear();

        // 根据当前方向决定任务顺序
        if (direction == DIR_UP || (direction == DIR_STOP && !up.empty())) {
            for (int f : up) tasks.push_back(f);    // 先上
            for (int f : down) tasks.push_back(f);  // 后下
        } else {
            for (int f : down) tasks.push_back(f);
            for (int f : up) tasks.push_back(f);
        }
    }

    /**
     * 移除第一个任务（到达该楼层后调用）
     */
    void popTask() {
        if (!tasks.empty()) tasks.erase(tasks.begin());
    }

    /**
     * 获取下一个目标楼层（任务列表的第一个，取绝对值）
     * @return 目标楼层，若无任务返回-1
     */
    int getNextTarget() {
        return tasks.empty() ? -1 : abs(tasks[0]);
    }

    /**
     * 移除指定的外部任务（根据原始楼层，移除对应的负数任务）
     * 用于动态重分配时，将请求从旧电梯中撤走
     * @param floor 正数的请求楼层
     */
    void removeExternalTask(int floor) {
        int target = -floor;   // 任务列表中存储的是负数
        auto it = find(tasks.begin(), tasks.end(), target);
        if (it != tasks.end()) {
            tasks.erase(it);
            optimizeTasks();   // 移除后重新优化顺序
        }
    }
};

// ======================= 全局变量 =======================
vector<Elevator> elevators;                 // 所有电梯对象

/**
 * 动态分配版本：外部队列改为持久化列表，支持重分配
 * 每个外部请求一旦创建就保存在此，直到服务完成才移除
 */
vector<ExternalReq> pendingRequests;        // 所有未完成的外部请求

stack<vector<int>> emergencyBackup;        // 紧急模式时备份各电梯任务列表（栈）

// ======================= 工具函数 =======================

/**
 * 检查楼层是否在合法范围内
 * @param floor 待检查楼层
 * @return true 合法，false 非法
 */
bool isValidFloor(int floor) {
    return floor >= g_minFloor && floor <= g_maxFloor;
}

// ======================= 核心算法函数 =======================

/**
 * 选择最佳电梯响应外部请求（最短距离 + 方向惩罚）
 * 与静态分配版本相同，但会在动态调度中反复调用
 * @param floor 请求楼层
 * @param dir   请求方向（上/下）
 * @return      最佳电梯的索引（0~g_elevatorNum-1），若失败返回-1
 */
int chooseBestElevator(int floor, int dir) {
    if (g_emergencyActive) return -1;   // 紧急模式下禁止分配
    int bestIdx = -1;
    int bestCost = INT_MAX;
    for (size_t i = 0; i < elevators.size(); ++i) {
        Elevator &e = elevators[i];
        if (e.isFull()) continue;       // 满载电梯跳过

        int cost = abs(e.curFloor - floor);
        // 方向惩罚：如果电梯当前运行方向与请求方向相反且请求楼层在电梯后方，则代价大幅增加
        if (e.direction != DIR_STOP) {
            if ((dir == DIR_UP && e.direction == DIR_DOWN && floor < e.curFloor) ||
                (dir == DIR_DOWN && e.direction == DIR_UP && floor > e.curFloor)) {
                cost += 100;
            }
        }
        if (cost < bestCost) {
            bestCost = cost;
            bestIdx = i;
        }
    }
    return bestIdx;
}

/**
 * 动态调度：遍历所有未完成的外部请求，重新计算最佳电梯并分配（支持重分配）
 * 这是实现动态分配的核心函数，会在以下时机自动调用：
 *   1) 新外部请求加入时
 *   2) 电梯每移动一步后
 *   3) 紧急模式解除后
 *   4) 外部请求服务完成后
 * 工作流程：
 *   - 对每个未完成的请求，调用 chooseBestElevator 寻找最佳电梯
 *   - 若请求尚未分配，则直接分配给最佳电梯
 *   - 若已分配但当前最佳电梯不同，则从旧电梯移除任务并转移至新电梯
 *   - 最后清理所有已完成（completed）的请求
 */
void dispatchRequests() {
    if (g_emergencyActive) {
        cout << "[调度] 紧急模式中，暂停所有外部请求分配。\n";
        return;
    }
    for (auto &req : pendingRequests) {
        if (req.completed) continue;   // 已完成的不再处理

        int best = chooseBestElevator(req.floor, req.dir);
        if (best == -1) {
            // 无法分配（全部满载或不可用），保留原有分配不变
            if (req.assignedElevator != -1) {
                cout << "[调度] 请求 " << req.floor << " 层 "
                     << (req.dir == DIR_UP ? "↑" : "↓")
                     << " 保留在电梯 " << req.assignedElevator + 1 << "（当前无更优空闲电梯）\n";
            } else {
                cout << "[调度] 请求 " << req.floor << " 层 "
                     << (req.dir == DIR_UP ? "↑" : "↓")
                     << " 暂时无法分配（全部满载或紧急），等待下次调度。\n";
            }
            continue;
        }

        if (req.assignedElevator == -1) {
            // 新请求：直接分配
            elevators[best].addExternalTask(req.floor);
            req.assignedElevator = best;
            cout << "[调度] 外部请求 " << req.floor << " 层 "
                 << (req.dir == DIR_UP ? "↑" : "↓")
                 << " 分配给电梯 " << best + 1 << endl;
        } else if (req.assignedElevator != best) {
            // 已分配但当前最佳电梯变了，进行重分配
            int oldElev = req.assignedElevator;
            elevators[oldElev].removeExternalTask(req.floor);  // 从旧电梯移除负数任务
            elevators[best].addExternalTask(req.floor);        // 加入新电梯
            req.assignedElevator = best;
            cout << "[重调度] 请求 " << req.floor << " 层 "
                 << (req.dir == DIR_UP ? "↑" : "↓")
                 << " 从电梯 " << oldElev + 1 << " 转移至电梯 " << best + 1 << endl;
        }
        // 如果分配未变，保持原样
    }

    // 清理已完成的请求（从持久化列表中移除）
    pendingRequests.erase(
        remove_if(pendingRequests.begin(), pendingRequests.end(),
                  [](const ExternalReq &r) { return r.completed; }),
        pendingRequests.end());
}

/**
 * 显示所有电梯的当前状态：位置、方向、门状态、任务列表、外部请求队列大小
 * 动态分配版本：增加显示未完成请求的分配情况
 */
void showStatus() {
    cout << "\n========== 电梯状态 ==========\n";
    for (size_t i = 0; i < elevators.size(); ++i) {
        Elevator &e = elevators[i];
        cout << "电梯 " << e.id+1 << " : 当前楼层 " << e.curFloor << "  | 方向 ";
        if (e.direction == DIR_UP) cout << "↑";
        else if (e.direction == DIR_DOWN) cout << "↓";
        else cout << "●";          // 空闲时显示的黑点
        cout << " | 门" << (e.isOpen ? "开" : "关");
        cout << " | 载重 " << e.currentLoad << "/" << Elevator::MAX_LOAD << " kg";
        cout << " | 乘客数 " << e.passengerCount();
        cout << " | 任务列表 [";
        for (size_t j = 0; j < e.tasks.size(); ++j) {
            // 动态分配：负数任务显示为 楼层- （例如 -5 显示为 5-）
            int val = e.tasks[j];
            if (val < 0) cout << -val << "-";
            else cout << val;
            if (j != e.tasks.size()-1) cout << ",";
        }
        cout << "]\n";
    }
    cout << "外部请求数量: " << pendingRequests.size();
    if (!pendingRequests.empty()) {
        cout << " （";
        for (size_t i = 0; i < pendingRequests.size(); ++i) {
            if (i > 0) cout << ", ";
            cout << pendingRequests[i].floor << (pendingRequests[i].dir == DIR_UP ? "↑" : "↓")
                 << "->电梯" << (pendingRequests[i].assignedElevator == -1 ? "?" : to_string(pendingRequests[i].assignedElevator + 1));
        }
        cout << "）";
    }
    cout << endl;
    cout << "紧急模式: " << (g_emergencyActive ? "激活" : "关闭") << endl;
    cout << "================================\n";
}

/**
 * 电梯移动一步（模拟一个时间单位）
 * 动态分配版本：
 *   - 到达目标楼层时，若为外部请求（负数任务），自动标记对应请求完成，并触发重调度
 *   - 每移动一层后，自动调用 dispatchRequests() 进行动态重分配
 */
void moveOneStep(Elevator &e) {
    int target = e.getNextTarget();
    if (target == -1) {
        e.direction = DIR_STOP;
        return;
    }
    if (e.curFloor == target) {
        // 检查当前任务是否为外部请求（任务值小于0）
        bool isExternalTask = !e.tasks.empty() && e.tasks[0] < 0;
        
        // 紧急模式处理（保持不变）
        if (g_emergencyActive && e.hasPassenger()) {
            if (!e.isOpen) {
                e.isOpen = true;
                cout << "电梯 " << e.id+1 << " 在 " << e.curFloor << " 层开门紧急疏散乘客\n";
                e.clearPassengers();
                e.isOpen = false;
                e.popTask();
                cout << "电梯 " << e.id+1 << " 疏散完毕，关门\n";
                e.tasks.clear();
                return;
            }
        }

        // 到达目标楼层
        if (!e.isOpen) {
            e.isOpen = true;
            cout << "电梯 " << e.id+1 << " 到达 " << e.curFloor << " 层，开门\n";
        }

        if (isExternalTask) {
            // 外部请求任务：服务完成，标记所有分配给本梯的该楼层请求为已完成
            for (auto &req : pendingRequests) {
                if (!req.completed && req.floor == e.curFloor && req.assignedElevator == e.id) {
                    req.completed = true;
                    cout << "电梯 " << e.id+1 << " 完成外部请求 " << req.floor 
                         << " 层 " << (req.dir == DIR_UP ? "↑" : "↓") << " 的服务。\n";
                }
            }
        } else {
            // 内部送客任务
            e.alightPassengersAt(e.curFloor);
        }

        // 关门并移除该任务
        e.isOpen = false;
        e.popTask();
        cout << "电梯 " << e.id+1 << " 关门，继续运行\n";
        e.optimizeTasks();   // 任务变化后重新规划顺序

        // 外部请求完成后，立即进行一次重分配（可能会把未分配的请求交给更合适的电梯）
        dispatchRequests();
        return;
    }
    // 未到达，移动一层
    if (e.curFloor < target) {
        e.direction = DIR_UP;
        e.curFloor++;
    } else {
        e.direction = DIR_DOWN;
        e.curFloor--;
    }
    cout << "电梯 " << e.id+1 << " 移动到 " << e.curFloor << " 层\n";
    // 位置改变后，触发动态重调度（可能转移未服务的请求）
    dispatchRequests();
}

/**
 * 连续运行所有电梯，直到所有任务完成为止
 * 每次循环中，所有电梯同时移动一步（模拟并发）
 */
void runUntilIdle() {
    bool hasTask = true;
    while (hasTask) {
        hasTask = false;
        for (size_t i = 0; i < elevators.size(); ++i) {
            if (!elevators[i].tasks.empty()) {
                hasTask = true;
                moveOneStep(elevators[i]);
            }
        }
    }
    if (g_emergencyActive)
        cout << "\n所有电梯紧急任务完成，已停止服务。\n";
    else
        cout << "\n所有电梯任务完成，已空闲。\n";
}

/**
 * 单步运行：所有电梯同时移动一步（若有任务），否则提示空闲
 * 动态分配版本：移动后触发全局重调度
 */
void stepOne() {
    bool moved = false;
    for (size_t i = 0; i < elevators.size(); ++i) {
        if (!elevators[i].tasks.empty()) {
            moveOneStep(elevators[i]);
            moved = true;
        }
    }
    if (!moved) cout << "所有电梯空闲，无动作。\n";
    else dispatchRequests();  // 步进后重新调度
}

// ---------- 紧急模式（安全楼层固定为1层） ----------
/**
 * 执行紧急模式，根据不同类型计算目标安全楼层，并区分电梯是否有人
 * 与原版完全相同，未做修改
 */
void emergencyMode(EmergType type, int dangerFloor = 0) {
    if (g_emergencyActive) {
        cout << "系统已处于紧急模式，请先解除。\n";
        return;
    }
    g_emergencyActive = true;
    cout << "\n!!! 紧急模式触发 !!!\n";
    int targetFloor;
    string reason;
    int avoidFloor = -1;

    switch (type) {
        case FIRE:
            reason = "火灾";
            avoidFloor = dangerFloor;
            if (avoidFloor < g_minFloor || avoidFloor > g_maxFloor) avoidFloor = -1;
            targetFloor = 1;
            if (targetFloor < g_minFloor || targetFloor > g_maxFloor)
                targetFloor = g_minFloor;
            break;
        case FAULT:
            reason = "故障";
            targetFloor = 1;
            if (targetFloor < g_minFloor || targetFloor > g_maxFloor)
                targetFloor = g_minFloor;
            break;
        case FLOOD:
            reason = "水浸";
            avoidFloor = dangerFloor;
            targetFloor = dangerFloor + 2;
            if (targetFloor > g_maxFloor) targetFloor = g_maxFloor;
            if (targetFloor < g_minFloor) targetFloor = g_minFloor;
            break;
        case EARTHQUAKE:
            reason = "地震";
            targetFloor = -1;
            break;
    }
    cout << "原因：" << reason;
    if (type == FLOOD) cout << "（进水楼层 " << dangerFloor << "）";
    if (type == FIRE) cout << "（着火楼层 " << avoidFloor << "）";
    cout << endl;

    // 强制关门
    for (size_t i = 0; i < elevators.size(); ++i) {
        if (elevators[i].isOpen) {
            elevators[i].isOpen = false;
            cout << "电梯 " << i+1 << " 强制关门\n";
        }
    }
    // 备份当前任务并清空，将所有电梯任务改为紧急任务
    while (!emergencyBackup.empty()) emergencyBackup.pop();
    for (size_t i = 0; i < elevators.size(); ++i) {
        emergencyBackup.push(elevators[i].tasks);
        elevators[i].tasks.clear();
    }

    // 为每部电梯生成紧急任务
    for (size_t i = 0; i < elevators.size(); ++i) {
        Elevator &e = elevators[i];

        if (e.hasPassenger()) {
            int safeStop = e.curFloor;
            if (avoidFloor != -1 && e.curFloor == avoidFloor) {
                safeStop = (e.curFloor < g_maxFloor) ? e.curFloor + 1 : e.curFloor - 1;
                if (safeStop == avoidFloor) {
                    safeStop = (safeStop < g_maxFloor) ? safeStop + 1 : safeStop - 1;
                }
            }
            if (safeStop < g_minFloor) safeStop = g_minFloor;
            if (safeStop > g_maxFloor) safeStop = g_maxFloor;

            if (type == EARTHQUAKE) {
                if (e.curFloor != safeStop) e.addTask(safeStop);
                cout << "电梯 " << e.id+1 << " 内有乘客，地震就近疏散至 " << safeStop << " 层。\n";
            } else {
                e.addTask(safeStop);
                if (safeStop != targetFloor)
                    e.addTask(targetFloor);
                cout << "电梯 " << e.id+1 << " 内有乘客，就近疏散至 " << safeStop 
                     << " 层，随后前往基站 " << targetFloor << " 层。\n";
            }
        } else {
            if (type == EARTHQUAKE) {
                int base = (1 >= g_minFloor && 1 <= g_maxFloor) ? 1 : g_minFloor;
                if (e.curFloor != base) e.addTask(base);
                cout << "电梯 " << e.id+1 << " 无乘客，地震直接前往基站 " << base << " 层并停止服务。\n";
            } else {
                if (e.curFloor != targetFloor) e.addTask(targetFloor);
                cout << "电梯 " << e.id+1 << " 无乘客，直接前往安全楼层 " << targetFloor << " 层。\n";
            }
        }
    }

    runUntilIdle();  // 执行归位/疏散
}

/**
 * 无参数的紧急模式，默认触发火灾（着火层为1）
 */
void emergencyMode() {
    emergencyMode(FIRE, 1);
}

/**
 * 解除紧急模式，恢复之前备份的任务
 * 与原版相同，但恢复后立即触发一次动态调度
 */
void recoverFromEmergency() {
    if (!g_emergencyActive) {
        cout << "没有处于紧急模式或备份为空。\n";
        return;
    }
    g_emergencyActive = false;
    cout << "\n解除紧急模式，恢复之前的任务。\n";
    // 注意：备份栈的弹出顺序与电梯编号相反（因为压栈时从0到N-1）
    for (int i = elevators.size() - 1; i >= 0; --i) {
        if (!emergencyBackup.empty()) {
            vector<int> backupTasks = emergencyBackup.top();
            emergencyBackup.pop();
            for (int floor : backupTasks) {
                // 恢复的任务统一视为内部请求（正数），外部请求由 pendingRequests 重新分配
                elevators[i].addTask(floor);
            }
        }
    }
    dispatchRequests();   // 恢复后立即进行动态调度，重新分配未完成的外部请求
    cout << "任务恢复完成。\n";
}

/**
 * 管理员手动控制菜单（交互式）
 * 与原版完全相同
 */
void manualControl() {
    int id, cmd, floor;
    cout << "请输入电梯编号(1~" << g_elevatorNum << "): ";
    cin >> id;
    if (id<1 || id>g_elevatorNum) { cout << "无效编号\n"; return; }
    Elevator &e = elevators[id-1];
    cout << "手动命令: 1-强制前往某层  2-强制开关门(1开0关)  3-添加内部请求: ";
    cin >> cmd;
    if (cmd == 1) {
        cout << "目标楼层: ";
        cin >> floor;
        if (isValidFloor(floor)) {
            e.addTask(floor);
            cout << "已添加任务\n";
        } else cout << "楼层无效\n";
    } else if (cmd == 2) {
        int s; cin >> s;
        e.isOpen = (s==1);
        cout << "门" << (e.isOpen?"开":"关") << "\n";
    } else if (cmd == 3) {
        cout << "目标楼层: ";
        cin >> floor;
        if (isValidFloor(floor)) {
            e.addTask(floor);
            cout << "内部请求已加入\n";
        } else cout << "无效楼层\n";
    } else cout << "无效命令\n";
}

/**
 * 交互式添加外部呼叫
 * 动态分配版本：请求加入 pendingRequests 并立即触发调度
 */
void addExternal() {
    if (g_emergencyActive) {
        cout << "紧急模式中，不接受外部请求。\n";
        return;
    }
    int floor, dir;
    cout << "请输入请求楼层(" << g_minFloor << "~" << g_maxFloor << "): ";
    cin >> floor;
    cout << "方向(1-上, -1-下): ";
    cin >> dir;
    if (!isValidFloor(floor) || (dir!=1 && dir!=-1)) {
        cout << "输入无效\n";
        return;
    }
    // 创建新请求，初始未分配
    pendingRequests.push_back({floor, dir, -1, false});
    cout << "外部请求已加入，系统将动态分配。\n";
    dispatchRequests();   // 立即尝试分配
}

// ======================= 文件测试相关 =======================

/**
 * 重置系统：清空所有电梯、请求列表、备份栈，根据全局 g_elevatorNum 重新创建电梯
 */
void resetSystem() {
    elevators.clear();
    for (int i=0; i<g_elevatorNum; ++i) {
        elevators.push_back(Elevator(i));
    }
    pendingRequests.clear();
    while (!emergencyBackup.empty()) emergencyBackup.pop();
    g_emergencyActive = false;
}

/**
 * 从指定文件读取测试指令并执行（支持中文文件名）
 * 支持的命令不变，但外部请求改为使用 pendingRequests
 */
void runTestFromFile(const string& filename) {
    ifstream infile;
    infile.open(fs::u8path(filename));
    if (!infile.is_open()) {
        cout << "无法打开文件: " << filename << endl;
        return;
    }
    resetSystem();
    cout << "\n========== 开始运行测试文件：" << filename << " ==========\n";                         
    string line;
    while (getline(infile, line)) {
        size_t pos = line.find_first_not_of(" \t");
        if (pos == string::npos || line[pos] == '#') continue;
        line = line.substr(pos);
        
        istringstream iss(line);
        string cmd;
        iss >> cmd;
        
        if (cmd == "EXT") {
            int floor, dir;
            iss >> floor >> dir;
            if (!isValidFloor(floor)) {
                cout << "[文件] 错误：楼层 " << floor << " 超出范围\n";
                continue;
            }
            if (g_emergencyActive) {
                cout << "[文件] 紧急模式，外部请求被忽略。\n";
                continue;
            }
            pendingRequests.push_back({floor, dir, -1, false});
            cout << "[文件] 外部请求: " << floor << "层 " << (dir==1?"↑":"↓") << endl;
            dispatchRequests();
        }
        else if (cmd == "INT") {
            int eid, floor;
            iss >> eid >> floor;
            if (eid<1 || eid>g_elevatorNum) {
                cout << "[文件] 错误：电梯编号超出范围\n";
                continue;
            }
            if (!isValidFloor(floor)) {
                cout << "[文件] 错误：楼层 " << floor << " 超出范围\n";
                continue;
            }
            elevators[eid-1].addTask(floor);
            cout << "[文件] 电梯" << eid << " 内部请求: " << floor << "层" << endl;
        }
        else if (cmd == "BOARD") {
            int eid, target;
            iss >> eid >> target;
            if (eid<1 || eid>g_elevatorNum) {
                cout << "[文件] 错误：电梯编号超出范围\n";
                continue;
            }
            if (!isValidFloor(target)) {
                cout << "[文件] 错误：目标楼层 " << target << " 超出范围\n";
                continue;
            }
            if (g_emergencyActive) {
                cout << "[文件] 紧急模式，禁止上客。\n";
                continue;
            }
            bool ok = elevators[eid-1].boardPassenger(target);
            if (ok)
                cout << "[文件] 电梯" << eid << " 上客成功，去往 " << target 
                     << " 层，当前载重 " << elevators[eid-1].currentLoad << " kg\n";
            else
                cout << "[文件] 电梯" << eid << " 上客失败（超重）\n";
        }
        else if (cmd == "EMERGENCY") {
            string sub;
            if (iss >> sub) {
                if (sub == "FIRE") {
                    int fireFloor;
                    if (iss >> fireFloor) {
                        if (!isValidFloor(fireFloor)) {
                            cout << "[文件] 错误：着火楼层无效\n";
                            continue;
                        }
                        emergencyMode(FIRE, fireFloor);
                    } else {
                        emergencyMode(FIRE, 1);
                    }
                }
                else if (sub == "FAULT") emergencyMode(FAULT);
                else if (sub == "FLOOD") {
                    int floodFloor;
                    if (iss >> floodFloor) {
                        if (!isValidFloor(floodFloor)) {
                            cout << "[文件] 错误：进水楼层无效\n";
                            continue;
                        }
                        emergencyMode(FLOOD, floodFloor);
                    } else {
                        cout << "[文件] 错误：FLOOD 需要指定楼层\n";
                    }
                }
                else if (sub == "EARTHQUAKE") {
                    emergencyMode(EARTHQUAKE);
                }
                else {
                    cout << "[文件] 未知紧急类型: " << sub << endl;
                }
            } else {
                emergencyMode();
            }
        }
        else if (cmd == "RECOVER") recoverFromEmergency();
        else if (cmd == "STEP") {
            int steps; iss >> steps;
            for (int i=0; i<steps; ++i) stepOne();
        }
        else if (cmd == "RUN") runUntilIdle();
        else if (cmd == "SHOW") showStatus();
        else if (cmd == "MANUAL") {
            int eid, mcmd, arg;
            iss >> eid >> mcmd >> arg;
            if (eid<1 || eid>g_elevatorNum) {
                cout << "[文件] 错误：电梯编号超出范围\n";
                continue;
            }
            Elevator &e = elevators[eid-1];
            if (mcmd == 1 || mcmd == 3) {
                if (!isValidFloor(arg)) {
                    cout << "[文件] 错误：楼层 " << arg << " 超出范围\n";
                    continue;
                }
                e.addTask(arg);
                if (mcmd == 1)
                    cout << "[文件] 管理员：电梯" << eid << " 强制前往 " << arg << " 层" << endl;
                else
                    cout << "[文件] 管理员：电梯" << eid << " 内部请求 " << arg << " 层" << endl;
            } else if (mcmd == 2) {
                e.isOpen = (arg == 1);
                cout << "[文件] 管理员：电梯" << eid << " 门" << (e.isOpen?"开":"关") << endl;
            }
        }
    }
    infile.close();
    cout << "========== 测试文件执行完毕 ==========\n\n";
}

/**
 * 显示预置的测试样例列表，覆盖所有场景（含新增的动态分配、满载、恢复等）
 * 用户通过数字选择即可自动执行对应的测试文件
 */
void runSample() {
    // 包含所有场景的测试样例文件名列表（共11个）
    vector<string> samples = {
        "基本功能测试.txt",
        "动态重分配测试.txt",
        "乘客与满载测试.txt",
        "火灾有人测试.txt",
        "火灾无人测试.txt",
        "积水测试.txt",
        "故障测试.txt",
        "地震测试.txt",
        "紧急恢复测试.txt",
        "管理员手动测试.txt",
        "综合场景测试.txt"
    };

    while (true) {
        cout << "\n========== 选择测试样例 ==========\n";
        for (size_t i = 0; i < samples.size(); ++i) {
            cout << i+1 << ". " << samples[i] << endl;
        }
        cout << "0. 返回主菜单\n";
        int choice;
        cin >> choice;
        if (choice == 0) break;
        if (choice >= 1 && choice <= (int)samples.size()) {
            runTestFromFile(samples[choice-1]);
        } else {
            cout << "无效选择，请重新输入。\n";
        }
    }
}

// ======================= 主函数（程序入口） =======================
int main() {
#ifdef _WIN32
    system("chcp 65001 > nul");   // 将控制台编码切换为 UTF-8，正确显示中文
#endif

    // 输入楼层范围
    cout << "请输入最低楼层（可负数）: ";
    cin >> g_minFloor;
    cout << "请输入最高楼层: ";
    cin >> g_maxFloor;
    while (g_maxFloor <= g_minFloor) {
        cout << "最高楼层必须大于最低楼层，请重新输入最高楼层: ";
        cin >> g_maxFloor;
    }
    cout << "楼层范围已设为 [" << g_minFloor << " ~ " << g_maxFloor << "]"
         << "，紧急安全楼层（火灾/故障）默认为 1 层。\n";

    // 输入电梯数量
    cout << "请输入电梯数量（2~6部）：";
    cin >> g_elevatorNum;
    while (g_elevatorNum < 2 || g_elevatorNum > 6) {
        cout << "数量范围2~6，请重新输入：";
        cin >> g_elevatorNum;
    }
    resetSystem();   // 初始化所有电梯

    // 主交互循环
    while (true) {
        cout << "\n========== 多路电梯控制系统（楼层：" << g_minFloor << "~" << g_maxFloor
             << "，电梯数：" << g_elevatorNum << "，最大承重：" << Elevator::MAX_LOAD << " kg） ==========\n";
        cout << "1. 添加外部呼叫\n";
        cout << "2. 添加内部请求\n";
        cout << "3. 显示状态\n";
        cout << "4. 执行一步\n";
        cout << "5. 运行直到空闲\n";
        cout << "6. 紧急模式\n";
        cout << "7. 解除紧急模式\n";
        cout << "8. 管理员手动控制\n";
        cout << "9. 模拟乘客上梯\n";
        cout << "0. 从文件运行测试\n";
        cout << "q. 退出\n";
        cout << "请选择: ";
        char ch; cin >> ch;
        if (ch == 'q') break;
        switch(ch) {
            case '1': addExternal(); break;
            case '2': {
                int id, f;
                cout << "电梯号(1~" << g_elevatorNum << "): "; cin >> id;
                cout << "楼层: "; cin >> f;
                if(id>=1 && id<=g_elevatorNum && isValidFloor(f))
                    elevators[id-1].addTask(f);
                else
                    cout << "无效\n";
                break;
            }
            case '3': showStatus(); break;
            case '4': stepOne(); break;
            case '5': runUntilIdle(); break;
            case '6': {
                cout << "选择紧急类型:\n1. 火灾\n2. 故障\n3. 水浸\n4. 地震\n请选择: ";
                int type; cin >> type;
                if (type == 1) {
                    int fireFloor;
                    cout << "请输入着火楼层: ";
                    cin >> fireFloor;
                    if (!isValidFloor(fireFloor))
                        cout << "无效楼层，操作取消。\n";
                    else
                        emergencyMode(FIRE, fireFloor);
                }
                else if (type == 2) emergencyMode(FAULT);
                else if (type == 3) {
                    int floodFloor;
                    cout << "请输入进水楼层: ";
                    cin >> floodFloor;
                    if (!isValidFloor(floodFloor))
                        cout << "无效楼层，操作取消。\n";
                    else
                        emergencyMode(FLOOD, floodFloor);
                }
                else if (type == 4) emergencyMode(EARTHQUAKE);
                else cout << "无效选择\n";
                break;
            }
            case '7': recoverFromEmergency(); break;
            case '8': manualControl(); break;
            case '9': {
                int eid, target;
                cout << "电梯编号(1~" << g_elevatorNum << "): "; cin >> eid;
                cout << "目标楼层: "; cin >> target;
                if (eid >= 1 && eid <= g_elevatorNum && isValidFloor(target)) {
                    if (g_emergencyActive) {
                        cout << "紧急模式中，禁止上客。\n";
                    } else {
                        bool ok = elevators[eid-1].boardPassenger(target);
                        if (!ok) cout << "上客失败。\n";
                    }
                } else {
                    cout << "输入无效\n";
                }
                break;
            }
            case '0': runSample(); break;
            default: cout << "无效选项\n";
        }
    }
    return 0;
}