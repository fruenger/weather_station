#ifndef _TSL2591_H_
#define _TSL2591_H_

#include "DEV_Config.h"

#define TSL2591_ADDRESS       (0x29)
#define TSL2591_CHIP_ID       (0x50)

#define COMMAND_BIT           (0xA0)
//Register (0x00)
#define ENABLE_REGISTER       (0x00)
#define ENABLE_POWERON        (0x01)
#define ENABLE_POWEROFF       (0x00)
#define ENABLE_AEN            (0x02)
#define ENABLE_AIEN           (0x10)
#define ENABLE_SAI            (0x40)
#define ENABLE_NPIEN          (0x80)

#define CONTROL_REGISTER      (0x01)
#define SRESET                (0x80)

#define PERSIST_REGISTER      (0x0C)
#define ID_REGISTER           (0x12)
#define STATUS_REGISTER       (0x13) // read only
#define STATUS_AVALID         (0x01)

#define CHAN0_LOW             (0x14)
#define CHAN0_HIGH            (0x15)
#define CHAN1_LOW             (0x16)
#define CHAN1_HIGH            (0x17)

// Result of TSL2591_Poll()
#define TSL2591_OK            (0)  // new lux value written
#define TSL2591_NOT_READY     (1)  // integration still running
#define TSL2591_RANGING       (2)  // reading discarded, sensitivity changed
#define TSL2591_ERROR         (3)  // I2C failure
/***********************************************************************************/
// Checks the chip ID and starts continuous integration. Returns 0 on success.
UBYTE TSL2591_Init(void);
// Non-blocking: reads the latest finished integration, adjusts gain/integration
// time for the next one and writes the lux value (float, >= 0) on TSL2591_OK.
int TSL2591_Poll(float *lux);
// Index of the current sensitivity stage (see tsl2591_logic.h)
UBYTE TSL2591_Stage(void);
#endif
