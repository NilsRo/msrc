#include "esc_apd_f.h"

static void process(esc_apd_f_parameters_t *parameter);
static uint8_t update_crc8(uint8_t crc, uint8_t crc_seed);
static uint8_t get_crc8(uint8_t *buffer, uint8_t lenght);

void esc_apd_f_task(void *parameters)
{
    esc_apd_f_parameters_t parameter = *(esc_apd_f_parameters_t *)parameters;
    *parameter.rpm = 0;
    *parameter.voltage = 0;
    *parameter.current = 0;
    *parameter.temperature = 0;
    *parameter.cell_voltage = 0;
    *parameter.consumption = 0;
    *parameter.cell_count = 1;
    xTaskNotifyGive(receiver_task_handle);
#ifdef SIM_SENSORS
    *parameter.temperature = 12.34;
    *parameter.voltage = 12.34;
    *parameter.current = 12.34;
    *parameter.consumption = 12.34;
    *parameter.rpm = 12345.67;
    *parameter.cell_voltage = 3.75;
#endif

    TaskHandle_t task_handle;
    uint cell_count_delay = 15000;
    cell_count_parameters_t cell_count_parameters = {cell_count_delay, parameter.voltage, parameter.cell_count};
    xTaskCreate(cell_count_task, "cell_count_task", STACK_CELL_COUNT, (void *)&cell_count_parameters, 1, &task_handle);
    xQueueSendToBack(tasks_queue_handle, task_handle, 0);

    uart_begin(UART_ESC, 115200, UART1_TX_GPIO, UART_ESC_RX, ESC_APD_F_TIMEOUT, 8, 1, UART_PARITY_NONE, false);

    while (1)
    {
        ulTaskNotifyTakeIndexed(1, pdTRUE, portMAX_DELAY);
        process(&parameter);
    }
}

static void process(esc_apd_f_parameters_t *parameter)
{
    static uint32_t timestamp = 0;
    uint8_t lenght = uart_available(UART_ESC);
    if (lenght == ESC_APD_F_PACKET_LENGHT || lenght == ESC_KISS_PACKET_LENGHT)
    {
        uint8_t data[ESC_KISS_PACKET_LENGHT];
        uart_read_bytes(UART_ESC, data, ESC_KISS_PACKET_LENGHT);
        if (get_crc8(data, ESC_KISS_PACKET_LENGHT - 1) == data[9])
        {
            float temperature = data[0];
            float voltage = ((uint16_t)data[1] << 8 | data[2]) / 100.0;
            float current = ((uint16_t)data[3] << 8 | data[4]) / 100.0;
            float consumption = ((uint16_t)data[5] << 8 | data[6]);
            float rpm = ((uint16_t)data[7] << 8 | data[8]) * 100.0;
            *parameter->temperature = get_average(parameter->alpha_temperature, *parameter->temperature, temperature);
            *parameter->voltage = get_average(parameter->alpha_voltage, *parameter->voltage, voltage);
            *parameter->current = get_average(parameter->alpha_current, *parameter->current, current);
            *parameter->consumption = get_average(parameter->alpha_voltage, *parameter->consumption, consumption);
            *parameter->rpm = get_average(parameter->alpha_rpm, *parameter->rpm, rpm);
            *parameter->cell_voltage = *parameter->voltage / *parameter->cell_count;
            if (debug)
                printf("\nApd F (%u) < Rpm: %.0f Volt: %0.2f Curr: %.2f Temp: %.0f Cons: %.0f CellV: %.2f", uxTaskGetStackHighWaterMark(NULL), *parameter->rpm, *parameter->voltage, *parameter->current, *parameter->temperature, *parameter->consumption, *parameter->cell_voltage);
        }
    }
}

static uint8_t update_crc8(uint8_t crc, uint8_t crc_seed)
{
    uint8_t crc_u, i;
    crc_u = crc;
    crc_u ^= crc_seed;
    for (i = 0; i < 8; i++)
        crc_u = (crc_u & 0x80) ? 0x7 ^ (crc_u << 1) : (crc_u << 1);
    return (crc_u);
}

static uint8_t get_crc8(uint8_t *buffer, uint8_t lenght)
{
    uint8_t crc = 0, i;
    for (i = 0; i < lenght; i++)
        crc = update_crc8(buffer[i], crc);
    return crc;
}
