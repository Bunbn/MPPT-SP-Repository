/*
  Incremental Conductance MPPT Controller
  Created: 4/21/2025
  By: Alex Aarnio

  V2 -> Battery
  V1 -> Solar Panel

  Using code adapted from:
    "Output Voltage Controller for Buck Converter"
    Created: 1/25/2023
    By: Elijah Gordon and Joshua Hutchinson

  IC algorithm based on:
    https://www.sciencedirect.com/science/article/pii/S1364032116305706
*/
// #define SENSOR_I_WINDOW_MAX 128
// #define SENSOR_V_WINDOW_MAX 128

#include <Arduino.h>
#include <AtverterH.h>

#define INTERRUPT_TIME 1000
#define DUTY_CYCLE_INCREMENT 1
#define VOLTAGE_ERROR_RANGE 10
#define CURRENT_ERROR_RANGE 10

#define LOW_SIDE_MAX_VOLTAGE 18000
#define LOW_SIDE_MAX_CURRENT 15000
#define HIGH_SIDE_MAX_CURRENT 15000

#define LOW_VOLTAGE_RESET 9000
#define HIGH_VOLTAGE_RESET 15000

#define DEBUG 0

AtverterH atverterH;

// Variables for buck control
int ledState = HIGH;
long slowInterruptCounter = 0;
uint16_t dutyCycle;

// Variables for IC
int32_t lowCurrent;
int32_t prevLowCurrent;
int32_t lowVoltage;
int32_t prevLowVoltage;

int32_t highCurrent;
int32_t highVoltage;

int32_t dV;
int32_t dI;

// Function prototypes
void setup();
void controlUpdate();
void transmitData();

void setup(void)
{
    atverterH.setupPinMode();                             // set pins to input or output
    atverterH.initializeSensors();                        // set filtered sensor values to initial reading
    atverterH.setCurrentShutdown1(LOW_SIDE_MAX_CURRENT);  // set gate shutdown at 6A peak current
    atverterH.setCurrentShutdown2(HIGH_SIDE_MAX_CURRENT); // set gate shutdown at 6A peak current
    atverterH.setThermalShutdown(60);                     // set gate shutdown at 60°C temperature

    dutyCycle = 50;
    atverterH.startPWM(dutyCycle);
    atverterH.initializeInterruptTimer(INTERRUPT_TIME, &controlUpdate); // Get interrupts enabled
    atverterH.applyHoldHigh2();                                         // hold side 2 high for a buck converter with side 1 input

    atverterH.startUART(); // send messages to computer via basic UART serial
}

void loop(void)
{
}

void controlUpdate(void)
{
    atverterH.updateVISensors();       // read voltage and current sensors and update moving average
    atverterH.checkCurrentShutdown();  // checks average current and shut down gates if necessary
    atverterH.checkThermalShutdown();  // checks switch temperature and shut down gates if necessary
    atverterH.checkBootstrapRefresh(); // refresh bootstrap capacitors on a timer
    // needed for buck or boost mode

    // check for overvoltage
    if (atverterH.getV2() > LOW_SIDE_MAX_VOLTAGE)
    {
        atverterH.shutdownGates(4);
        Serial.print("Low Side Overvoltage\n");
    }
    if (atverterH.isGateShutdown())
    // check if safety shutdown is active
    {
        Serial.print("Safety Shutoff Triggered\n");
        Serial.print("Shutdown Code: ");
        Serial.print(atverterH.getShutdownCode());
        Serial.print("\n");
    }
    else
    // if not in safety shutdown, continue
    {
        if((atverterH.getV2() < LOW_VOLTAGE_RESET) || (atverterH.getV2() > HIGH_VOLTAGE_RESET)) {
            // if outside of normal operating range, reset
            atverterH.setDutyCycle(50);
        }
         
        slowInterruptCounter++;
        if (slowInterruptCounter > 1000)
        // runs every 1000 interrupt calls (1 second)
        {
            slowInterruptCounter = 0;
            atverterH.updateVCC();            // read on-board VCC voltage, update stored average (shouldn't change)
            atverterH.updateTSensors();       // occasionally read thermistors and update temperature moving average
            atverterH.checkThermalShutdown(); // checks average temperature and shut down gates if necessary

            // toggle LED to show control loop is running
            atverterH.setLED(LED1_PIN, ledState);
            ledState = !ledState;

            // Perform IC operations
            // -----------------------------------------------------------------------------------------------------------------------------------

            // get sensor vales
            lowCurrent = -atverterH.getI2();
            lowVoltage = atverterH.getV2();
            highCurrent = atverterH.getI1();
            highVoltage = atverterH.getV1();

            // calculate derivatives
            dV = lowVoltage - prevLowVoltage;
            dI = lowCurrent - prevLowCurrent;

            if ((-VOLTAGE_ERROR_RANGE < dV) && (dV < VOLTAGE_ERROR_RANGE))
            {
#if DEBUG
                Serial.print("dV ~= 0\t");
#endif
                if (dI > CURRENT_ERROR_RANGE)
                {
#if DEBUG
                    Serial.print("dI ~> 0\t");
                    Serial.print("Duty cycle +\t");
#endif
                    dutyCycle += DUTY_CYCLE_INCREMENT; // inc. duty cycle
                }
                else if (dI < -CURRENT_ERROR_RANGE)
                {
#if DEBUG
                    Serial.print("dI ~< 0\t");
                    Serial.print("Duty cycle -\t");
#endif
                    dutyCycle += -DUTY_CYCLE_INCREMENT; // dec. duty cycle
                }
                else
                {
#if DEBUG
                    Serial.print("dI ~= 0\t");
                    Serial.print("Duty cycle 0\t");
#endif
                    dutyCycle += 0; // no change
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
                Serial.print((double)-lowCurrent / lowVoltage);
                Serial.print("\t");
#endif

                if (((double)dI / dV > -((double)lowCurrent / lowVoltage + CURRENT_ERROR_RANGE / VOLTAGE_ERROR_RANGE)) && ((double)dI / dV > -((double)lowCurrent / lowVoltage - CURRENT_ERROR_RANGE / VOLTAGE_ERROR_RANGE)))
                {
#if DEBUG
                    Serial.print("dI/dV ~> -avg\t");
                    Serial.print("Duty cycle +\t");
#endif
                    dutyCycle += DUTY_CYCLE_INCREMENT;
                }
                else if (((double)dI / dV < -((double)lowCurrent / lowVoltage + CURRENT_ERROR_RANGE / VOLTAGE_ERROR_RANGE)) && ((double)dI / dV < -((double)lowCurrent / lowVoltage - CURRENT_ERROR_RANGE / VOLTAGE_ERROR_RANGE)))
                {
#if DEBUG
                    Serial.print("dI/dV ~< -avg\t");
                    Serial.print("Duty cycle -\t");
#endif
                    dutyCycle += -DUTY_CYCLE_INCREMENT;
                }
                else
                {
#if DEBUG
                    Serial.print("dI/dV ~= -avg\t");
                    Serial.print("Duty cycle 0\t");
#endif
                    dutyCycle += 0;
                }
            }
#if DEBUG
            Serial.print("\r\n");
#endif
            // save previous values for voltage/current
            prevLowCurrent = lowCurrent;
            prevLowVoltage = lowVoltage;

            atverterH.setDutyCycle(dutyCycle); // set new duty cycle
            transmitData();                    // send relevent data over UART
        }
    }
}

void transmitData()
{
    Serial.print("LowSideVoltage: ");
    Serial.print(lowVoltage);
    Serial.print("\t");

    Serial.print("LowSideCurrent: ");
    Serial.print(lowCurrent);
    Serial.print("\t");

    Serial.print("LowSidePower: ");
    Serial.print(lowVoltage * lowCurrent / 1000);
    Serial.print("\t");

    Serial.print("HighSideVoltage: ");
    Serial.print(highVoltage);
    Serial.print("\t");

    Serial.print("HighSideCurrent: ");
    Serial.print(highCurrent);
    Serial.print("\t");

    Serial.print("HighSidePower: ");
    Serial.print(highVoltage * highCurrent / 1000);
    Serial.print("\t");

    Serial.print("DutyCycle: ");
    Serial.print(atverterH.getDutyCycle());
    Serial.print("\t");

    Serial.print("\r\n");

#if DEBUG
    Serial.print("DEBUG info: \t");

    Serial.print("dV: ");
    Serial.print(dV);
    Serial.print("\t");

    Serial.print("dI: ");
    Serial.print(dI);
    Serial.print("\t");

    Serial.println("-------------------------------------------------------------------------------------------------------");

#endif
}