# MSPM0G3507 Seven-Channel Line Tracking Car

基于 TI MSPM0G3507 的七路数字循迹小车工程，使用 CCS Theia、SysConfig 和 DriverLib 开发。工程包含双电机 PWM 驱动、左右编码器测速、速度 PID、七路循迹加权误差计算、直角弯增强和 10ms 周期控制中断。

默认运行模式为循迹闭环，适合 TB6612 类双路电机驱动板、带编码器减速电机和七路数字循迹模块。

## 功能特性

- MSPM0G3507 原生 DriverLib 工程
- 七路数字循迹输入，支持 `S1` 到 `S7`
- 循迹权重：`{50, 33, 16, 0, -16, -33, -50}`
- 直角弯增强识别：
  - `S1~S4` 同时触发：左直角弯
  - `S4~S7` 同时触发：右直角弯
- 双电机独立控制
- 左电机使用 TIMG8 QEI 硬件正交解码
- 右电机使用 GPIO 双边沿中断软件正交解码
- PWM 范围保持 `0..3199`
- 10ms 定时控制周期
- 支持四种运行模式：
  - `0`：禁用停车
  - `1`：开环 PWM
  - `2`：速度 PID
  - `3`：循迹闭环
- 保留 `g_car` 和 `g_line` 全局调试变量，方便 Ozone / CCS 观察和在线调参

## 硬件连接
![alt text](mspm0g3507_pinout-1.svg)
### 七路循迹模块

按小车前进方向定义左右：站在车尾看向车头，最左边探头为 `S1`，中间探头为 `S4`，最右边探头为 `S7`。

| 循迹通道 | MSPM0G3507 引脚 | 说明 |
|---|---|---|
| S1 | PA14 | 最左侧探头 |
| S2 | PA15 | 左侧探头 |
| S3 | PA16 | 左中探头 |
| S4 | PA17 | 中间探头 |
| S5 | PA18 | 右中探头 |
| S6 | PB18 | 右侧探头 |
| S7 | PB19 | 最右侧探头 |

循迹输入在 SysConfig 中配置为输入上拉。`g_line.active_low` 默认值为 `0`，即信号高电平表示检测到黑线或有效状态。如果你的循迹模块输出逻辑相反，可以在调试时把 `g_line.active_low` 改为 `1`。

循迹模块供电按模块要求接 5V 或 3.3V，但进入 MSPM0G3507 的信号必须不超过 3.3V。循迹模块、开发板、电机驱动和电池必须共地。

### 电机驱动与编码器

| 驱动板接口 | MSPM0G3507 引脚 | 说明 |
|---|---|---|
| PWMA | PB8 | 左电机 PWM，TIMA0.CCP0 |
| AIN1 | PA7 | 左电机方向 1 |
| AIN2 | PA28 | 左电机方向 2 |
| STBY | PA22 | TB6612 使能，高电平工作 |
| BIN1 | PA31 | 右电机方向 1 |
| BIN2 | PA21 | 右电机方向 2 |
| PWMB | PB9 | 右电机 PWM，TIMA0.CCP1 |
| E1A | PB6 | 左编码器 A 相，TIMG8 QEI |
| E1B | PB7 | 左编码器 B 相，TIMG8 QEI |
| E2A | PA0 | 右编码器 A 相，GPIO 中断 |
| E2B | PA1 | 右编码器 B 相，GPIO 中断 |
| GND | GND | 必须共地 |

电机动力线接驱动板白色电机接口：

- `MOTOR-A` 接左电机
- `MOTOR-B` 接右电机

如果电机方向或编码器方向与期望相反，优先通过下面变量调节：

- `g_car.left.invert_motor`
- `g_car.left.invert_encoder`
- `g_car.right.invert_motor`
- `g_car.right.invert_encoder`

默认值：

```c
g_car.left.invert_motor = 1;
g_car.left.invert_encoder = 0;
g_car.right.invert_motor = 1;
g_car.right.invert_encoder = 1;
```

## 工程结构

| 文件 | 说明 |
|---|---|
| `empty.c` | 主函数、初始化顺序、10ms 定时中断入口 |
| `empty.syscfg` | MSPM0G3507 引脚、PWM、QEI、GPIO 中断和定时器配置 |
| `car_control.c` | 电机控制、速度 PID、循迹闭环、编码器读取、PWM/方向输出 |
| `car_control.h` | 小车控制结构体、模式枚举和公共接口 |
| `line_tracker.c` | 七路循迹采样、加权误差、直角弯识别 |
| `line_tracker.h` | 循迹结构体和公共接口 |

## 初始化流程

主程序初始化顺序：

```c
SYSCFG_DL_init();
LineTracker_Init();
Car_Init();
```

随后写入默认运行参数：

```c
g_car.left.pid.kp = 180;
g_car.left.pid.ki = 0.35f;
g_car.left.pid.kd = 26;

g_car.right.pid.kp = 180;
g_car.right.pid.ki = 0.28f;
g_car.right.pid.kd = 18;

g_car.left.target_counts = 26;
g_car.right.target_counts = 26;
g_car.mode = CAR_MODE_LINE_FOLLOW;
```

控制周期由 `TIMG12` 产生，周期为 10ms。每次中断调用：

```c
Car_ControlStep();
```

## 控制变量

### `g_car`

`g_car` 是小车主控制变量，可用于观察状态和在线调参。

常用字段：

| 字段 | 说明 |
|---|---|
| `g_car.mode` | 当前运行模式 |
| `g_car.control_tick` | 10ms 控制周期计数 |
| `g_car.driver_enabled` | 驱动使能状态 |
| `g_car.left.target_counts` | 左轮目标编码器增量 |
| `g_car.right.target_counts` | 右轮目标编码器增量 |
| `g_car.left.measured_counts` | 左轮实测编码器增量 |
| `g_car.right.measured_counts` | 右轮实测编码器增量 |
| `g_car.left.pwm_output` | 左轮最终 PWM 输出 |
| `g_car.right.pwm_output` | 右轮最终 PWM 输出 |
| `g_car.line.base_counts` | 循迹基础速度 |
| `g_car.line.correction_counts` | 循迹修正量 |
| `g_car.line.left_target_counts` | 循迹计算后的左轮目标 |
| `g_car.line.right_target_counts` | 循迹计算后的右轮目标 |

### `g_line`

`g_line` 是循迹模块状态变量。

常用字段：

| 字段 | 说明 |
|---|---|
| `g_line.raw_mask` | 原始输入位图 |
| `g_line.active_mask` | 有效循迹位图 |
| `g_line.active_count` | 当前触发探头数量 |
| `g_line.line_seen` | 是否检测到线 |
| `g_line.error` | 当前循迹误差 |
| `g_line.sample_tick` | 循迹采样计数 |
| `g_line.active_low` | 是否低电平有效 |
| `g_line.right_angle_detected` | 是否检测到直角弯 |
| `g_line.right_angle_direction` | 直角弯方向，`1` 左，`-1` 右 |

单个探头触发时的默认误差：

| 通道 | `active_mask` | `error` |
|---|---:|---:|
| S1 | `0x01` | `50` |
| S2 | `0x02` | `33` |
| S3 | `0x04` | `16` |
| S4 | `0x08` | `0` |
| S5 | `0x10` | `-16` |
| S6 | `0x20` | `-33` |
| S7 | `0x40` | `-50` |

## 编译与下载

开发环境：

- CCS Theia
- MSPM0 SDK
- SysConfig
- TI Arm Clang
- MSPM0G3507 开发板

使用方法：

1. 用 CCS Theia 打开工程文件夹。
2. 确认 SysConfig 中芯片为 `MSPM0G3507`，封装为 `LQFP-64(PM)`。
3. 编译 Debug 配置。
4. 下载到开发板。
5. 打开 Ozone 或 CCS 调试窗口，观察 `g_car` 和 `g_line`。

## 上板验证

### 静态验证

1. 设置 `g_car.mode = 0`。
2. 确认：
   - `STBY = 0`
   - 左右 PWM 为 0
   - `AIN1/AIN2/BIN1/BIN2` 全为低电平
3. 依次遮挡 `S1..S7`，确认：
   - `g_line.active_mask` 分别为 `1,2,4,8,16,32,64`
   - `g_line.error` 分别为 `50,33,16,0,-16,-33,-50`
4. 同时触发 `S1~S4`，确认左直角弯增强。
5. 同时触发 `S4~S7`，确认右直角弯增强。

### 架空动态验证

将小车架空，避免车轮接触地面。

1. 确认 `g_car.control_tick` 每秒约增加 100。
2. 确认 `g_line.sample_tick` 每秒约增加 100。
3. 设置 `g_car.mode = 1`，测试开环 PWM 输出。
4. 设置 `g_car.mode = 2`，给左右轮正的 `target_counts`，确认：
   - 两个轮子朝前转
   - `measured_counts` 为正
5. 若方向不对，调整：
   - `invert_motor`
   - `invert_encoder`
6. 设置 `g_car.mode = 3`，测试循迹闭环：
   - `S1` 触发时，`right_target_counts > left_target_counts`
   - `S7` 触发时，`left_target_counts > right_target_counts`

## 调参建议

常用调参入口：

```c
g_car.line.base_counts
g_car.line.pid.kp
g_car.line.pid.ki
g_car.line.pid.kd
g_car.left.pid.kp
g_car.left.pid.ki
g_car.left.pid.kd
g_car.right.pid.kp
g_car.right.pid.ki
g_car.right.pid.kd
```

建议调试顺序：

1. 先确认电机方向和编码器方向正确。
2. 再调速度 PID，使左右轮在 `mode = 2` 下转速稳定。
3. 然后进入 `mode = 3`，从较低的 `base_counts` 开始循迹。
4. 根据偏航情况调整循迹 PID。
5. 根据场地弯道情况调整 `right_angle_error` 和基础速度。

## 安全注意事项

- 所有进入 MSPM0G3507 的信号必须不超过 3.3V。
- 主控、电机驱动、循迹模块、电池必须共地。
- 第一次上电建议架空车轮。
- 调试 PID 时先用低速度，确认方向正确后再提高速度。
- 电机电源电压按电机额定电压选择，当前默认适配 12V 电机系统。
