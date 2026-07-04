/*
 * MSPM0G3507 line-tracking car application.
 */

#include "ti_msp_dl_config.h"
#include "car_control.h"
#include "line_tracker.h"

int main(void)
{
    SYSCFG_DL_init();

    LineTracker_Init();
    Car_Init();

    g_car.left.pid.kp = 180;
    g_car.left.pid.ki = 0.35f;
    g_car.left.pid.kd = 26;
    g_car.right.pid.kp = 180;
    g_car.right.pid.ki = 0.28f;
    g_car.right.pid.kd = 18;

    g_car.left.invert_motor = 1U;
    g_car.left.invert_encoder = 0U;
    g_car.right.invert_motor = 1U;
    g_car.right.invert_encoder = 1U;

    g_car.left.target_counts = 26;
    g_car.right.target_counts = 26;
    g_car.mode = CAR_MODE_LINE_FOLLOW;

    while (1) {
        __WFI();
    }
}

void TIMER_CONTROL_INST_IRQHandler(void)
{
    switch (DL_TimerG_getPendingInterrupt(TIMER_CONTROL_INST)) {
        case DL_TIMER_IIDX_ZERO:
            Car_ControlStep();
            break;
        default:
            break;
    }
}
