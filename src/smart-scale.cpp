/******************************************************/
//       THIS IS A GENERATED FILE - DO NOT EDIT       //
/******************************************************/

#include "Particle.h"
#line 1 "/Users/manuelorduno/Documents/GitHub/smart-scale/src/smart-scale.ino"
/*
 * Project smart-scale
 * Description: Keg monitoring project using Particle's Photon 2
 * Author: Manuel Orduño
 * Date: 06/21/2023
 */
#include "loadcell2.h"
#include "photon-thermistor.h"

// required when working with Particle Products, learn more here:
// https://docs.particle.io/getting-started/products/introduction-to-products/#product-firmware-workflow
void setup();
void loop();
#line 12 "/Users/manuelorduno/Documents/GitHub/smart-scale/src/smart-scale.ino"
PRODUCT_VERSION(2);

SYSTEM_THREAD(ENABLED);
SYSTEM_MODE(SEMI_AUTOMATIC);

SerialLogHandler logHandler(LOG_LEVEL_WARN, { // Logging level for non-application messages
    { "app", LOG_LEVEL_ALL } // Logging level for application messages
});

const std::chrono::milliseconds readPeriod = 5s;
const std::chrono::milliseconds publishPeriod = 6min;
unsigned long lastRead;
unsigned long lastPublish;

static loadcell2_t loadcell2;
loadcell2_data_t cell_data;
static float weight_gr;
const int tareOffsetAddr = 0;
const int weightRefAddr = 10;
const int calibrationFactorAddr = 20;
bool calibrateScale = false;
bool tareScale = false;
uint16_t weightRef;

Thermistor *thermistor;
float temp_c;

// Particle variables must be double, not float
double weightGr;
double tempC;

char buf[1024];

// function declarations
int calibrate_scale(String cmd);
void loadcell2_init();
void publishToCloud();
void readCalibrationSettings();
int tare_scale(String cmd);

// setup() runs once, when the device is first turned on
// put initialization like pinMode and begin functions here
void setup()
{
  waitFor(Serial.isConnected, 10000);
  delay(1000);
  
  thermistor = new Thermistor(A1, 10000, 10000, 25, 3984, 5, 20);

  loadcell2_init();
  readCalibrationSettings();

  Particle.variable("weight", weightGr);
  Particle.variable("temperature", tempC);
  Particle.function("tare", tare_scale);
  Particle.function("calibrate", calibrate_scale);

  Particle.connect();
}

// loop() runs over and over again, as quickly as it can execute.
void loop()
{
    if (millis() - lastRead >= readPeriod.count())
    {
        lastRead = millis();
        
        weight_gr = loadcell2_get_weight(&loadcell2, &cell_data);
        Log.info("Weight: %.2f", weight_gr);

        temp_c = thermistor->readTempC();
        Log.info("Temp: %.2f C", temp_c);

        // update Particle variables
        tempC = temp_c;
        weightGr = weight_gr;
    }
    
    if (millis() - lastPublish >= publishPeriod.count())
    {
        lastPublish = millis();
        
        // always make sure the device is online before publishing
        if (Particle.connected()) 
        {
            publishToCloud();
        }
    }

    if (tareScale == true)
    {
        tareScale = false;

        Log.info("Tarring the scale");
        loadcell2_tare(&loadcell2, &cell_data);
        delay(500);
        Log.info("Tarring complete");

        EEPROM.put(tareOffsetAddr, cell_data.tare);
    }

    if (calibrateScale == true)
    {
        calibrateScale = false;

        Log.info("Start calibration");
        if (loadcell2_calibration(&loadcell2, weightRef, &cell_data ) == LOADCELL2_GET_RESULT_OK)
        {
            Log.info("Calibration complete");
            EEPROM.put(weightRefAddr, weightRef);

            if (weightRef == LOADCELL2_WEIGHT_100G) EEPROM.put(calibrationFactorAddr, cell_data.weight_coeff_100g);
            else if (weightRef == LOADCELL2_WEIGHT_500G) EEPROM.put(calibrationFactorAddr, cell_data.weight_coeff_500g);
            else if (weightRef == LOADCELL2_WEIGHT_1000G) EEPROM.put(calibrationFactorAddr, cell_data.weight_coeff_1000g);
            else if (weightRef == LOADCELL2_WEIGHT_5000G) EEPROM.put(calibrationFactorAddr, cell_data.weight_coeff_5000g);
            else if (weightRef == LOADCELL2_WEIGHT_10000G) EEPROM.put(calibrationFactorAddr, cell_data.weight_coeff_10000g);
        }
        else
        {
            Log.info("Calibration error");
        }
    }
}

// Load Cell 2 Click initialization
void loadcell2_init()
{
    loadcell2_cfg_t cfg;

    loadcell2_cfg_setup( &cfg );
    LOADCELL2_MAP_MIKROBUS( cfg, MIKROBUS_1 );
    loadcell2_init( &loadcell2, &cfg );
    delay(100);
    
    Log.info("Reset all registers");
    loadcell2_reset(&loadcell2);
    delay(100);

    Log.info("Power on");
    loadcell2_power_on(&loadcell2);
    delay(100);

    Log.info("Set default config");
    loadcell2_default_cfg( &loadcell2 );
    Delay_ms(100);

    Log.info("Calibrate AFE");
    loadcell2_calibrate_afe(&loadcell2);
    delay(1000);
}

// Particle Cloud Functions must return quickly! Set a boolean to do logic in loop()
int calibrate_scale(String cmd)
{
    weightRef = (uint16_t)cmd.toInt();
    calibrateScale = true;
    return 0;
}

void publishToCloud() 
{
    // zero out the buffer before use.
    memset(buf, 0, sizeof(buf));
    JSONBufferWriter writer(buf, sizeof(buf) - 1);   

    writer.beginObject();
        writer.name("weight").value(weight_gr);
        writer.name("temperature").value(temp_c);
    writer.endObject();
    // add a null terminator at the end of the buffer
    writer.buffer()[std::min(writer.bufferSize(), writer.dataSize())] = 0;

    Particle.publish("datacake/data", writer.buffer());
}

// reads the current calibration settings from EEPROM
void readCalibrationSettings()
{
    float tareOffsetVal;        // zero value that is read when scale is tared
    uint16_t weightRefVal;      // "known" weight reference used when calibrating
    float calibrationFactorVal; // value used to convert the load cell numeric reading to weight
    
    EEPROM.get(tareOffsetAddr, tareOffsetVal);
    if (tareOffsetVal == 0xFFFFFFFF)
    {
        Log.info("No tarring found. Perfoming a tare.");
        tareScale = true;
    }
    else
    {
        cell_data.tare = tareOffsetVal;
        cell_data.tare_ok = LOADCELL2_DATA_OK;
    }
    
    EEPROM.get(weightRefAddr, weightRefVal);
    if (weightRefVal == 0xFFFF)
    {
        Log.info("No weight ref found.");
        weightRef = 1000;
    }
    
    EEPROM.get(calibrationFactorAddr, calibrationFactorVal);
    if (calibrationFactorVal == 0xFFFFFFFF)
    {
        Log.info("No calibration value found. Performing a calibration.");
        calibrateScale = true;
    }
    else
    {
        switch (weightRefVal)
        {
            case LOADCELL2_WEIGHT_100G:
                cell_data.weight_coeff_100g = calibrationFactorVal;
                cell_data.weight_data_100g_ok = LOADCELL2_DATA_OK;
                break;
            case LOADCELL2_WEIGHT_500G:
                cell_data.weight_coeff_500g = calibrationFactorVal;
                cell_data.weight_data_500g_ok = LOADCELL2_DATA_OK;
                break;
            case LOADCELL2_WEIGHT_1000G:
                cell_data.weight_coeff_1000g = calibrationFactorVal;
                cell_data.weight_data_1000g_ok = LOADCELL2_DATA_OK;
                break;
            case LOADCELL2_WEIGHT_5000G:
                cell_data.weight_coeff_5000g = calibrationFactorVal;
                cell_data.weight_data_5000g_ok = LOADCELL2_DATA_OK;
                break;
            case LOADCELL2_WEIGHT_10000G:
                cell_data.weight_coeff_10000g = calibrationFactorVal;
                cell_data.weight_data_10000g_ok = LOADCELL2_DATA_OK;
                break;
            default:
                cell_data.weight_data_100g_ok = LOADCELL2_DATA_NO_DATA;
                cell_data.weight_data_500g_ok = LOADCELL2_DATA_NO_DATA;
                cell_data.weight_data_1000g_ok = LOADCELL2_DATA_NO_DATA;
                cell_data.weight_data_5000g_ok = LOADCELL2_DATA_NO_DATA;
                cell_data.weight_data_10000g_ok = LOADCELL2_DATA_NO_DATA;
                break;
        }
    }
}

// Particle Cloud Functions must return quickly! Set a boolean to do logic in loop() 
int tare_scale(String cmd)
{
    tareScale = true;
    return 0;
}