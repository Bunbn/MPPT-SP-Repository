/*
  Incremental Conductance MPPT Controller
  Created: 4/21/2025
  By: Alex Aarnio

  Using code adapted from:
    "Output Voltage Controller for Buck Converter"
    Created: 1/25/2023
    By: Elijah Gordon and Joshua Hutchinson

  IC algorithm based on:
    https://www.sciencedirect.com/science/article/pii/S1364032116305706
*/

#include <Arduino.h>
#include <AtverterE.h>

#define INTERRUPT_TIME 1
#define NUM_AVERAGES 1000
#define DUTY_CYCLE_INCREMENT 20
#define VOLTAGE_ERROR_RANGE 10
#define CURRENT_ERROR_RANGE 20
#define LOW_SIDE_MAX_VOLTAGE 15000

#define DEBUG 1
#define SAFETY_ENABLE 0

AtverterE atverterE;
int ledState = HIGH;

// Variables for buck control
uint16_t dutyCycle;

// Safety shutoff flag
bool VOLTAGE_SAFETY = 0;

// Variables for averaging
int16_t avgCount = 0;

int avgLowCurrentSum;
int avgHighCurrentSum;
int32_t avgLowVoltageSum;
int32_t avgHighVoltageSum;

int avgLowCurrent;
int avgPrevLowCurrent;
int32_t avgLowVoltage;
int32_t avgPrevLowVoltage;

int avgHighCurrent;
int32_t avgHighVoltage;

// Variables for IC algorithm
int32_t dV;
int dI;

// Voltage sensor calibration
const double VL_scale  = 1.00;
const double VL_offset = 36;
const double VH_scale  = 0.98;
const double VH_offset = 0;

// Function prototypes
void setup();
void controlUpdate();
void transmitData();
int32_t getCalibratedVH();
int32_t getCalibratedVL();

void setup(void)
{
    atverterE.setupPinMode();       // Get pins setup
    atverterE.initializePWMTimer(); // Setup Timers

    atverterE.initializeInterruptTimer(INTERRUPT_TIME, &controlUpdate); // Get interrupts enabled
    Serial.begin(9600);

    dutyCycle = 512;    // set initial 50% duty cycle, quickly changed by IC algorithm
    atverterE.setDutyCycle(dutyCycle);
    atverterE.startPWM();
}

void loop(void)
{
}

void controlUpdate(void)
{
#if SAFETY_ENABLE
    if (VOLTAGE_SAFETY)
    {
        dutyCycle = ((12000 / getCalibratedVH()) * 1024); // set duty cycle to get roughly 12V on battery side
        atverterE.setDutyCycle(dutyCycle);
        Serial.print("Safety Shutoff Triggered\n");
        return;
    }
    else
#endif
    {
        // Store data for averaging
        // -----------------------------------------------------------------------------------------------------------------------------------
        avgLowCurrentSum += atverterE.getIL();
        avgHighCurrentSum += atverterE.getIH();

        avgLowVoltageSum += getCalibratedVL();
        avgHighVoltageSum += getCalibratedVH();

        avgCount++;

        // toggle LED to show control loop is running
        atverterE.setLED(LED1G_PIN, ledState);
        ledState = !ledState;

        // Perform IC operations
        // -----------------------------------------------------------------------------------------------------------------------------------
        if (avgCount == NUM_AVERAGES)
        { // perform full IC operation

            // calculate current averages
            avgLowCurrent = avgLowCurrentSum / (NUM_AVERAGES + 1);
            avgLowVoltage = avgLowVoltageSum / (NUM_AVERAGES + 1);

            avgHighCurrent = avgHighCurrentSum / (NUM_AVERAGES + 1);
            avgHighVoltage = avgHighVoltageSum / (NUM_AVERAGES + 1);

            dV = avgLowVoltage - avgPrevLowVoltage;
            dI = avgLowCurrent - avgPrevLowCurrent;

            if (avgLowVoltage > LOW_SIDE_MAX_VOLTAGE)
            {
                VOLTAGE_SAFETY = 1;
                Serial.print("Low Side Overvoltage\n");
            }

            if ((-VOLTAGE_ERROR_RANGE < dV) && (dV < VOLTAGE_ERROR_RANGE))
            {
#if DEBUG
                Serial.print("dV ~= 0\t");
#endif
                if (dI > CURRENT_ERROR_RANGE)
                {
#if DEBUG
                    Serial.print("dI ~> 0\t");
#endif
                    dutyCycle += DUTY_CYCLE_INCREMENT; // inc. duty cycle
                }
                else if (dI < -CURRENT_ERROR_RANGE)
                {
#if DEBUG
                    Serial.print("dI ~< 0\t");
#endif
                    dutyCycle += -DUTY_CYCLE_INCREMENT; // dec. duty cycle
                }
                else
                {
#if DEBUG
                    Serial.print("dI ~= 0\t");
#endif
                    // dutyCycle += 0; // no change
                }
            }
            else
            {
#if DEBUG
                Serial.print("dV != 0\t");

                Serial.print("dI/dV = ");
                Serial.print((double)dI / dV);
                Serial.print("\t");

                Serial.print("avgI/avgV = ");
                Serial.print((double)-avgLowCurrent / avgLowVoltage);
                Serial.print("\t");
#endif

                if (((double)dI / dV > -((double)avgLowCurrent / avgLowVoltage + CURRENT_ERROR_RANGE / VOLTAGE_ERROR_RANGE)) && ((double)dI / dV > -((double)avgLowCurrent / avgLowVoltage - CURRENT_ERROR_RANGE / VOLTAGE_ERROR_RANGE)))
                {
#if DEBUG
                    Serial.print("dI/dV ~> -avg\t");
#endif
                    dutyCycle += DUTY_CYCLE_INCREMENT;
                }
                else if (((double)dI / dV < -((double)avgLowCurrent / avgLowVoltage + CURRENT_ERROR_RANGE / VOLTAGE_ERROR_RANGE)) && ((double)dI / dV < -((double)avgLowCurrent / avgLowVoltage - CURRENT_ERROR_RANGE / VOLTAGE_ERROR_RANGE)))
                {
#if DEBUG
                    Serial.print("dI/dV ~< -avg\t");
#endif
                    dutyCycle += -DUTY_CYCLE_INCREMENT;
                }
                else
                {
#if DEBUG
                    Serial.print("dI/dV ~= -avg\t");
#endif
                    // dutyCycle += 0;
                }
            }
            Serial.print("\r\n");
            avgCount = 0; // reset count

            // save previous values for voltage/current
            avgPrevLowCurrent = avgLowCurrent;
            avgPrevLowVoltage = avgLowVoltage;

            // reset sums for average calcs
            avgLowCurrentSum = 0;
            avgLowVoltageSum = 0;
            avgHighCurrentSum = 0;
            avgHighVoltageSum = 0;

            dutyCycle = constrain(dutyCycle, 10, 1023);
            atverterE.setDutyCycle(dutyCycle);

            transmitData(); // send relevent data over UART
        }
    }
}

void transmitData()
{
    Serial.print("LowSideVoltage: ");
    Serial.print(avgLowVoltage);
    Serial.print("\t");

    Serial.print("LowSideCurrent: ");
    Serial.print(avgLowCurrent);
    Serial.print("\t");

    Serial.print("HighSideVoltage: ");
    Serial.print(avgHighVoltage);
    Serial.print("\t");

    Serial.print("HighSideCurrent: ");
    Serial.print(avgHighCurrent);
    Serial.print("\t");
    Serial.print("\r\n");

#if DEBUG
    Serial.print("DutyCycle: ");
    Serial.print(dutyCycle);
    Serial.print("\t");

    Serial.print("dV: ");
    Serial.print(dV);
    Serial.print("\t");

    Serial.print("dI: ");
    Serial.print(dI);
    Serial.print("\t");

    Serial.print("dV: ");
    Serial.print(dV);
    Serial.print("\r\n");
#endif

    Serial.print("------------------------------------------------------------------------\n");
}

int32_t getCalibratedVH()
{
    return (int32_t)(((double)atverterE.getActualVH() * VH_scale) + VH_offset);
}

int32_t getCalibratedVL()
{
    return (int32_t)(((double)atverterE.getActualVL() * VL_scale) + VL_offset);
}