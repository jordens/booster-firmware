/*
 * channels.c
 *
 *  Created on: Oct 2, 2017
 *      Author: wizath
 */

#include "config.h"
#include "channels.h"
#include "ads7924.h"
#include "max6642.h"
#include "i2c.h"
#include "led_bar.h"
#include "math.h"
#include "locks.h"
#include "temp_mgt.h"
#include "eeprom.h"
#include "tasks.h"

static channel_t channels[8] = { 0 };
volatile uint8_t channel_mask = 0;

TaskHandle_t task_rf_measure;
TaskHandle_t task_rf_info;
TaskHandle_t task_rf_interlock;

void rf_channels_init(void)
{
	GPIO_InitTypeDef  GPIO_InitStructure;

	channel_t ch;
	memset(&ch, 0, sizeof(channel_t));

	for (int i = 0; i < 8; i++) {
		channels[i] = ch;
	}

	/* Channel ENABLE Pins */
	RCC_AHB1PeriphClockCmd(RCC_AHB1Periph_GPIOD, ENABLE);

	GPIO_InitStructure.GPIO_Pin = GPIO_Pin_0 | GPIO_Pin_1 | GPIO_Pin_2 | GPIO_Pin_3 |
								  GPIO_Pin_4 | GPIO_Pin_5 | GPIO_Pin_6 | GPIO_Pin_7;
	GPIO_InitStructure.GPIO_Mode = GPIO_Mode_OUT;
	GPIO_InitStructure.GPIO_OType = GPIO_OType_PP;
	GPIO_InitStructure.GPIO_Speed = GPIO_Speed_100MHz;
	GPIO_InitStructure.GPIO_PuPd = GPIO_PuPd_NOPULL;
	GPIO_Init(GPIOD, &GPIO_InitStructure);

	/* Disable all channels by default */
	GPIO_ResetBits(GPIOD, 0b0000000011111111);

	/* Channel ALERT Pins */
	GPIO_InitStructure.GPIO_Pin = GPIO_Pin_8 | GPIO_Pin_9 | GPIO_Pin_10 | GPIO_Pin_11 |
				                  GPIO_Pin_12 | GPIO_Pin_13 | GPIO_Pin_14 | GPIO_Pin_15;
	GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IN;
	GPIO_InitStructure.GPIO_OType = GPIO_OType_PP;
	GPIO_InitStructure.GPIO_Speed = GPIO_Speed_100MHz;
	GPIO_InitStructure.GPIO_PuPd = GPIO_PuPd_NOPULL;
	GPIO_Init(GPIOD, &GPIO_InitStructure);

	/* Channel OVL Pins */
	GPIO_InitStructure.GPIO_Pin = GPIO_Pin_8 | GPIO_Pin_9 | GPIO_Pin_10 | GPIO_Pin_11 |
								  GPIO_Pin_12 | GPIO_Pin_13 | GPIO_Pin_14 | GPIO_Pin_15;
	GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IN;
	GPIO_InitStructure.GPIO_OType = GPIO_OType_PP;
	GPIO_InitStructure.GPIO_Speed = GPIO_Speed_100MHz;
	GPIO_InitStructure.GPIO_PuPd = GPIO_PuPd_DOWN;
	GPIO_Init(GPIOE, &GPIO_InitStructure);

	/* Channel USER IO */
	/* IN PINS */
	RCC_AHB1PeriphClockCmd(RCC_AHB1Periph_GPIOE, ENABLE);

	GPIO_InitStructure.GPIO_Pin = GPIO_Pin_0 | GPIO_Pin_1 | GPIO_Pin_2 | GPIO_Pin_3 |
								  GPIO_Pin_4 | GPIO_Pin_5 | GPIO_Pin_6 | GPIO_Pin_7;
	GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IN;
	GPIO_InitStructure.GPIO_OType = GPIO_OType_PP;
	GPIO_InitStructure.GPIO_Speed = GPIO_Speed_100MHz;
	GPIO_InitStructure.GPIO_PuPd = GPIO_PuPd_DOWN;
	GPIO_Init(GPIOE, &GPIO_InitStructure);

	/* Channel SIG_ON */
	RCC_AHB1PeriphClockCmd(RCC_AHB1Periph_GPIOG, ENABLE);
	GPIO_InitStructure.GPIO_Pin = GPIO_Pin_8 | GPIO_Pin_9 | GPIO_Pin_10 | GPIO_Pin_11 |
								  GPIO_Pin_12 | GPIO_Pin_13 | GPIO_Pin_14 | GPIO_Pin_15;
	GPIO_InitStructure.GPIO_Mode = GPIO_Mode_OUT;
	GPIO_InitStructure.GPIO_OType = GPIO_OType_PP;
	GPIO_InitStructure.GPIO_Speed = GPIO_Speed_100MHz;
	GPIO_InitStructure.GPIO_PuPd = GPIO_PuPd_DOWN;
	GPIO_Init(GPIOG, &GPIO_InitStructure);

	/* Disable all SIGON IO by default */
	GPIO_ResetBits(GPIOG, 0b1111111100000000);

	/* Channel ADCs RESET */
	RCC_AHB1PeriphClockCmd(RCC_AHB1Periph_GPIOB, ENABLE);
	GPIO_InitStructure.GPIO_Pin = GPIO_Pin_9;
	GPIO_InitStructure.GPIO_Mode = GPIO_Mode_OUT;
	GPIO_InitStructure.GPIO_OType = GPIO_OType_PP;
	GPIO_InitStructure.GPIO_Speed = GPIO_Speed_100MHz;
	GPIO_InitStructure.GPIO_PuPd = GPIO_PuPd_NOPULL;
	GPIO_Init(GPIOB, &GPIO_InitStructure);

	/* ADC reset off */
	GPIO_SetBits(GPIOB, GPIO_Pin_9);
}

uint8_t rf_channels_get_mask(void)
{
	return channel_mask;
}

void rf_channel_load_values(channel_t * ch)
{
	// load interlocks value
	ch->cal_values.input_dac_cal_value = eeprom_read16(DAC1_EEPROM_ADDRESS);
	ch->cal_values.output_dac_cal_value = eeprom_read16(DAC2_EEPROM_ADDRESS);

	ch->cal_values.fwd_pwr_offset = eeprom_read16(ADC1_OFFSET_ADDRESS);
	ch->cal_values.fwd_pwr_scale = eeprom_read16(ADC1_SCALE_ADDRESS);

	ch->cal_values.rfl_pwr_offset = eeprom_read16(ADC2_OFFSET_ADDRESS);
	ch->cal_values.rfl_pwr_scale = eeprom_read16(ADC2_SCALE_ADDRESS);

	ch->cal_values.bias_dac_cal_value = eeprom_read16(BIAS_DAC_VALUE_ADDRESS);

	uint32_t u32_int_scale = eeprom_read32(HW_INT_SCALE);
	uint32_t u32_int_offset = eeprom_read32(HW_INT_OFFSET);
	memcpy(&ch->cal_values.hw_int_scale, &u32_int_scale, sizeof(float));
	memcpy(&ch->cal_values.hw_int_offset, &u32_int_offset, sizeof(float));

	uint16_t interlock = eeprom_read16(SOFT_INTERLOCK_ADDRESS);
	if (interlock >= 0 && interlock <= 380) {
		ch->soft_interlock_value = ((double) interlock / 10.0f);
		ch->soft_interlock_enabled = true;
	} else
		ch->soft_interlock_enabled = false;
}

uint8_t rf_channels_detect(void)
{
	printf("Starting detect channels\n");

	if (lock_take(I2C_LOCK, portMAX_DELAY))
	{
		for (int i = 0; i < CHANNEL_COUNT; i++)
		{
			i2c_mux_select(i);

			uint8_t dac_detected = i2c_device_connected(I2C1, I2C_DAC_ADDR);
			vTaskDelay(10);
			uint8_t dual_dac_detected = i2c_device_connected(I2C1, I2C_DUAL_DAC_ADDR);
			vTaskDelay(10);
			uint8_t temp_sensor_detected = i2c_device_connected(I2C1, I2C_MODULE_TEMP);

			if (dual_dac_detected && dac_detected && temp_sensor_detected)
			{
				printf("Channel %d OK\n", i);
				ads7924_init();
				max6642_init();

				channel_mask |= 1UL << i;
				channels[i].detected = true;
				channels[i].input_interlock = false;
				channels[i].output_interlock = false;

				rf_channel_load_values(&channels[i]);
			}
		}

		lock_free(I2C_LOCK);
	}

	return 0;
}

void rf_channels_control(uint16_t channel_mask, bool enable)
{
	/* ENABLE Pins GPIOD 0-7 */
	if (enable)
		GPIO_SetBits(GPIOD, channel_mask);
	else
		GPIO_ResetBits(GPIOD, channel_mask);
}

void rf_channels_sigon(uint16_t channel_mask, bool enable)
{
	/* ENABLE Pins GPIOD 0-7 */
	if (enable)
		GPIO_SetBits(GPIOG, (channel_mask << 8) & 0xFF00);
	else
		GPIO_ResetBits(GPIOG, (channel_mask << 8) & 0xFF00);
}

uint8_t rf_channels_read_alert(void)
{
	/* ALERT Pins GPIOD 8-15 */
	return (GPIO_ReadInputData(GPIOD) & 0xFF00) >> 8;
}

uint8_t rf_channels_read_user(void)
{
	/* ALERT Pins GPIOD 8-15 */
	return (GPIO_ReadInputData(GPIOE) & 0xFF);
}

uint8_t rf_channels_read_sigon(void)
{
	/* ENABLE Pins GPIOG 8-15 */
	return (GPIO_ReadOutputData(GPIOG) & 0xFF00) >> 8;
}

uint8_t rf_channels_read_ovl(void)
{
	/* OVL Pins GPIOE 8-15 */
	return (GPIO_ReadInputData(GPIOE) & 0xFF00) >> 8;
}

uint8_t rf_channels_read_enabled(void)
{
	/* ENABLE Pins GPIOD 0-8 */
	return (GPIO_ReadOutputData(GPIOD) & 0xFF);
}

bool rf_channel_enable_procedure(uint8_t channel)
{
	int bitmask = 1 << channel;

	if (channels[channel].error) {
		led_bar_or(0, 0, (1UL << channel));
		return false;
	}

	rf_channels_control(bitmask, true);
	vTaskDelay(50);

	if (lock_take(I2C_LOCK, portMAX_DELAY))
	{
		i2c_mux_select(channel);
		i2c_dac_set(4095);

		vTaskDelay(50);

		// set calibration values
		i2c_dual_dac_set(0, channels[channel].cal_values.input_dac_cal_value);
		vTaskDelay(10);

		i2c_dual_dac_set(1, channels[channel].cal_values.output_dac_cal_value);
		vTaskDelay(10);

//		i2c_dac_set_value(2.05f);
		i2c_dac_set(channels[channel].cal_values.bias_dac_cal_value);

		vTaskDelay(10);

		lock_free(I2C_LOCK);
	}

	rf_channels_sigon(bitmask, true);
	vTaskDelay(50);
	rf_channels_sigon(bitmask, false);
	vTaskDelay(50);

	rf_channels_sigon(bitmask, true);

//	if (lock_take(I2C_LOCK, portMAX_DELAY))
//	{
//		i2c_mux_select(channel);
//		ads7924_enable_alert();
//		ads7924_clear_alert();
//		lock_free(I2C_LOCK);
//	}

	return true;
}

void rf_channels_enable(uint8_t mask)
{
	for (int i = 0; i < 8; i++)
	{
		if ((1 << i) & mask) {
			if (rf_channel_enable_procedure(i)) {
				led_bar_or(1UL << i, 0, 0);
			}
		}
	}
}

void rf_channels_disable(uint8_t mask)
{
	for (int i = 0; i < 8; i++)
	{
		if ((1 << i) & mask) {
			rf_channel_disable_procedure(i);
			led_bar_and(1UL << i, 0, 0);
		}
	}
}

void rf_channel_disable_procedure(uint8_t channel)
{
	int bitmask = 1 << channel;

	rf_channels_control(bitmask, false);
	rf_channels_sigon(bitmask, false);

	if (lock_take(I2C_LOCK, portMAX_DELAY)) {
		i2c_mux_select(channel);
		i2c_dac_set(0);
		i2c_dual_dac_set_val(0.0f, 0.0f);

		ads7924_disable_alert();

		lock_free(I2C_LOCK);
	}
}

void rf_channels_soft_interlock_set(uint8_t channel, double value)
{
	channel_t * ch;

	if (channel < 8) {
		if ((1 << channel) & channel_mask) {
			ch = rf_channel_get(channel);

			if (value < 39.0f && value >= 0) {
				ch->soft_interlock_value = value;
				ch->soft_interlock_enabled = true;

				if (lock_take(I2C_LOCK, portMAX_DELAY)) {
					i2c_mux_select(channel);
					eeprom_write16(SOFT_INTERLOCK_ADDRESS, (uint16_t) ((float) (value * 10.0f)));
					lock_free(I2C_LOCK);
				}
			} else {
				ch->soft_interlock_value = 0;
				ch->soft_interlock_enabled = false;

				if (lock_take(I2C_LOCK, portMAX_DELAY)) {
					i2c_mux_select(channel);
					eeprom_write16(SOFT_INTERLOCK_ADDRESS, 0xffff);
					lock_free(I2C_LOCK);
				}
			}
		}
	}
}

channel_t * rf_channel_get(uint8_t num)
{
	if (num < 8) return &channels[num];
	return NULL;
}

void rf_channels_interlock_task(void *pvParameters)
{
	uint8_t channel_enabled = 0;
	uint8_t channel_ovl = 0;
	uint8_t channel_alert = 0;
	uint8_t channel_user = 0;
	uint8_t channel_sigon = 0;

	rf_channels_init();
	rf_channels_detect();

	xTaskCreate(rf_channels_measure_task, "CH MEAS", configMINIMAL_STACK_SIZE + 256UL, NULL, tskIDLE_PRIORITY + 2, &task_rf_measure);
	xTaskCreate(rf_channels_info_task, "CH INFO", configMINIMAL_STACK_SIZE + 256UL, NULL, tskIDLE_PRIORITY + 1, &task_rf_info);
	vTaskSuspend(task_rf_info);

	for (;;)
	{
		GPIO_ToggleBits(BOARD_LED1);

		channel_enabled = rf_channels_read_enabled();
		channel_ovl = rf_channels_read_ovl();
		channel_alert = rf_channels_read_alert();
		channel_sigon = rf_channels_read_sigon();
		channel_user = rf_channels_read_user();

		for (int i = 0; i < 8; i++) {

			if ((1 << i) & channel_mask) {

				channels[i].overcurrent = (channel_alert >> i) & 0x01;
				channels[i].enabled = (channel_enabled >> i) & 0x01;
				channels[i].sigon = (channel_sigon >> i) & 0x01;

				if (((channel_ovl >> i) & 0x01) && (channels[i].sigon && channels[i].enabled)) channels[i].input_interlock = true;
				if (((channel_user >> i) & 0x01) && (channels[i].sigon && channels[i].enabled)) channels[i].output_interlock = true;
				if (channels[i].soft_interlock_enabled && (channels[i].measure.fwd_pwr > channels[i].soft_interlock_value)) channels[i].soft_interlock = true;

				if ((channels[i].sigon && channels[i].enabled) && ( channels[i].output_interlock || channels[i].input_interlock || channels[i].soft_interlock )) {
					rf_channels_sigon(1 << i, false);

					led_bar_and((1UL << i), 0x00, 0x00);
					led_bar_or(0x00, (1UL << i), 0x00);
				}

				if (channels[i].measure.remote_temp > 80.0f && channels[i].measure.remote_temp < 5.0f)
				{
					rf_channel_disable_procedure(i);
					led_bar_and((1UL << i), 0x00, 0x00);
					led_bar_or(0, 0, (1UL << i));
					channels[i].error = 1;
				}

				if (channels[i].enabled && channels[i].overcurrent)
				{
					rf_channel_disable_procedure(i);
					led_bar_and((1UL << i), 0x00, 0x00);
					led_bar_or(0, 0, (1UL << i));
					channels[i].error = 1;
				}
			}
		}

		vTaskDelay(10);
	}
}

void rf_channels_measure_task(void *pvParameters)
{
	for (;;)
	{
		for (int i = 0; i < 8; i++) {
			if ((1 << i) & channel_mask) {

				if (lock_take(I2C_LOCK, portMAX_DELAY))
				{
					i2c_mux_select(i);

					channels[i].measure.fwd_pwr = (double) (channels[i].measure.adc_raw_ch1 - channels[i].cal_values.fwd_pwr_offset) / (double) channels[i].cal_values.fwd_pwr_scale;
					channels[i].measure.rfl_pwr = (double) (channels[i].measure.adc_raw_ch1 - channels[i].cal_values.rfl_pwr_offset) / (double) channels[i].cal_values.rfl_pwr_scale;

					channels[i].measure.i30 = (ads7924_get_channel_voltage(0) / 50) / 0.02f;
					channels[i].measure.i60 = (ads7924_get_channel_voltage(0) / 50) / 0.1f;
					channels[i].measure.in80 = (ads7924_get_channel_voltage(0) / 50) / 4.7f;

					channels[i].measure.local_temp = max6642_get_local_temp();
					channels[i].measure.remote_temp = max6642_get_remote_temp();

					lock_free(I2C_LOCK);
				}

			}
		}

		vTaskDelay(50);
	}
}

void rf_channels_hwint_override(uint8_t channel, double int_value)
{
	if (channel < 8)
	{
		if (int_value >= -10.0f && int_value < 39.0f)
		{
			double value = (double) ((int_value * channels[channel].cal_values.fwd_pwr_scale) + channels[channel].cal_values.fwd_pwr_offset);
			uint16_t dac_value = (uint16_t) value;
			printf("DAC VALUE %0.2f %d\n", value, dac_value);
		}
	}
}

void rf_clear_interlock(void)
{
	for (int i = 0; i < 8; i++) {
		channels[i].input_interlock = false;
		channels[i].output_interlock = false;
		channels[i].soft_interlock = false;
	}

	vTaskDelay(10);
	rf_channels_sigon(channel_mask & rf_channels_read_enabled(), true);
	led_bar_write(rf_channels_read_sigon(), 0, 0);
}

void rf_channels_info_task(void *pvParameters)
{
	uint8_t address_list[] = {0x2C, 0x2E, 0x2F};

	for (;;)
	{
		puts("\033[2J");
		printf("PGOOD: %d\n", GPIO_ReadInputDataBit(GPIOG, GPIO_Pin_4));
//		printf("TEMPERATURES\n");
//		for (int i = 0; i < 3; i ++)
//		{
//			if (lock_take(I2C_LOCK, portMAX_DELAY))
//			{
//				float temp1 = max6639_get_temperature(address_list[i], 0);
//				float temp2 = max6639_get_temperature(address_list[i], 1);
//				lock_free(I2C_LOCK);
//				printf("[%d] ch1 (ext): %.3f ch2 (int): %.3f\n", i, temp1, temp2);
//			}
//		}

		printf("FAN SPEED: %d %%\n", temp_mgt_get_fanspeed());
		printf("AVG TEMP: %0.2f CURRENT: %0.2f\n", temp_mgt_get_avg_temp(), temp_mgt_get_max_temp());
		printf("CHANNELS INFO\n");
		printf("==============================================================================\n");
		printf("\t\t#0\t#1\t#2\t#3\t#4\t#5\t#6\t#7\n");

		printf("DETECTED\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t\n", channels[0].detected,
																channels[1].detected,
																channels[2].detected,
																channels[3].detected,
																channels[4].detected,
																channels[5].detected,
																channels[6].detected,
																channels[7].detected);

		printf("TXPWR [V]\t%0.2f\t%0.2f\t%0.2f\t%0.2f\t%0.2f\t%0.2f\t%0.2f\t%0.2f\t\n", channels[0].measure.adc_ch1,
																						channels[1].measure.adc_ch1,
																						channels[2].measure.adc_ch1,
																						channels[3].measure.adc_ch1,
																						channels[4].measure.adc_ch1,
																						channels[5].measure.adc_ch1,
																						channels[6].measure.adc_ch1,
																						channels[7].measure.adc_ch1);

		printf("RFLPWR [V]\t%0.2f\t%0.2f\t%0.2f\t%0.2f\t%0.2f\t%0.2f\t%0.2f\t%0.2f\t\n", channels[0].measure.adc_ch2,
																						channels[1].measure.adc_ch2,
																						channels[2].measure.adc_ch2,
																						channels[3].measure.adc_ch2,
																						channels[4].measure.adc_ch2,
																						channels[5].measure.adc_ch2,
																						channels[6].measure.adc_ch2,
																						channels[7].measure.adc_ch2);

		printf("TXPWR [dB]\t%0.2f\t%0.2f\t%0.2f\t%0.2f\t%0.2f\t%0.2f\t%0.2f\t%0.2f\t\n", channels[0].measure.fwd_pwr,
																						channels[1].measure.fwd_pwr,
																						channels[2].measure.fwd_pwr,
																						channels[3].measure.fwd_pwr,
																						channels[4].measure.fwd_pwr,
																						channels[5].measure.fwd_pwr,
																						channels[6].measure.fwd_pwr,
																						channels[7].measure.fwd_pwr);

		printf("RFLPWR [dB]\t%0.2f\t%0.2f\t%0.2f\t%0.2f\t%0.2f\t%0.2f\t%0.2f\t%0.2f\t\n", channels[0].measure.rfl_pwr,
																						channels[1].measure.rfl_pwr,
																						channels[2].measure.rfl_pwr,
																						channels[3].measure.rfl_pwr,
																						channels[4].measure.rfl_pwr,
																						channels[5].measure.rfl_pwr,
																						channels[6].measure.rfl_pwr,
																						channels[7].measure.rfl_pwr);

		printf("I30V [A]\t%0.3f\t%0.3f\t%0.3f\t%0.3f\t%0.3f\t%0.3f\t%0.3f\t%0.3f\t\n", channels[0].measure.i30,
																						channels[1].measure.i30,
																						channels[2].measure.i30,
																						channels[3].measure.i30,
																						channels[4].measure.i30,
																						channels[5].measure.i30,
																						channels[6].measure.i30,
																						channels[7].measure.i30);

		printf("I6V0 [A]\t%0.3f\t%0.3f\t%0.3f\t%0.3f\t%0.3f\t%0.3f\t%0.3f\t%0.3f\t\n", channels[0].measure.i60,
																						channels[1].measure.i60,
																						channels[2].measure.i60,
																						channels[3].measure.i60,
																						channels[4].measure.i60,
																						channels[5].measure.i60,
																						channels[6].measure.i60,
																						channels[7].measure.i60);

		printf("IN8V0 [mA]\t%0.3f\t%0.3f\t%0.3f\t%0.3f\t%0.3f\t%0.3f\t%0.3f\t%0.3f\t\n", channels[0].measure.in80 * 1000,
																						channels[1].measure.in80 * 1000,
																						channels[2].measure.in80 * 1000,
																						channels[3].measure.in80 * 1000,
																						channels[4].measure.in80 * 1000,
																						channels[5].measure.in80 * 1000,
																						channels[6].measure.in80 * 1000,
																						channels[7].measure.in80 * 1000);

		printf("ON\t\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t\n", channels[0].enabled,
															channels[1].enabled,
															channels[2].enabled,
															channels[3].enabled,
															channels[4].enabled,
															channels[5].enabled,
															channels[6].enabled,
															channels[7].enabled);

		printf("SON\t\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t\n", channels[0].sigon,
															channels[1].sigon,
															channels[2].sigon,
															channels[3].sigon,
															channels[4].sigon,
															channels[5].sigon,
															channels[6].sigon,
															channels[7].sigon);

		printf("IINT\t\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t\n", channels[0].input_interlock,
															channels[1].input_interlock,
															channels[2].input_interlock,
															channels[3].input_interlock,
															channels[4].input_interlock,
															channels[5].input_interlock,
															channels[6].input_interlock,
															channels[7].input_interlock);

		printf("OINT\t\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t\n", channels[0].output_interlock,
															channels[1].output_interlock,
															channels[2].output_interlock,
															channels[3].output_interlock,
															channels[4].output_interlock,
															channels[5].output_interlock,
															channels[6].output_interlock,
															channels[7].output_interlock);

		printf("SINT\t\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t\n", channels[0].soft_interlock,
															channels[1].soft_interlock,
															channels[2].soft_interlock,
															channels[3].soft_interlock,
															channels[4].soft_interlock,
															channels[5].soft_interlock,
															channels[6].soft_interlock,
															channels[7].soft_interlock);

		printf("SINTE\t\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t\n", channels[0].soft_interlock_enabled,
																channels[1].soft_interlock_enabled,
																channels[2].soft_interlock_enabled,
																channels[3].soft_interlock_enabled,
																channels[4].soft_interlock_enabled,
																channels[5].soft_interlock_enabled,
																channels[6].soft_interlock_enabled,
																channels[7].soft_interlock_enabled);


		printf("SINTV\t\t%0.1f\t%0.1f\t%0.1f\t%0.1f\t%0.1f\t%0.1f\t%0.1f\t%0.1f\t\n", channels[0].soft_interlock_value,
																channels[1].soft_interlock_value,
																channels[2].soft_interlock_value,
																channels[3].soft_interlock_value,
																channels[4].soft_interlock_value,
																channels[5].soft_interlock_value,
																channels[6].soft_interlock_value,
																channels[7].soft_interlock_value);


		printf("OVC\t\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t\n", channels[0].overcurrent,
															channels[1].overcurrent,
															channels[2].overcurrent,
															channels[3].overcurrent,
															channels[4].overcurrent,
															channels[5].overcurrent,
															channels[6].overcurrent,
															channels[7].overcurrent);

		printf("ADC1\t\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t\n", channels[0].measure.adc_raw_ch1,
																channels[1].measure.adc_raw_ch1,
																channels[2].measure.adc_raw_ch1,
																channels[3].measure.adc_raw_ch1,
																channels[4].measure.adc_raw_ch1,
																channels[5].measure.adc_raw_ch1,
																channels[6].measure.adc_raw_ch1,
																channels[7].measure.adc_raw_ch1);

		printf("ADC2\t\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t\n", channels[0].measure.adc_raw_ch2,
															channels[1].measure.adc_raw_ch2,
															channels[2].measure.adc_raw_ch2,
															channels[3].measure.adc_raw_ch2,
															channels[4].measure.adc_raw_ch2,
															channels[5].measure.adc_raw_ch2,
															channels[6].measure.adc_raw_ch2,
															channels[7].measure.adc_raw_ch2);

		printf("DAC1\t\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t\n", channels[0].cal_values.input_dac_cal_value,
																channels[1].cal_values.input_dac_cal_value,
																channels[2].cal_values.input_dac_cal_value,
																channels[3].cal_values.input_dac_cal_value,
																channels[4].cal_values.input_dac_cal_value,
																channels[5].cal_values.input_dac_cal_value,
																channels[6].cal_values.input_dac_cal_value,
																channels[7].cal_values.input_dac_cal_value);

		printf("DAC2\t\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t\n", channels[0].cal_values.output_dac_cal_value,
															channels[1].cal_values.output_dac_cal_value,
															channels[2].cal_values.output_dac_cal_value,
															channels[3].cal_values.output_dac_cal_value,
															channels[4].cal_values.output_dac_cal_value,
															channels[5].cal_values.output_dac_cal_value,
															channels[6].cal_values.output_dac_cal_value,
															channels[7].cal_values.output_dac_cal_value);

		printf("SCALE1\t\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t\n", channels[0].cal_values.fwd_pwr_scale,
															channels[1].cal_values.fwd_pwr_scale,
															channels[2].cal_values.fwd_pwr_scale,
															channels[3].cal_values.fwd_pwr_scale,
															channels[4].cal_values.fwd_pwr_scale,
															channels[5].cal_values.fwd_pwr_scale,
															channels[6].cal_values.fwd_pwr_scale,
															channels[7].cal_values.fwd_pwr_scale);

		printf("OFFSET1\t\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t\n", channels[0].cal_values.fwd_pwr_offset,
															channels[1].cal_values.fwd_pwr_offset,
															channels[2].cal_values.fwd_pwr_offset,
															channels[3].cal_values.fwd_pwr_offset,
															channels[4].cal_values.fwd_pwr_offset,
															channels[5].cal_values.fwd_pwr_offset,
															channels[6].cal_values.fwd_pwr_offset,
															channels[7].cal_values.fwd_pwr_offset);

		printf("BIASCAL\t\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t\n", channels[0].cal_values.bias_dac_cal_value,
																	channels[1].cal_values.bias_dac_cal_value,
																	channels[2].cal_values.bias_dac_cal_value,
																	channels[3].cal_values.bias_dac_cal_value,
																	channels[4].cal_values.bias_dac_cal_value,
																	channels[5].cal_values.bias_dac_cal_value,
																	channels[6].cal_values.bias_dac_cal_value,
																	channels[7].cal_values.bias_dac_cal_value);

		printf("HWIS\t\t%.2f\t%.2f\t%.2f\t%.2f\t%.2f\t%.2f\t%.2f\t%.2f\t\n", channels[0].cal_values.hw_int_scale,
																			channels[1].cal_values.hw_int_scale,
																			channels[2].cal_values.hw_int_scale,
																			channels[3].cal_values.hw_int_scale,
																			channels[4].cal_values.hw_int_scale,
																			channels[5].cal_values.hw_int_scale,
																			channels[6].cal_values.hw_int_scale,
																			channels[7].cal_values.hw_int_scale);

		printf("HWIO\t\t%.2f\t%.2f\t%.2f\t%.2f\t%.2f\t%.2f\t%.2f\t%.2f\t\n", channels[0].cal_values.hw_int_offset,
															channels[1].cal_values.hw_int_offset,
															channels[2].cal_values.hw_int_offset,
															channels[3].cal_values.hw_int_offset,
															channels[4].cal_values.hw_int_offset,
															channels[5].cal_values.hw_int_offset,
															channels[6].cal_values.hw_int_offset,
															channels[7].cal_values.hw_int_offset);

		printf("LTEMP\t\t%.2f\t%.2f\t%.2f\t%.2f\t%.2f\t%.2f\t%.2f\t%.2f\t\n", channels[0].measure.local_temp,
																			channels[1].measure.local_temp,
																			channels[2].measure.local_temp,
																			channels[3].measure.local_temp,
																			channels[4].measure.local_temp,
																			channels[5].measure.local_temp,
																			channels[6].measure.local_temp,
																			channels[7].measure.local_temp);

		printf("RTEMP\t\t%.2f\t%.2f\t%.2f\t%.2f\t%.2f\t%.2f\t%.2f\t%.2f\t\n", channels[0].measure.remote_temp,
																				channels[1].measure.remote_temp,
																				channels[2].measure.remote_temp,
																				channels[3].measure.remote_temp,
																				channels[4].measure.remote_temp,
																				channels[5].measure.remote_temp,
																				channels[6].measure.remote_temp,
																				channels[7].measure.remote_temp);

		printf("==============================================================================\n");

//		GPIO_ToggleBits(BOARD_LED3);
		vTaskDelay(1000);
	}
}

uint16_t rf_channel_calibrate_input_interlock(uint8_t channel, int16_t start_value, uint8_t step)
{
	int16_t dacval = 0;

	if (start_value > 0 && start_value < 1700)
		dacval = start_value;
	else
		dacval = 1500;

	if (step == 0) step = 1;

	if (channel < 8)
	{
		printf("Calibrating ch %d start_value %d step %d\n", channel, dacval, step);

		if (lock_take(I2C_LOCK, portMAX_DELAY))
		{
			i2c_mux_select(channel);
			i2c_dual_dac_set(0, dacval);

			lock_free(I2C_LOCK);
		}

		vTaskSuspend(task_rf_interlock);
		vTaskDelay(100);
		rf_channel_enable_procedure(channel);
		vTaskDelay(100);
		rf_clear_interlock();

		while (dacval > 0) {

			printf("Trying value %d\n", dacval);
			if (lock_take(I2C_LOCK, portMAX_DELAY))
			{
				i2c_mux_select(channel);
				i2c_dual_dac_set(0, dacval);

				lock_free(I2C_LOCK);
			}

			if (dacval == 1500 || dacval == start_value) {
				// clear the issue of interlock popping
				// straight up after setting first value
				vTaskDelay(200);
				rf_clear_interlock();
			}

			vTaskDelay(200);

			led_bar_write(rf_channels_read_sigon(), (rf_channels_read_ovl()) & rf_channels_read_sigon(), (rf_channels_read_user()) & rf_channels_read_sigon());

			uint8_t val = 0;
			val = rf_channels_read_ovl();

			if ((val >> channel) & 0x01) {
				printf("Found interlock value = %d\n", dacval);

				rf_channel_disable_procedure(channel);
				vTaskDelay(100);
				led_bar_write(0, 0, 0);
				vTaskResume(task_rf_interlock);

				return (uint16_t) dacval;
				break;
			}

			vTaskDelay(200);
			dacval -= step;
		}

		rf_channel_disable_procedure(channel);
		vTaskDelay(100);
		led_bar_write(0, 0, 0);
		vTaskResume(task_rf_interlock);
	}

	return (uint16_t) dacval;
}

uint16_t rf_channel_calibrate_output_interlock(uint8_t channel, int16_t start_value, uint8_t step)
{
	int16_t dacval = 0;

	if (start_value > 0 && start_value < 1700)
		dacval = start_value;
	else
		dacval = 1500;

	if (step == 0) step = 1;

	if (channel < 8)
	{
		channels[channel].cal_values.output_dac_cal_value = dacval;

		printf("Calibrating ch %d start_value %d step %d\n", channel, dacval, step);

		vTaskSuspend(task_rf_interlock);
		vTaskDelay(100);
		rf_channel_enable_procedure(channel);
		vTaskDelay(100);
		rf_clear_interlock();

		while (dacval > 0) {

			printf("Trying value %d\n", dacval);
			if (lock_take(I2C_LOCK, portMAX_DELAY))
			{
				i2c_mux_select(channel);
				i2c_dual_dac_set(1, dacval);

				lock_free(I2C_LOCK);
			}

			if (dacval == 1500 || dacval == start_value) {
				// clear the issue of interlock popping
				// straight up after setting first value
				vTaskDelay(200);
				rf_clear_interlock();
			}

			vTaskDelay(200);

			led_bar_write(rf_channels_read_sigon(), (rf_channels_read_ovl()) & rf_channels_read_sigon(), (rf_channels_read_user()) & rf_channels_read_sigon());

			uint8_t val = 0;
			val = rf_channels_read_user();

			if ((val >> channel) & 0x01) {
				printf("Found interlock value = %d\n", dacval);
				rf_channel_disable_procedure(channel);
				vTaskDelay(100);
				led_bar_write(0, 0, 0);
				vTaskResume(task_rf_interlock);

				return (uint16_t) dacval;
				break;
			}

			vTaskDelay(200);
			dacval -= step;
		}

		rf_channel_disable_procedure(channel);
		vTaskDelay(100);
		led_bar_write(0, 0, 0);
		vTaskResume(task_rf_interlock);
	}

	return (uint16_t) dacval;
}

bool rf_channel_calibrate_bias(uint8_t channel, uint16_t current)
{
	int16_t dacval = 4095;

	if (channel < 8)
	{
		vTaskSuspend(task_rf_interlock);
		vTaskDelay(100);
		rf_channel_enable_procedure(channel);
		vTaskDelay(100);
		rf_clear_interlock();

		while (dacval > 0)
		{
			if (lock_take(I2C_LOCK, portMAX_DELAY))
			{
				i2c_mux_select(channel);
				i2c_dac_set(dacval);

				lock_free(I2C_LOCK);
			}

			vTaskDelay(100);

			uint16_t value;
			double avg_current = 0.0f;

			if (lock_take(I2C_LOCK, portMAX_DELAY))
			{
				i2c_mux_select(channel);
				for (int i = 0; i < 10; i++)
				{
					avg_current += (((ads7924_get_channel_voltage(0) / 50) / 0.02f) * 1000);
					vTaskDelay(20);
				}
				lock_free(I2C_LOCK);
			}

			value = (uint16_t) (avg_current / 10);
			printf("DAC %d CURRENT %d\n", dacval, value);

//			if (dacval == 4095 && val > current) {
//				printf("Wrong value! %d current %d\n", dacval, val);
//				eeprom_write16(BIAS_DAC_VALUE_ADDRESS, dacval);
//				channels[channel].cal_values.bias_dac_cal_value = dacval;
//
//				rf_channel_disable_procedure(channel);
//				vTaskResume(task_rf_interlock);
//
//				return false;
//			}

			if (value > current * 0.99 && value < current * 1.01) {
				printf("Found value %d current %d\n", dacval, value);
				rf_channel_disable_procedure(channel);
				vTaskResume(task_rf_interlock);

				eeprom_write16(BIAS_DAC_VALUE_ADDRESS, dacval);
				channels[channel].cal_values.bias_dac_cal_value = dacval;

				return true;
			}

			dacval -= 15;
		}
	}

	rf_channel_disable_procedure(channel);
	vTaskResume(task_rf_interlock);

	return false;
}
