#include "configuration.h"

#if !MESHTASTIC_EXCLUDE_ENVIRONMENTAL_SENSOR && defined(ARCH_ESP32)

#include "AnalogSensor.h"
#include <driver/adc.h>

#define NUM_SAMPLES 10  // Average 10 readings for stability

// ========== Constructors and Factory Methods ==========

AnalogSensor::AnalogSensor(const AnalogSensorConfig& cfg)
    : TelemetrySensor(meshtastic_TelemetrySensorType_SENSOR_UNSET, cfg.sensorName)
{
    config = cfg;
    LOG_INFO("AnalogSensor created: %s (type=%d, pin=GPIO%d)", 
             config.sensorName, config.calcType, config.adcPin);
}

AnalogSensor* AnalogSensor::createTDSSensor(uint8_t pin, float temp)
{
    AnalogSensorConfig cfg = {
        // ADC Configuration
        .adcPin = pin,
        .adcBitDepth = 12,           // ESP32 default: 12-bit
        .adcVref = 3.3,              // Standard ESP32 reference voltage
        .voltageCalibration = 1.0,   // Default - calibrate with multimeter
        
        // Calculation type
        .calcType = CUBIC_TDS,
        
        // Linear parameters (not used for TDS)
        .linearSlope = 1.0,
        .linearOffset = 0.0,
        
        // TDS cubic formula (from manufacturer datasheet)
        .cubicCoefA = 133.42,
        .cubicCoefB = -255.86,       // ⚠️ NEGATIVE number!
        .cubicCoefC = 857.39,
        .cubicMultiplier = 0.5,      // Calibrate: expected_tds / (term1 + term2 + term3)
        .temperatureC = temp,
        
        // Output calibration
        .outputOffset = 0.0,         // Default - adjust to zero reading in air
        
        // Metadata
        .sensorName = "TDS",
        .units = "PPM"
    };
    
    return new AnalogSensor(cfg);
}

AnalogSensor* AnalogSensor::createSoilMoistureSensor(uint8_t pin)
{
    AnalogSensorConfig cfg = {
        // ADC Configuration
        .adcPin = pin,
        .adcBitDepth = 12,           // ESP32-S3 uses 12-bit (some soil sensors spec 10-bit but we use 12)
        .adcVref = 3.3,
        .voltageCalibration = 1.0,
        
        // Calculation type
        .calcType = INVERSE_MOISTURE,
        
        // Linear parameters (high voltage = dry = 0%, low voltage = wet = 100%)
        .linearSlope = -30.303,      // Approximate: (0 - 100) / (3.3 - 0) = -30.303
        .linearOffset = 100.0,       // At 0V = 100% moisture
        
        // TDS parameters (not used)
        .cubicCoefA = 0,
        .cubicCoefB = 0,
        .cubicCoefC = 0,
        .cubicMultiplier = 1.0,
        .temperatureC = 25.0,
        
        // Output calibration
        .outputOffset = 0.0,
        
        // Metadata
        .sensorName = "Soil",
        .units = "%"
    };
    
    return new AnalogSensor(cfg);
}

AnalogSensor* AnalogSensor::createLinearSensor(uint8_t pin, float slope, float offset, const char* name)
{
    AnalogSensorConfig cfg = {
        .adcPin = pin,
        .adcBitDepth = 12,
        .adcVref = 3.3,
        .voltageCalibration = 1.0,
        .calcType = LINEAR,
        .linearSlope = slope,
        .linearOffset = offset,
        .cubicCoefA = 0,
        .cubicCoefB = 0,
        .cubicCoefC = 0,
        .cubicMultiplier = 1.0,
        .temperatureC = 25.0,
        .outputOffset = 0.0,
        .sensorName = name,
        .units = "units"
    };
    
    return new AnalogSensor(cfg);
}

// ========== Initialization ==========

bool AnalogSensor::initDevice(TwoWire *bus, ScanI2C::FoundDevice *dev)
{
    LOG_INFO("Init sensor: %s on GPIO%d (Heltec V3 pin label: %s)", 
             sensorName, config.adcPin, 
             (config.adcPin == 7) ? "18 - RECOMMENDED" : "varies");
    
    if (config.adcPin == 6) {
        LOG_WARN("GPIO6 may have stability issues - GPIO7 (pin 18) is recommended");
    }
    
    // Configure ADC width based on bit depth
    // Note: ESP32-S3 (Heltec V3) only supports 12-bit and 13-bit
    // ESP32 classic supports 9, 10, 11, 12 bits
    adc_bits_width_t width;
    switch (config.adcBitDepth) {
        case 12: 
            width = ADC_WIDTH_BIT_12; 
            break;
        case 13:
#ifdef ADC_WIDTH_BIT_13
            width = ADC_WIDTH_BIT_13;  // ESP32-S3 only
#else
            LOG_WARN("13-bit ADC not supported on this chip, using 12-bit");
            width = ADC_WIDTH_BIT_12;
            config.adcBitDepth = 12;  // Update config to reflect reality
#endif
            break;
        case 9:
        case 10:
        case 11:
            // These are only supported on ESP32 classic, not S3
            LOG_WARN("%d-bit ADC not supported on ESP32-S3, using 12-bit", config.adcBitDepth);
            width = ADC_WIDTH_BIT_12;
            config.adcBitDepth = 12;  // Update config
            break;
        default:
            LOG_ERROR("Invalid ADC bit depth %d. ESP32-S3 supports 12 or 13 bits", config.adcBitDepth);
            return false;
    }
    adc1_config_width(width);
    
    // Map GPIO to ADC channel for Heltec V3
    adc1_channel_t channel;
    switch (config.adcPin) {
        case 1: channel = ADC1_CHANNEL_0; break;
        case 2: channel = ADC1_CHANNEL_1; break;
        case 4: channel = ADC1_CHANNEL_3; break;
        case 5: channel = ADC1_CHANNEL_4; break;
        case 6: channel = ADC1_CHANNEL_5; break;
        case 7: channel = ADC1_CHANNEL_6; break;  // RECOMMENDED for Heltec V3
        default:
            LOG_ERROR("Invalid ADC pin %d. Valid: 1,2,4,5,6,7 (GPIO7/pin18 recommended)", config.adcPin);
            return false;
    }
    
    // Configure for full 0-3.3V range
    adc1_config_channel_atten(channel, ADC_ATTEN_DB_12);
    
    status = true;
    initialized = true;
    LOG_INFO("%s sensor initialized successfully (%d-bit ADC)", sensorName, config.adcBitDepth);
    return true;
}

// ========== Reading and Calculation ==========

float AnalogSensor::readVoltage()
{
    // Map GPIO to channel
    adc1_channel_t channel;
    switch (config.adcPin) {
        case 1: channel = ADC1_CHANNEL_0; break;
        case 2: channel = ADC1_CHANNEL_1; break;
        case 4: channel = ADC1_CHANNEL_3; break;
        case 5: channel = ADC1_CHANNEL_4; break;
        case 6: channel = ADC1_CHANNEL_5; break;
        case 7: channel = ADC1_CHANNEL_6; break;
        default: return -1.0;  // Error
    }
    
    // Average multiple readings for stability
    uint32_t sum = 0;
    int validReadings = 0;
    
    for (int i = 0; i < NUM_SAMPLES; i++) {
        int raw = adc1_get_raw(channel);
        if (raw < 0) {
            LOG_WARN("ADC read failed on attempt %d", i);
            continue;  // Skip this reading but try others
        }
        sum += raw;
        validReadings++;
        delay(10);
    }
    
    // If we got NO valid readings, that's an error
    if (validReadings == 0) {
        LOG_ERROR("All ADC readings failed - sensor disconnected?");
        return -1.0;
    }
    
    float avgRaw = sum / (float)validReadings;
    float maxValue = (1 << config.adcBitDepth) - 1;  // e.g., 4095 for 12-bit
    
    // Calculate voltage
    float uncalibratedVoltage = (avgRaw / maxValue) * config.adcVref;
    float voltage = uncalibratedVoltage * config.voltageCalibration;
    
    LOG_INFO("ADC: Raw=%d, MaxVal=%.0f, Vref=%.2f, Uncal=%.3fV, Cal=%.3fV (factor=%.3f)", 
             (int)avgRaw, maxValue, config.adcVref, uncalibratedVoltage, voltage, config.voltageCalibration);
    
    return voltage >= 0.0 ? voltage : 0.0;
}

float AnalogSensor::calculateValue(float voltage)
{
    float result = 0.0;
    
    switch (config.calcType) {
        case LINEAR:
            // Simple linear: output = (voltage * slope) + offset
            result = (voltage * config.linearSlope) + config.linearOffset;
            LOG_INFO("%s Calc (LINEAR): V=%.3f, Slope=%.3f, Offset=%.3f, Result=%.2f %s",
                     sensorName, voltage, config.linearSlope, config.linearOffset, result, config.units);
            break;
            
        case INVERSE_MOISTURE:
            // Inverse relationship: high voltage = low moisture
            // Same formula as LINEAR but conceptually different
            result = (voltage * config.linearSlope) + config.linearOffset;
            // Clamp to 0-100%
            result = max(0.0f, min(100.0f, result));
            LOG_INFO("%s Calc (INVERSE): V=%.3f, Moisture=%.1f%%", sensorName, voltage, result);
            break;
            
        case CUBIC_TDS: {
            // Temperature compensation
            float compensationCoefficient = 1.0 + 0.02 * (config.temperatureC - 25.0);
            float compensationVoltage = voltage / compensationCoefficient;
            
            // Cubic polynomial: TDS = (A*V³ + B*V² + C*V) * multiplier
            // ⚠️ NOTE: B (term2) is NEGATIVE (-255.86) - this is correct!
            float term1 = config.cubicCoefA * compensationVoltage * compensationVoltage * compensationVoltage;
            float term2 = config.cubicCoefB * compensationVoltage * compensationVoltage;  // NEGATIVE!
            float term3 = config.cubicCoefC * compensationVoltage;
            result = (term1 + term2 + term3) * config.cubicMultiplier;
            
            LOG_INFO("TDS Calc: Temp=%.1f°C, CompCoef=%.4f, Vin=%.3fV, Vcomp=%.3fV", 
                     config.temperatureC, compensationCoefficient, voltage, compensationVoltage);
            LOG_INFO("TDS Calc: Term1=%.2f, Term2=%.2f (NEGATIVE!), Term3=%.2f, Multiplier=%.4f", 
                     term1, term2, term3, config.cubicMultiplier);
            LOG_INFO("TDS Calc: Raw result=%.1f PPM (before offset)", result);
            break;
        }
            
        case CUSTOM:
            // Reserved for future use
            LOG_WARN("CUSTOM calculation not implemented");
            result = voltage;  // Fallback to voltage
            break;
    }
    
    return result;
}

// ========== Metrics Collection ==========

bool AnalogSensor::getMetrics(meshtastic_Telemetry *measurement)
{
    float voltage = readVoltage();
    
    // Check for error condition (negative voltage means sensor error)
    if (voltage < 0.0) {
        LOG_ERROR("%s sensor error - cannot read voltage", sensorName);
        return false;
    }
    
    // Calculate sensor value
    float rawValue = calculateValue(voltage);
    
    // Apply output offset calibration
    float calibratedValue = max(0.0f, rawValue - config.outputOffset);
    
    LOG_INFO("%s: Voltage=%.3fV, Raw=%.2f, Offset=%.2f, Final=%.2f %s", 
             sensorName, voltage, rawValue, config.outputOffset, calibratedValue, config.units);
    
    // ========== Determine which telemetry variant we're filling ==========
    // Check if this is air_quality_metrics or environment_metrics
    bool isAirQuality = (measurement->which_variant == meshtastic_Telemetry_air_quality_metrics_tag);
    
    if (isAirQuality) {
        // ========== AIR QUALITY METRICS (actually gets logged!) ==========
        LOG_INFO("Writing to air_quality_metrics (PM2.5 field)");
        measurement->variant.air_quality_metrics.has_pm25_standard = true;
        measurement->variant.air_quality_metrics.pm25_standard = (uint32_t)calibratedValue;
        // Also store voltage in PM1.0 field
        measurement->variant.air_quality_metrics.has_pm10_standard = true;
        measurement->variant.air_quality_metrics.pm10_standard = (uint32_t)(voltage * 1000);  // mV
    } else {
        // ========== ENVIRONMENT METRICS (may not be logged) ==========
        // Always store raw voltage
        measurement->variant.environment_metrics.has_voltage = true;
        measurement->variant.environment_metrics.voltage = voltage;
        
        // Store sensor value in current field (default for backwards compatibility)
        measurement->variant.environment_metrics.has_current = true;
        measurement->variant.environment_metrics.current = calibratedValue;
    }
    
    return true;  // Success
}

#endif
