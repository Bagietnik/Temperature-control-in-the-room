#include "mcp9808.h"

static const char *TAG = "X003";

MCP9808RegisterInfo_t mcp9808_reg_add = {
    .sensor_address =       0x18,            //I2C slave adress
    .config_register =      0x01,            //Config register
    .t_upper_register =     0x02,            //T upper
    .t_lower_register =     0x03,            //T lower
    .t_crit_register =      0x04,            //T crit
    .amb_temp_register =    0x05,            //Ta
    .man_id_register =      0x06,            //Manufacturer ID 
    .dev_id_register =      0x07,            //Device ID / Device Revision
    .res_register =         0x08,            //Resolution 
};


ProcessValues_t process_values = {
    .temp = 0,                                   //Official temp value
    .temp_sign = 0,                              //Official temp sign value (0 -> +, 1 -> -)
    .temp_upp = 0,
    .temp_upp_sign = 0,
    .temp_low = 0,
    .temp_low_sign = 0,
    .temp_crit = 0,
    .temp_crit_sign = 0,
};

ConfigRegisterValues_t conf_reg_values = {
    /*
    Alert Mod.: Alert Output Mode bit
    0 = Comparator output (power-up default)
    1 = Interrupt output
    This bit cannot be altered when either of the Lock bits are set (bit 6 and bit 7).
    This bit can be programmed in Shutdown mode, but the Alert output will not assert or deassert.
    */
    .alert_mode = 0, 

    /*
    Alert Pol.: Alert Output Polarity bit
    0 = Active-low (power-up default; pull-up resistor required) 
    1 = Active-high
    This bit cannot be altered when either of the Lock bits are set (bit 6 and bit 7).
    This bit can be programmed in Shutdown mode, but the Alert output will not assert or deassert
    */
    .alert_pol = 0,

    /*
    Alert Sel.: Alert Output Select bit
    0 = Alert output for TUPPER, TLOWER and TCRIT (power-up default)
    1 =TA > TCRIT only (TUPPER and TLOWER temperature boundaries are disabled)
    When the Alarm Window Lock bit is set, this bit cannot be altered until unlocked (bit 6).
    This bit can be programmed in Shutdown mode, but the Alert output will not assert or deassert.
    */
    .alert_sel = 0,

    /*
    Alert Cnt.: Alert Output Control bit
    0 = Disabled (power-up default)
    1 = Enabled
    This bit can not be altered when either of the Lock bits are set (bit 6 and bit 7).
    This bit can be programmed in Shutdown mode, but the Alert output will not assert or deassert.
    */
    .alert_cnt = 1,

    /*
    Alert Stat.: Alert Output Status bit
    0 = Alert output is not asserted by the device (power-up default)
    1 = Alert output is asserted as a comparator/Interrupt or critical temperature output
    This bit can not be set to ‘1’ or cleared to ‘0’ in Shutdown mode. However, if the Alert output is configured as Interrupt mode, and if the host controller clears to ‘0’, the interrupt, using bit 5 while the device
    is in Shutdown mode, then this bit will also be cleared ‘0’
    */
    .alert_stat = 0,

    /*
    Int. Clear: Interrupt Clear bit
    0 = No effect (power-up default)
    1 = Clear interrupt output; when read, this bit returns to ‘0’
    This bit can not be set to ‘1’ in Shutdown mode, but it can be cleared after the device enters Shutdown mode
    */
    .int_clear = 0,

    /*
    Win. Lock: TUPPER and TLOWER Window Lock bit
    0 = Unlocked; TUPPER and TLOWER registers can be written (power-up default)
    1 = Locked; TUPPER and TLOWER registers can not be written
    When enabled, this bit remains set to ‘1’ or locked until cleared by a Power-on Reset (Section 5.3 “Summary of Power-on Default”).
    This bit can be programmed in Shutdown mode
    */
    .win_lock = 0,

    /*
    Crit. Lock: TCRIT Lock bit
    0 = Unlocked. TCRIT register can be written (power-up default)
    1 = Locked. TCRIT register can not be written
    When enabled, this bit remains set to ‘1’ or locked until cleared by an internal Reset (Section 5.3 “Summary of Power-on Default”).
    This bit can be programmed in Shutdown mode
    */
    .crit_lock = 0,

    /*
    SHDN: Shutdown Mode bit
    0 = Continuous conversion (power-up default)
    1 = Shutdown (Low-Power mode)
    In shutdown, all power-consuming activities are disabled, though all registers can be written to or read.
    This bit cannot be set to ‘1’ when either of the Lock bits is set (bit 6 and bit 7). However, it can be
    cleared to ‘0’ for continuous conversion while locked (refer to Section 5.2.1 “Shutdown Mode”
    */
    .shdn = 0,

    /*
    THYST: TUPPER and TLOWER Limit Hysteresis bits
    00 = 0°C (power-up default)
    01 = +1.5°C (0 - t_hyst1 1 - t_hyst2)
    10 = +3.0°C
    11 = +6.0°C
    (Refer to Section 5.2.3 “Alert Output Configuration”.)
    This bit can not be altered when either of the Lock bits are set (bit 6 and bit 7).
    */
    .t_hyst_1 = 0,
    .t_hyst_2 = 0,

    /*
    Unimplemented: Read as ‘0
    */
};



// I2C config params
#define I2C_MASTER_SCL_IO                   22                         // GPIO number used for I2C master clock 
#define I2C_MASTER_SDA_IO                   21                         // GPIO number used for I2C master data 
#define I2C_MASTER_NUM                      0                          // I2C master i2c port number, the number of i2c peripheral interfaces available will depend on the chip
#define I2C_MASTER_FREQ_HZ                  400000                     // I2C master clock frequency 
#define I2C_MASTER_TX_BUF_DISABLE           0                          // I2C master doesn't need buffer 
#define I2C_MASTER_RX_BUF_DISABLE           0                          // I2C master doesn't need buffer 
#define I2C_MASTER_TIMEOUT_MS               1000

// Values to write in the res_register
#define TEMP_MEAS_PERIOD                    CONFIG_TEMP_MEAS_PERIOD
#define MCP9808_RES_0_5                     0x0                        // Temp measurement resolution -> 0.5 °C
#define MCP9808_RES_0_25                    0x1                        // Temp measurement resolution -> 0.25 °C
#define MCP9808_RES_0_125                   0x2                        // Temp measurement resolution -> 0.125 °C
#define MCP9808_RES_0_0_625                 0x3                        // Temp measurement resolution -> 0.0625 °C

/**
 * @brief Read a sequence of bytes from a MCP9808 sensor registers
 */
esp_err_t mcp9808_register_read(uint8_t reg_addr, uint8_t *data, size_t len)
{
    return i2c_master_write_read_device(I2C_MASTER_NUM, mcp9808_reg_add.sensor_address, &reg_addr, 1, data, len, I2C_MASTER_TIMEOUT_MS / portTICK_PERIOD_MS);
}

/**
 * @brief Write a byte to a MCP9808 sensor register
 */
esp_err_t mcp9808_register_write_byte(uint8_t reg_addr, uint8_t data)
{
    int ret;
    uint8_t write_buf[2] = {reg_addr, data};

    ret = i2c_master_write_to_device(I2C_MASTER_NUM, mcp9808_reg_add.sensor_address, write_buf, sizeof(write_buf), I2C_MASTER_TIMEOUT_MS / portTICK_PERIOD_MS);

    return ret;
}

/**
 * @brief Get the register value - resolution of the temperature measurments on mcp9808 sensor
 */
uint8_t mcp9808_get_resolution(void)
{   
    uint8_t resolution = 0;
    mcp9808_register_read(mcp9808_reg_add.res_register, &resolution, 1);
    return resolution; 
}

/**
 * @brief Get the config register (add - 0x1) current value 
 */
void mcp9808_get_config_register()
{   
    uint8_t *data = (uint8_t*)pvPortMalloc(2*sizeof(uint8_t));
    esp_err_t ret = i2c_master_write_read_device(I2C_MASTER_NUM, mcp9808_reg_add.sensor_address, &mcp9808_reg_add.config_register, 1, data, 2, I2C_MASTER_TIMEOUT_MS / portTICK_PERIOD_MS);
    if(ret != ESP_OK)
    {
        ESP_LOGE(TAG, "Getting config register value: %s", esp_err_to_name(ret));
        
    }else{
        printf("\n**********************************************");
        printf("\nConfig register / The upper byte: %x", data[0]);
        printf("\nConfig register / The lower byte: %x", data[1]);
        printf("\n**********************************************\n");
    }

    vPortFree(data);
}

/**
* @brief Get the register value - upper temperature
**/
void get_upper_temp()
{
    uint8_t *data = (uint8_t*)pvPortMalloc(2*sizeof(uint8_t));

    ESP_ERROR_CHECK(mcp9808_register_read(mcp9808_reg_add.t_upper_register, data, 2));
    data[0] = data[0] & 0x1F; //upper byte
    process_values.temp_upp = data[0] * 16.0 + data[1] / 16.0; 
    process_values.temp_upp_sign = data[0] & 0x10;
    if(process_values.temp_upp_sign == 0) {ESP_LOGI(TAG, "The upper temp = + %f °C", process_values.temp_upp); } else { ESP_LOGI(TAG, "The upper temp = - %f °C", process_values.temp_upp); }
    
    vPortFree(data);
}

/**
* @brief Get the register value - upper temperature
**/
void get_lower_temp()
{
    uint8_t *data = (uint8_t*)pvPortMalloc(2*sizeof(uint8_t));

    ESP_ERROR_CHECK(mcp9808_register_read(mcp9808_reg_add.t_lower_register, data, 2));
    data[0] = data[0] & 0x1F; //upper byte
    process_values.temp_low = data[0] * 16.0 + data[1] / 16.0; 
    process_values.temp_low_sign = data[0] & 0x10;
    if(process_values.temp_low_sign == 0) {ESP_LOGI(TAG, "The low temp = + %f °C", process_values.temp_low); } else { ESP_LOGI(TAG, "The low temp = - %f °C", process_values.temp_low); }
    
    vPortFree(data);
}

/**
* @brief Get the register value - upper temperature
**/
void get_crit_temp()
{
    uint8_t *data = (uint8_t*)pvPortMalloc(2*sizeof(uint8_t));

    ESP_ERROR_CHECK(mcp9808_register_read(mcp9808_reg_add.t_crit_register, data, 2));
    data[0] = data[0] & 0x1F; //upper byte
    process_values.temp_crit = data[0] * 16.0 + data[1] / 16.0; 
    process_values.temp_crit_sign = data[0] & 0x10;
    if(process_values.temp_crit_sign == 0) {ESP_LOGI(TAG, "The crit temp = + %f °C", process_values.temp_crit); } else { ESP_LOGI(TAG, "The crit temp = - %f °C", process_values.temp_crit); }
    
    vPortFree(data);
}

/**
* @brief Set the register value - upper temperature
**/
void set_upper_temp()
{
    uint8_t write_buf[3] = {mcp9808_reg_add.t_upper_register, 0x2};

    i2c_master_write_to_device(I2C_MASTER_NUM, mcp9808_reg_add.sensor_address, write_buf, sizeof(write_buf), I2C_MASTER_TIMEOUT_MS / portTICK_PERIOD_MS);    
    ESP_LOGI(TAG, "New temp upp value has been saved");
}

void set_lower_temp()
{
    uint8_t write_buf[3] = {mcp9808_reg_add.t_lower_register, 0x1};

    i2c_master_write_to_device(I2C_MASTER_NUM, mcp9808_reg_add.sensor_address, write_buf, sizeof(write_buf), I2C_MASTER_TIMEOUT_MS / portTICK_PERIOD_MS);    
    ESP_LOGI(TAG, "New low value has been saved");
}

/**
* @brief Set the register value - upper temperature
**/
void set_crit_temp()
{
    uint8_t write_buf[3] = {mcp9808_reg_add.t_crit_register, 0x3};

    i2c_master_write_to_device(I2C_MASTER_NUM, mcp9808_reg_add.sensor_address, write_buf, sizeof(write_buf), I2C_MASTER_TIMEOUT_MS / portTICK_PERIOD_MS);    
    ESP_LOGI(TAG, "New temp crit value has been saved");
}

/**
 * @brief Set the register value - resolution of the temperature measurments on mcp9808 sensor
 */
void mcp9808_setResolution()
{
    static uint8_t MCP9808_TEMP_RES = 0x0;
    
    #if CONFIG_TEMP_RESOLUTION_0_25
        MCP9808_TEMP_RES = MCP9808_RES_0_25;
    #endif

    #if CONFIG_TEMP_RESOLUTION_0_125
        MCP9808_TEMP_RES = MCP9808_RES_0_125;
    #endif

    #if CONFIG_TEMP_RESOLUTION_0_0_625
        MCP9808_TEMP_RES = MCP9808_RES_0_0_625;
    #endif

    ESP_ERROR_CHECK(mcp9808_register_write_byte(mcp9808_reg_add.res_register, MCP9808_TEMP_RES));
    ESP_LOGI(TAG, "New resolution has been saved");
}

/**
 * @brief Get the current temperature value
 */
float get_temp()
{
    return process_values.temp;
}

/**
 * @brief I2c master initialization
 */
esp_err_t i2c_master_init(void)
{
    int i2c_master_port = I2C_MASTER_NUM;

    i2c_config_t conf = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = I2C_MASTER_SDA_IO,
        .scl_io_num = I2C_MASTER_SCL_IO,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .master.clk_speed = I2C_MASTER_FREQ_HZ,
    };

    i2c_param_config(i2c_master_port, &conf);

    return i2c_driver_install(i2c_master_port, conf.mode, I2C_MASTER_RX_BUF_DISABLE, I2C_MASTER_TX_BUF_DISABLE, 0);
}

void mcp9808_set_config_register()
{
    uint8_t lower_byte = 0x0;
    uint8_t upper_byte = 0x0;

    lower_byte |= conf_reg_values.alert_mode << 0;
    lower_byte |= conf_reg_values.alert_pol << 1;
    lower_byte |= conf_reg_values.alert_sel << 2;
    lower_byte |= conf_reg_values.alert_cnt << 3;
    lower_byte |= conf_reg_values.alert_stat << 4;
    lower_byte |= conf_reg_values.int_clear << 5;
    lower_byte |= conf_reg_values.win_lock << 6;
    lower_byte |= conf_reg_values.crit_lock << 7;
    
    upper_byte |= conf_reg_values.shdn << 0;
    upper_byte |= conf_reg_values.t_hyst_1 << 1;
    upper_byte |= conf_reg_values.t_hyst_2 << 2;
    
    uint8_t write_buf[3] = {mcp9808_reg_add.config_register, upper_byte, lower_byte};
    i2c_master_write_to_device(I2C_MASTER_NUM, mcp9808_reg_add.sensor_address, write_buf, sizeof(write_buf), I2C_MASTER_TIMEOUT_MS / portTICK_PERIOD_MS);
    ESP_LOGI(TAG, "Config register has been saved");
}

/**
* @brief Initialization of the mcp9808 sensor (I2C + resolution)
**/
void mcp9808_init()
{
    ESP_ERROR_CHECK(i2c_master_init());
    ESP_LOGI(TAG, "I2C initialized successfully");

    mcp9808_set_config_register();
    mcp9808_setResolution();

    mcp9808_get_config_register();
}

/**
* @brief Get the register value - temperature
**/
void get_new_temp(void *pvParameters)
{
    while (1)
    {   
        uint8_t *data = (uint8_t*)pvPortMalloc(2*sizeof(uint8_t));

        ESP_ERROR_CHECK(mcp9808_register_read(mcp9808_reg_add.amb_temp_register, data, 2));
        data[0] = data[0] & 0x1F; //upper byte
        process_values.temp = data[0] * 16.0 + data[1] / 16.0; 
        process_values.temp_sign = data[0] & 0x10;
        if(process_values.temp_sign == 0) {ESP_LOGI(TAG, "Temperature = + %f °C", process_values.temp); } else { ESP_LOGI(TAG, "Temperature = - %f °C", process_values.temp); }
        
        get_alarm_output_state();

        vPortFree(data);
        vTaskDelay(TEMP_MEAS_PERIOD * 1000 / portTICK_PERIOD_MS);
    } 
}


void get_alarm_output_state()
{
        uint8_t *data = (uint8_t*)pvPortMalloc(2*sizeof(uint8_t));

        ESP_ERROR_CHECK(mcp9808_register_read(mcp9808_reg_add.amb_temp_register, data, 2));
        data[0] = data[0] & 0xE0; //upper byte
        
        printf("Alarm temp reg value: %d\n", data[0]);

        vPortFree(data);
}

