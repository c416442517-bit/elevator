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
 */
struct ExternalReq {
    int floor;  // 请求所在楼层
    int dir;    // 请求方向：DIR_UP 或 DIR_DOWN
};

/**
 * 乘客信息：仅记录目标楼层（体重由电梯自动按平均值计算）
 */
struct Passenger {
    int target;      // 目标楼层
};

/**
 * 电梯类：封装单部电梯的所有属性和行为
 */
class Elevator {
public:
    int id;                 // 电梯编号（0开始，显示时+1）
    int curFloor;           // 当前所在楼层
    int direction;          // 当前运动方向（上/下/停）
    bool isOpen;            // 门状态：true开，false关
    vector<int> tasks;      // 任务列表：需要停靠的楼层

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
        addTask(targetFloor);                    // 自动加入送客任务
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
     * @param floor 目标楼层
     */
    void addTask(int floor) {
        if (floor == curFloor) {
            // 同层请求：直接开门完成
            if (!isOpen) {
                isOpen = true;
                cout << "电梯 " << id+1 << " 在 " << curFloor << " 层已开门（同层响应）\n";
                isOpen = false;
                cout << "电梯 " << id+1 << " 关门（服务完成）\n";
            }
            return;
        }
        tasks.push_back(floor);
        optimizeTasks();    // 每次添加后优化顺序
    }

    /**
     * 优化任务列表顺序：根据当前楼层和方向，将同向任务优先执行
     * 采用简化的LOOK算法：先处理与当前方向同向的任务，再反向
     */
    void optimizeTasks() {
        if (tasks.empty()) return;

        // 先按楼层升序排序
        sort(tasks.begin(), tasks.end());

        // 将任务分为上行任务（大于当前楼层）和下行任务（小于当前楼层）
        vector<int> up, down;
        for (int f : tasks) {
            if (f > curFloor) up.push_back(f);
            else if (f < curFloor) down.push_back(f);
            // 等于当前楼层的任务忽略（已在当前层，后续到达时会立即处理）
        }

        // 分别排序：上行升序，下行降序（这样下行时先从高楼层往低走）
        sort(up.begin(), up.end());
        sort(down.begin(), down.end(), greater<int>());

        tasks.clear();

        // 根据当前方向决定任务顺序
        // 如果当前向上 或 停止且有待处理的上行任务，则优先上行
        if (direction == DIR_UP || (direction == DIR_STOP && !up.empty())) {
            for (int f : up) tasks.push_back(f);    // 先上
            for (int f : down) tasks.push_back(f);  // 后下
        } else {
            // 否则优先下行
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
     * 获取下一个目标楼层（任务列表的第一个）
     * @return 目标楼层，若无任务返回-1
     */
    int getNextTarget() {
        return tasks.empty() ? -1 : tasks[0];
    }
};

// ======================= 全局变量 =======================
vector<Elevator> elevators;                 // 所有电梯对象
queue<ExternalReq> extQueue;               // 等待分配的外部请求队列
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
 * @param floor 请求楼层
 * @param dir   请求方向（上/下）
 * @return      最佳电梯的索引（0~g_elevatorNum-1），若失败返回-1
 * 策略：计算每个电梯到请求楼层的距离，并增加方向不顺路的惩罚（+100），选代价最小者
 * 贪心策略，减少乘客等待时间
 */
int chooseBestElevator(int floor, int dir) {
    if (g_emergencyActive) return -1;   // 紧急模式下禁止分配
    int bestIdx = -1;
    int bestCost = INT_MAX;
    for (size_t i = 0; i < elevators.size(); ++i) {
        Elevator &e = elevators[i];

        // 满载电梯无法再接人，直接跳过（代价无穷大）
        if (e.isFull()) continue;

        int cost = abs(e.curFloor - floor);   // 基础距离代价

        // 方向惩罚：如果电梯当前运行方向与请求方向相反且请求楼层在电梯后方，则代价大幅增加
        if (e.direction != DIR_STOP) {
            if ((dir == DIR_UP && e.direction == DIR_DOWN && floor < e.curFloor) ||
                (dir == DIR_DOWN && e.direction == DIR_UP && floor > e.curFloor)) {
                cost += 100;   // 不顺路，大幅增加代价
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
 * 处理外部请求队列：将队列中的所有请求分配给最佳电梯
 * 每次分配后，请求即从队列中移除
 */
void dispatchRequests() {
    if (g_emergencyActive) {
        cout << "[调度] 紧急模式中，暂停所有外部请求分配。\n";
        return;
    }
    while (!extQueue.empty()) {
        ExternalReq req = extQueue.front();   // 查看队首，不立即弹出
        int best = chooseBestElevator(req.floor, req.dir);
        if (best == -1) {
            // 无法分配（全部满载或紧急），保留请求等待下次调度
            cout << "[调度] 请求 " << req.floor << " 层 "
                 << (req.dir == DIR_UP ? "↑" : "↓")
                 << " 暂时无法分配（可能全部满载），等待下次调度。\n";
            break;
        }
        extQueue.pop();   // 确认能分配后再弹出
        elevators[best].addTask(req.floor);
        cout << "[调度] 外部请求 " << req.floor << " 层 "
             << (req.dir == DIR_UP ? "↑" : "↓")
             << " 分配给电梯 " << best+1 << endl;
    }
}

/**
 * 显示所有电梯的当前状态：位置、方向、门状态、任务列表、外部请求队列大小
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
            cout << e.tasks[j];
            if (j != e.tasks.size()-1) cout << ",";
        }
        cout << "]\n";
    }
    cout << "外部请求队列大小: " << extQueue.size() << endl;
    cout << "紧急模式: " << (g_emergencyActive ? "激活" : "关闭") << endl;
    cout << "================================\n";
}

/**
 * 电梯移动一步（模拟一个时间单位）
 * 逻辑：
 *   - 若无任务，方向置停，返回
 *   - 若已到达目标楼层，开门（模拟）、关门、移除任务、重新优化任务顺序
 *   - 否则向目标楼层移动一层，并更新方向
 */
void moveOneStep(Elevator &e) {
    int target = e.getNextTarget();
    if (target == -1) {
        e.direction = DIR_STOP;
        return;
    }
    if (e.curFloor == target) {
        // 如果处于紧急模式且电梯内有乘客，到达任何楼层都应开门疏散
        if (g_emergencyActive && e.hasPassenger()) {
            if (!e.isOpen) {
                e.isOpen = true;
                cout << "电梯 " << e.id+1 << " 在 " << e.curFloor << " 层开门紧急疏散乘客\n";
                e.clearPassengers();
                e.isOpen = false;
                e.popTask();
                cout << "电梯 " << e.id+1 << " 疏散完毕，关门\n";
                // 紧急模式下疏散后不再执行剩余任务，清空任务列表
                e.tasks.clear();
                return;
            }
        }

        // 到达目标楼层
        if (!e.isOpen) {
            e.isOpen = true;
            cout << "电梯 " << e.id+1 << " 到达 " << e.curFloor << " 层，开门\n";
        }
        // 正常下客
        e.alightPassengersAt(e.curFloor);
        // 关门并移除该任务（模拟乘客进出后关门）
        e.isOpen = false;
        e.popTask();
        cout << "电梯 " << e.id+1 << " 关门，继续运行\n";
        e.optimizeTasks();   // 任务变化后重新规划顺序
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
}

// ---------- 紧急模式（安全楼层固定为1层） ----------
/**
 * 执行紧急模式，根据不同类型计算目标安全楼层，并区分电梯是否有人
 * @param type          紧急类型：FIRE/FAULT/FLOOD/EARTHQUAKE
 * @param dangerFloor   危险楼层（火灾为着火层，水浸为进水层，其它忽略）
 * 规则：
 *   - 火灾/故障 → 有乘客就近非着火层疏散，无乘客回基站（1层）
 *   - 水浸 → 有乘客就近非进水层疏散，无乘客前往进水层+2
 *   - 地震 → 无论是否有乘客，就近停靠开门疏散，之后停用
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
    int avoidFloor = -1;   // 需避开的具体危险楼层

    switch (type) {
        case FIRE:
            reason = "火灾";
            avoidFloor = dangerFloor;
            if (avoidFloor < g_minFloor || avoidFloor > g_maxFloor) avoidFloor = -1;
            targetFloor = 1;                      // 固定首层
            if (targetFloor < g_minFloor || targetFloor > g_maxFloor)
                targetFloor = g_minFloor;
            break;
        case FAULT:
            reason = "故障";
            targetFloor = 1;                      // 返基站也去首层
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
            // 地震就近停靠，无特定危险楼层和统一目标
            targetFloor = -1;   // 表示“就近处理”
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
            // 有乘客：就近找安全楼层疏散
            int safeStop = e.curFloor;
            // 如果当前楼层是危险楼层，必须移到相邻安全层
            if (avoidFloor != -1 && e.curFloor == avoidFloor) {
                safeStop = (e.curFloor < g_maxFloor) ? e.curFloor + 1 : e.curFloor - 1;
                // 再次检查，防止相邻层也是危险层（极小概率，但安全起见）
                if (safeStop == avoidFloor) {
                    safeStop = (safeStop < g_maxFloor) ? safeStop + 1 : safeStop - 1;
                }
            }
            // 确保安全楼层合法
            if (safeStop < g_minFloor) safeStop = g_minFloor;
            if (safeStop > g_maxFloor) safeStop = g_maxFloor;

            if (type == EARTHQUAKE) {
                // 地震：就近停靠疏散，不再去其他楼层
                if (e.curFloor != safeStop) e.addTask(safeStop);
                cout << "电梯 " << e.id+1 << " 内有乘客，地震就近疏散至 " << safeStop << " 层。\n";
            } else {
                // 火灾/水浸/故障：先疏散乘客，再前往基站（如疏散层不是基站）
                e.addTask(safeStop);
                if (safeStop != targetFloor)
                    e.addTask(targetFloor);
                cout << "电梯 " << e.id+1 << " 内有乘客，就近疏散至 " << safeStop 
                     << " 层，随后前往基站 " << targetFloor << " 层。\n";
            }
        } else {
            // 无乘客：直接去安全楼层并停用
            if (type == EARTHQUAKE) {
                // 地震无乘客也就近去基站（1层）停用
                int base = (1 >= g_minFloor && 1 <= g_maxFloor) ? 1 : g_minFloor;
                if (e.curFloor != base) e.addTask(base);
                cout << "电梯 " << e.id+1 << " 无乘客，地震直接前往基站 " << base << " 层并停止服务。\n";
            } else {
                // 其他紧急类型：前往计算出的安全楼层
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
 * 注意：备份栈的弹出顺序要对应电梯编号（后进先出，这里使用逆序恢复）
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
            vector<int> backupTasks = emergencyBackup.top();  // 获取备份
            emergencyBackup.pop();

            // 合并：将备份任务逐个加入当前电梯（保留当前任务，如紧急期间产生的新请求）
            for (int floor : backupTasks) {
                elevators[i].addTask(floor);   // addTask 内部会调用 optimizeTasks
            }
            // 由于 addTask 每次都会优化，最终任务列表会按 LOOK 顺序排好
        }
    }
    cout << "任务恢复完成。\n";
}

/**
 * 管理员手动控制菜单（交互式）
 * 可对指定电梯：1-强制前往某层  2-强制开关门  3-添加内部请求
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
 * 输入楼层和方向，加入外部队列后立即尝试分配
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
    extQueue.push({floor, dir});
    cout << "外部请求已加入队列，稍后将由调度器分配。\n";
    dispatchRequests();   // 立即分配一次
}

// ======================= 文件测试相关 =======================

/**
 * 重置系统：清空所有电梯、外部队列、备份栈，根据全局 g_elevatorNum 重新创建电梯
 */
void resetSystem() {
    elevators.clear();
    for (int i=0; i<g_elevatorNum; ++i) {
        elevators.push_back(Elevator(i));
    }
    while (!extQueue.empty()) extQueue.pop();
    while (!emergencyBackup.empty()) emergencyBackup.pop();
    g_emergencyActive = false;
}

/**
 * 从指定文件读取测试指令并执行（支持中文文件名）
 * 支持的命令：
 *   EXT floor dir         - 添加外部请求
 *   INT elevatorId floor  - 添加内部请求（指定电梯）
 *   BOARD eid target      - 模拟乘客上梯（电梯号 目标楼层）
 *   EMERGENCY [类型]      - 触发紧急模式（FIRE/FAULT/FLOOD/EARTHQUAKE）
 *   RECOVER               - 解除紧急模式
 *   STEP steps            - 单步运行指定步数
 *   RUN                   - 运行直到空闲
 *   SHOW                  - 显示当前状态
 *   MANUAL eid cmd arg    - 管理员手动干预（1-强制任务, 2-开关门, 3-内部请求）
 *   以 '#' 开头的行视为注释，跳过
 * @param filename 测试文件名（支持 UTF-8 中文路径）
 */
void runTestFromFile(const string& filename) {
    ifstream infile;
    infile.open(fs::u8path(filename));          // 使用 filesystem 支持中文路径
    if (!infile.is_open()) {
        cout << "无法打开文件: " << filename << endl;
        return;
    }
    resetSystem();                              // 每次运行前重置系统
    cout << "\n========== 开始运行测试文件：" << filename << " ==========\n";                         
    string line;
    while (getline(infile, line)) {
        // 去除行首空白字符，跳过空行和注释行
        size_t pos = line.find_first_not_of(" \t");
        if (pos == string::npos || line[pos] == '#') continue;
        line = line.substr(pos);                // 截去前导空白
        
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
            extQueue.push({floor, dir});
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
                        emergencyMode(FIRE, 1);   // 默认1层着火
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
                emergencyMode();   // 默认火灾
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
            if (mcmd == 1 || mcmd == 3) {   // 强制任务 或 内部请求
                if (!isValidFloor(arg)) {
                    cout << "[文件] 错误：楼层 " << arg << " 超出范围\n";
                    continue;
                }
                e.addTask(arg);
                if (mcmd == 1)
                    cout << "[文件] 管理员：电梯" << eid << " 强制前往 " << arg << " 层" << endl;
                else
                    cout << "[文件] 管理员：电梯" << eid << " 内部请求 " << arg << " 层" << endl;
            } else if (mcmd == 2) {   // 开关门
                e.isOpen = (arg == 1);
                cout << "[文件] 管理员：电梯" << eid << " 门" << (e.isOpen?"开":"关") << endl;
            }
        }
    }
    infile.close();
    cout << "========== 测试文件执行完毕 ==========\n\n";
}

/**
 * 显示预置的测试样例列表（可根据需要自行扩充）
 */
void runSample() {
    vector<string> samples = {
        "火灾有人测试.txt",
        "火灾无人测试.txt",
        "地震测试.txt"
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