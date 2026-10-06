#include "DEV_Config.h"
#include "TSL2591.h"
#include "tsl2591_logic.h"

static UBYTE currentStage = tsl2591::DEFAULT_STAGE;
/******************************************************************************
function:	Read one byte of data to TSL2591 via I2C
parameter:
            Addr: Register address
Info:       Returns -1 on I2C failure
******************************************************************************/
static int TSL2591_Read_Byte(UBYTE Addr)
{
    Addr = Addr | COMMAND_BIT;
    return I2C_Read_Byte(Addr);
}

/******************************************************************************
function:	Read one word of data to TSL2591 via I2C
parameter:
            Addr: Register address
Info:       Returns -1 on I2C failure
******************************************************************************/
static int TSL2591_Read_Word(UBYTE Addr)
{
    Addr = Addr | COMMAND_BIT;
    return I2C_Read_Word(Addr);
}

/******************************************************************************
function:	Send one byte of data to TSL2591 via I2C
parameter:
            Addr: Register address
           Value: Write to the value of the register
Info:
******************************************************************************/
static void TSL2591_Write_Byte(UBYTE Addr, UBYTE Value)
{
    Addr = Addr | COMMAND_BIT;
    I2C_Write_Byte(Addr, Value);
}

/******************************************************************************
function:	Restart the integration
parameter:
Info:       Clearing AEN discards the running cycle and resets AVALID, so the
            next valid result is a complete integration with the current
            CONTROL settings.
******************************************************************************/
static void TSL2591_Restart(void)
{
    TSL2591_Write_Byte(ENABLE_REGISTER, ENABLE_POWERON);
    TSL2591_Write_Byte(ENABLE_REGISTER, ENABLE_POWERON | ENABLE_AEN);
}

/******************************************************************************
function:	Apply a sensitivity stage and restart the integration
parameter:
Info:
******************************************************************************/
static void TSL2591_Apply_Stage(UBYTE stage)
{
    const tsl2591::Stage &s = tsl2591::STAGES[stage];
    TSL2591_Write_Byte(ENABLE_REGISTER, ENABLE_POWERON);
    TSL2591_Write_Byte(CONTROL_REGISTER, s.gain_bits | s.atime_bits);
    TSL2591_Write_Byte(ENABLE_REGISTER, ENABLE_POWERON | ENABLE_AEN);
    currentStage = stage;
}

/******************************************************************************
function:	TSL2591 Initialization
parameter:
Info:       Returns 0 on success, 1 if the chip does not answer with its ID
******************************************************************************/
UBYTE TSL2591_Init(void)
{
    if (TSL2591_Read_Byte(ID_REGISTER) != TSL2591_CHIP_ID) {
        return 1;
    }
    TSL2591_Write_Byte(PERSIST_REGISTER, 0x01);//filter
    TSL2591_Apply_Stage(tsl2591::DEFAULT_STAGE);
    return 0;
}

/******************************************************************************
function:	Read the latest integration result (non-blocking)
parameter:
            lux: receives the illuminance in lx when TSL2591_OK is returned
Info:       Every call that finds a finished integration starts the next one,
            so polling at 1 Hz yields one value per second on every stage
            (longest integration: 600 ms).
******************************************************************************/
int TSL2591_Poll(float *lux)
{
    int status = TSL2591_Read_Byte(STATUS_REGISTER);
    if (status < 0) {
        return TSL2591_ERROR;
    }
    if (!(status & STATUS_AVALID)) {
        return TSL2591_NOT_READY;
    }
    int ch0 = TSL2591_Read_Word(CHAN0_LOW);
    int ch1 = TSL2591_Read_Word(CHAN1_LOW);
    if (ch0 < 0 || ch1 < 0) {
        return TSL2591_ERROR;
    }

    UBYTE stage = currentStage;
    bool reportable = tsl2591::isReportable(stage, (uint16_t)ch0, (uint16_t)ch1);
    if (reportable) {
        *lux = tsl2591::computeLux((uint16_t)ch0, (uint16_t)ch1, tsl2591::STAGES[stage]);
    }
    UBYTE next = tsl2591::nextStage(stage, (uint16_t)ch0, (uint16_t)ch1);
    if (next != stage) {
        TSL2591_Apply_Stage(next);
    } else {
        TSL2591_Restart();
    }
    return reportable ? TSL2591_OK : TSL2591_RANGING;
}

/******************************************************************************
function:	Current sensitivity stage
parameter:
Info:
******************************************************************************/
UBYTE TSL2591_Stage(void)
{
    return currentStage;
}
