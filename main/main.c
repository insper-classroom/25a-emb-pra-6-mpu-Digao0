#include <FreeRTOS.h>
#include <task.h>
#include <semphr.h>
#include <queue.h>

#include "pico/stdlib.h"
#include <stdio.h>

#include "hardware/gpio.h"
#include "hardware/i2c.h"
#include "mpu6050.h"

#include "Fusion.h"
#define SAMPLE_PERIOD (0.01f) // replace this with actual sample period

QueueHandle_t xQueueData;

typedef struct adc {
    int axis;
    int val;
} adc_t;

const int MPU_ADDRESS = 0x68;
const int I2C_SDA_GPIO = 4;
const int I2C_SCL_GPIO = 5;

static void mpu6050_reset() {
    // Two byte reset. First byte register, second byte data
    // There are a load more options to set up the device in different ways that could be added here
    uint8_t buf[] = {0x6B, 0x00};
    i2c_write_blocking(i2c_default, MPU_ADDRESS, buf, 2, false);
}

static void mpu6050_read_raw(int16_t accel[3], int16_t gyro[3], int16_t *temp) {
    // For this particular device, we send the device the register we want to read
    // first, then subsequently read from the device. The register is auto incrementing
    // so we don't need to keep sending the register we want, just the first.

    uint8_t buffer[6];

    // Start reading acceleration registers from register 0x3B for 6 bytes
    uint8_t val = 0x3B;
    i2c_write_blocking(i2c_default, MPU_ADDRESS, &val, 1, true); // true to keep master control of bus
    i2c_read_blocking(i2c_default, MPU_ADDRESS, buffer, 6, false);

    for (int i = 0; i < 3; i++) {
        accel[i] = (buffer[i * 2] << 8 | buffer[(i * 2) + 1]);
    }

    // Now gyro data from reg 0x43 for 6 bytes
    // The register is auto incrementing on each read
    val = 0x43;
    i2c_write_blocking(i2c_default, MPU_ADDRESS, &val, 1, true);
    i2c_read_blocking(i2c_default, MPU_ADDRESS, buffer, 6, false);  // False - finished with bus

    for (int i = 0; i < 3; i++) {
        gyro[i] = (buffer[i * 2] << 8 | buffer[(i * 2) + 1]);;
    }

    // Now temperature from reg 0x41 for 2 bytes
    // The register is auto incrementing on each read
    val = 0x41;
    i2c_write_blocking(i2c_default, MPU_ADDRESS, &val, 1, true);
    i2c_read_blocking(i2c_default, MPU_ADDRESS, buffer, 2, false);  // False - finished with bus

    *temp = buffer[0] << 8 | buffer[1];
}

void mpu6050_task(void *p) {
    // configuracao do I2C
    i2c_init(i2c_default, 400 * 1000);
    gpio_set_function(I2C_SDA_GPIO, GPIO_FUNC_I2C);
    gpio_set_function(I2C_SCL_GPIO, GPIO_FUNC_I2C);
    gpio_pull_up(I2C_SDA_GPIO);
    gpio_pull_up(I2C_SCL_GPIO);

    mpu6050_reset();
    int16_t acceleration[3], gyro[3], temp;


    FusionAhrs ahrs;
    FusionAhrsInitialise(&ahrs);


    const float threshold = 1.5f;  // ajuste esse valor conforme teste

    while(1) {
        mpu6050_read_raw(acceleration, gyro, &temp);
        FusionVector gyroscope = {
            .axis.x = gyro[0] / 131.0f, // Conversão para graus/s
            .axis.y = gyro[1] / 131.0f,
            .axis.z = gyro[2] / 131.0f,
        };
  
        FusionVector accelerometer = {
            .axis.x = acceleration[0] / 16384.0f, // Conversão para g
            .axis.y = acceleration[1] / 16384.0f,
            .axis.z = acceleration[2] / 16384.0f,
        };      
  
        FusionAhrsUpdateNoMagnetometer(&ahrs, gyroscope, accelerometer, SAMPLE_PERIOD);
  
        const FusionEuler euler = FusionQuaternionToEuler(FusionAhrsGetQuaternion(&ahrs));

        // Converte roll e pitch para inteiros pequenos (ajustar sensibilidade)
        float pitch = (euler.angle.pitch); // eixo 1 (vertical)
        float roll  = (euler.angle.roll); // eixo 0 (horizontal)

        adc_t x_data = {.axis = 0, .val = pitch};
        adc_t y_data = {.axis = 1, .val = roll};

        //xQueueSend(xQueueData, &x_data, 0);
        //xQueueSend(xQueueData, &y_data, 0);

        float accel_magnitude = sqrtf(
            accelerometer.axis.x * accelerometer.axis.x +
            accelerometer.axis.y * accelerometer.axis.y +
            accelerometer.axis.z * accelerometer.axis.z
        );

        //float delta = fabsf(accel_magnitude - last_accel_magnitude);

        if (accel_magnitude > threshold) {
            // Envia evento de click (axis 2, val 1)
            adc_t click_data = {.axis = 2, .val = 1};
            xQueueSend(xQueueData, &click_data, 0);
        } else{
            xQueueSend(xQueueData, &x_data, 0);
            xQueueSend(xQueueData, &y_data, 0);
        }

        //last_accel_magnitude = accel_magnitude;

        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

void uart_task(void *p) {
    while (1) {
        adc_t data;
        if (xQueueReceive(xQueueData, &data, portMAX_DELAY)) {
            uint8_t bytes[4];
            bytes[0] = (uint8_t)data.axis; 
            bytes[1] = (uint8_t)(data.val >> 8) & 0xFF; 
            bytes[2] = (uint8_t)(data.val & 0xFF);
            bytes[3] = 0xFF; 
            uart_write_blocking(uart0, bytes, sizeof(bytes)); 
        }
    }
}

int main() {
    stdio_init_all();

    xQueueData = xQueueCreate(2, sizeof(adc_t)); // tamanho adc

    xTaskCreate(mpu6050_task, "mpu6050_Task 1", 8192, NULL, 1, NULL);
    xTaskCreate(uart_task, "UART Task", 256, NULL, 1, NULL);

    vTaskStartScheduler();

    while (true)
        ;
}
