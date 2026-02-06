#pragma once
#include "configuration.h"

#if !MESHTASTIC_EXCLUDE_ENVIRONMENTAL_SENSOR && defined(ARCH_ESP32)

#include "../mesh/generated/meshtastic/telemetry.pb.h"
#include "TelemetrySensor.h"

/**
 * Generic Analog Sensor for ESP32 ADC
 * 
 * Supports various analog sensors with different ADC resolutions and calibration formulas.
 * 
 * Hardware Notes (Heltec V3):
 * - RECOMMENDED: GPIO7 (ADC1_CH6, board label "18") - Most reliable
 * - GPIO6 may have issues - avoid if possible
 * - Other options: GPIO1, 2, 4, 5 (see datasheet for ADC channel mapping)
 */

// Sensor calculation types
enum AnalogCalcType {
    LINEAR,           // Simple linear: output = (voltage * slope) + offset
    CUBIC_TDS,        // TDS sensor cubic polynomial formula
    INVERSE_MOISTURE, // Soil moisture (high voltage = low moisture %)
    CUSTOM            // Reserved for user-defined formulas
};

// Configuration structure for analog sensors
struct AnalogSensorConfig {
    // === ADC Hardware Configuration ===
    uint8_t adcPin;              // GPIO pin number (1,2,4,5,6,7 for Heltec V3 ADC1)
    uint8_t adcBitDepth;         // ADC resolution: 10, 11, or 12 bits (ESP32 supports all)
    float adcVref;               // Reference voltage in volts (typically 3.3V)
    float voltageCalibration;    // ADC calibration multiplier (default: 1.0)
                                 // How to calibrate: measure voltage with multimeter (DMM)
                                 // voltageCalibration = DMM_reading / ESP32_reading
    
    // === Calculation Type ===
    AnalogCalcType calcType;     // Which formula to use for converting voltage to output
    
    // === Linear Mode Parameters (for LINEAR, INVERSE_MOISTURE) ===
    float linearSlope;           // Slope/multiplier (default: 1.0)
    float linearOffset;          // Y-intercept/offset (default: 0.0)
                                 // Formula: output = (voltage * slope) + offset
    
    // === TDS Cubic Formula Parameters (for CUBIC_TDS) ===
    float cubicCoefA;            // Cubic coefficient (default: 133.42)
    float cubicCoefB;            // Quadratic coefficient (default: -255.86)
                                 // ⚠️ NOTE: This is NEGATIVE - don't forget the minus sign!
    float cubicCoefC;            // Linear coefficient (default: 857.39)
    float cubicMultiplier;       // Final multiplier (default: 0.5)
                                 // How to calibrate multiplier:
                                 // 1. Measure TDS with calibrated external meter
                                 // 2. Calculate: cubicMultiplier = expected_tds / (term1 + term2 + term3)
                                 //    where term1 = A*V³, term2 = B*V², term3 = C*V
    float temperatureC;          // Water temperature for compensation (default: 25.0°C)
    
    // === Output Calibration (all modes) ===
    float outputOffset;          // Subtract from final result (default: 0.0)
                                 // Used to zero-offset the sensor (e.g., reading in air)
    
    // === Metadata ===
    const char* sensorName;      // Display name (e.g., "TDS", "Soil Moisture")
    const char* units;           // Units for logging (e.g., "PPM", "%", "mV")
};

class AnalogSensor : public TelemetrySensor
{
  private:
    AnalogSensorConfig config;
    
    float readVoltage();
    float calculateValue(float voltage);
    
  public:
    // Constructor with custom configuration
    AnalogSensor(const AnalogSensorConfig& cfg);
    
    // Factory methods for common sensors
    static AnalogSensor* createTDSSensor(uint8_t pin = 7, float temp = 25.0);
    static AnalogSensor* createSoilMoistureSensor(uint8_t pin = 7);
    static AnalogSensor* createLinearSensor(uint8_t pin, float slope, float offset, 
                                           const char* name = "Analog");
    
    // TelemetrySensor interface
    virtual bool getMetrics(meshtastic_Telemetry *measurement) override;
    virtual bool initDevice(TwoWire *bus, ScanI2C::FoundDevice *dev) override;
};

#endif
