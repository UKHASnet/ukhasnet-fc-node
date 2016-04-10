/*
    ChibiOS - Copyright (C) 2006..2015 Giovanni Di Sirio

    Licensed under the Apache License, Version 2.0 (the "License");
    you may not use this file except in compliance with the License.
    You may obtain a copy of the License at

        http://www.apache.org/licenses/LICENSE-2.0

    Unless required by applicable law or agreed to in writing, software
    distributed under the License is distributed on an "AS IS" BASIS,
    WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
    See the License for the specific language governing permissions and
    limitations under the License.
*/

#include "hal.h"
#include "nil.h"

#define HTU_ADDR        0x40
#define HTU_READ_TEMP   0xE3
#define HTU_READ_HUMID  0xE5

/* Specify this 7 bit address as right-aligned */
#define RADIO_ADDR      0x70

/* Radio commands */
#define RADIO_GET_REV   0x10

static uint8_t htu_tx;
static uint8_t htu_buf[3];
static uint8_t radio_tx;
static uint8_t radio_buf[12];

/*
 * I2C2 config. See F0x0 refman.
 */
static const I2CConfig i2c_config = { 
    STM32_TIMINGR_PRESC(1U) |
        STM32_TIMINGR_SCLDEL(4U) | STM32_TIMINGR_SDADEL(2U) |
        STM32_TIMINGR_SCLH(15U)  | STM32_TIMINGR_SCLL(19U),
    0,  
    0
};

/*
 * Thread 1.
 */
THD_WORKING_AREA(waThread1, 256);
THD_FUNCTION(Thread1, arg) {
    (void)arg;

    // Wait for hardware to start
    chThdSleepMilliseconds(100);

    // Configure I2C
    i2cStart(&I2CD1, &i2c_config);

    // Radio SHDN low
    palSetPadMode(GPIOA, GPIOA_RADIO_SHDN, PAL_MODE_OUTPUT_PUSHPULL);
    palClearPad(GPIOA, GPIOA_RADIO_SHDN);

    while(true)
    {
        // Sensors
        /*
        htu_tx = HTU_READ_TEMP;
        i2cMasterTransmitTimeout(&I2CD1, HTU_ADDR, &htu_tx, 1,
                htu_buf, 3, TIME_INFINITE);
        */

        // Radio
        radio_tx = RADIO_GET_REV;
        i2cMasterTransmitTimeout(&I2CD1, RADIO_ADDR, &radio_tx, 1,
                radio_buf, 11, TIME_INFINITE);

        // Sleep
        chThdSleepMilliseconds(500);
    }
}

/*
 * Threads static table, one entry per thread. The number of entries must
 * match NIL_CFG_NUM_THREADS.
 */
THD_TABLE_BEGIN
  THD_TABLE_ENTRY(waThread1, "thd1", Thread1, NULL)
THD_TABLE_END

/*
 * Application entry point.
 */
int main(void) {

    /*
     * System initializations.
     * - HAL initialization, this also initializes the configured device drivers
     *   and performs the board-specific initializations.
     * - Kernel initialization, the main() function becomes a thread and the
     *   RTOS is active.
     */
    halInit();
    chSysInit();

    /* This is now the idle thread loop, you may perform here a low priority
       task but you must never try to sleep or wait in this loop. Note that
       this tasks runs at the lowest priority level so any instruction added
       here will be executed after all other tasks have been started.*/
    while (true) {
    }
}
