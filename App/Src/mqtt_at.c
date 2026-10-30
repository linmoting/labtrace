/**
 * @file mqtt_at.c
 * @brief ESP8266 AT （应答） command execution, MQTT connection, and UART2 reception.
 */
#include "mqtt_at.h"

#include "app_types.h"
#include "app_tasks.h"
#include "cmsis_os2.h"
#include "bsp_KeyLedBuzzer.h"
#include "main.h"
#include "usart.h"

#include <stdio.h>
#include <string.h>

#define ESP_RX_BUFFER_SIZE 256U
#define MQTT_TELEMETRY_BUFFER_SIZE 384U
#define MQTT_RECONNECT_DELAY_MS 5000U

static uint8_t espRxByte;
static uint8_t espRxBuffer[ESP_RX_BUFFER_SIZE];
static volatile uint16_t espRxLength;

static void Debug_Print(const char *text);
static uint8_t ESP_BufferContains(const uint8_t *buffer,
                                  uint16_t bufferLength,
                                  const char *text);
static uint8_t ESP_SendCommand(const char *command,
                               const char *expected,
                               uint32_t timeoutMs);
static uint8_t ESP_StartAsyncReceive(void);
static uint8_t MQTT_ConnectAndSubscribe(void);
static uint8_t MQTT_PublishRecord(const StorageRecord_t *record);

/* 由 StorageTask 在 Flash 落盘后投递，ESP8266Task 独占 USART2 发布流程。 */
extern osMessageQueueId_t MqttPublishQueueHandle;
extern osMessageQueueId_t DeviceCommandQueueHandle;
							   
/** Send a debug string to the PC through USART1. */
static void Debug_Print(const char *text)
{
    HAL_UART_Transmit(&huart1, (uint8_t *)text, strlen(text), 1000U);
}

/**
 * Search received bytes by length. This still works when ESP8266 boot data
 * contains zero bytes, while strstr() would stop at the first zero byte.
 */
static uint8_t ESP_BufferContains(const uint8_t *buffer,uint16_t bufferLength,const char *text)
{
    uint16_t index;
    size_t textLength = strlen(text);

    if ((textLength == 0U) || (textLength > bufferLength))
    {
        return 0U;
    }

    for (index = 0U; index <= (uint16_t)(bufferLength - textLength); index++)
    {
        if (memcmp(&buffer[index], text, textLength) == 0)
        {
            return 1U;
        }
    }

    return 0U;
}

/**
 * Send one AT command and wait until the expected response text arrives.
 * A NULL expected value accepts any non-empty response.
 */
static uint8_t ESP_SendCommand(const char *command,const char *expected,uint32_t timeoutMs)
{
    uint16_t lastLength = 0U;
    uint32_t elapsedTime = 0U;
    uint32_t quietTime = 0U;

    HAL_UART_AbortReceive(&huart2);
    __HAL_UART_CLEAR_OREFLAG(&huart2);
    espRxLength = 0U;
    memset(espRxBuffer, 0, sizeof(espRxBuffer));

    if (HAL_UART_Receive_IT(&huart2, &espRxByte, 1U) != HAL_OK)
    {
        Debug_Print("[ESP RX] INTERRUPT START FAILED\r\n");
        return 0U;
    }

    Debug_Print("[ESP TX] ");
    Debug_Print(command);
    if ((strlen(command) < 2U) ||
        (strcmp(command + strlen(command) - 2U, "\r\n") != 0))
    {
        Debug_Print("\r\n");
    }

    if (HAL_UART_Transmit(&huart2, (uint8_t *)command, strlen(command), 1000U) != HAL_OK)
    {
        HAL_UART_AbortReceive(&huart2);
        Debug_Print("[ESP TX] SEND FAILED\r\n");
        return 0U;
    }

    while (elapsedTime < timeoutMs)
    {
        osDelay(10U);
        elapsedTime += 10U;

        if (espRxLength != lastLength)
        {
            lastLength = espRxLength;
            quietTime = 0U;
        }
        else if (espRxLength > 0U)
        {
            quietTime += 10U;
            if (quietTime >= 300U)
            {
                break;
            }
        }
    }

    HAL_UART_AbortReceive(&huart2);
    espRxBuffer[espRxLength] = '\0';

    if (espRxLength == 0U)
    {
        Debug_Print("[ESP RX] NO RESPONSE\r\n");
        return 0U;
    }

    Debug_Print("[ESP RX] ");
    HAL_UART_Transmit(&huart1, espRxBuffer, espRxLength, 1000U);
    Debug_Print("\r\n");

    if ((expected != NULL) && (strstr((char *)espRxBuffer, expected) == NULL))
    {
        Debug_Print("[ESP RX] EXPECTED TEXT NOT FOUND\r\n");
        return 0U;
    }

    return 1U;
}

/** Clear the command buffer and restart continuous USART2 interrupt reception. */
static uint8_t ESP_StartAsyncReceive(void)
{
    HAL_UART_AbortReceive(&huart2);
    __HAL_UART_CLEAR_OREFLAG(&huart2);
    espRxLength = 0U;
    memset(espRxBuffer, 0, sizeof(espRxBuffer));

    if (HAL_UART_Receive_IT(&huart2, &espRxByte, 1U) != HAL_OK)
    {
        Debug_Print("[ESP TASK] ASYNC RECEIVE START FAILED\r\n");
        return 0U;
    }
    return 1U;
}

/**
 * Execute one complete Wi-Fi/MQTT connection attempt.
 * The caller retries this function after a delay when any step fails.
 */
static uint8_t MQTT_ConnectAndSubscribe(void)
{
    Debug_Print("[ESP TASK] CONNECTING MQTT\r\n");

    if (!ESP_SendCommand("AT\r\n", "OK", 2000U))
    {
        Debug_Print("[ESP TASK] AT TEST FAILED\r\n");
        return 0U;
    }
    if (!ESP_SendCommand("ATE0\r\n", "OK", 2000U))
    {
        Debug_Print("[ESP TASK] ECHO OFF FAILED\r\n");
        return 0U;
    }
    if (!ESP_SendCommand("AT+CWJAP?\r\n", "+CWJAP:", 3000U))
    {
        Debug_Print("[ESP TASK] WIFI NOT CONNECTED\r\n");
        return 0U;
    }
    if (!ESP_SendCommand("AT+MQTTCLEAN=0\r\n", NULL, 3000U))
    {
        Debug_Print("[ESP TASK] MQTT CLEAN NO RESPONSE\r\n");
        return 0U;
    }
    if (!ESP_SendCommand("AT+MQTTUSERCFG=0,1,\"esp_stm32_001\",\"\",\"\",0,0,\"\"\r\n",
                         "OK", 3000U))
    {
        Debug_Print("[ESP TASK] MQTT USERCFG FAILED\r\n");
        return 0U;
    }
    if (!ESP_SendCommand("AT+MQTTCONNCFG=0,60,0,\"\",\"\",0,0\r\n",
                         "OK", 3000U))
    {
        Debug_Print("[ESP TASK] MQTT CONNCFG FAILED\r\n");
        return 0U;
    }
    if (!ESP_SendCommand("AT+MQTTCONN=0,\"broker.hivemq.com\",1883,0\r\n",
                         "OK", 20000U))
    {
        Debug_Print("[ESP TASK] MQTT CONNECT FAILED\r\n");
        return 0U;
    }
    if (!ESP_SendCommand("AT+MQTTCONN?\r\n", "+MQTTCONN:0,4", 3000U))
    {
        Debug_Print("[ESP TASK] MQTT STATE CHECK FAILED\r\n");
        return 0U;
    }
    if (!ESP_SendCommand("AT+MQTTSUB=0,\"***/program3/device/stm32_001/command\",1\r\n",
                         "OK", 5000U))
    {
        Debug_Print("[ESP TASK] COMMAND SUBSCRIBE FAILED\r\n");
        return 0U;
    }
    if (!ESP_SendCommand("AT+MQTTCONN?\r\n", "+MQTTCONN:0,6", 3000U))
    {
        Debug_Print("[ESP TASK] SUBSCRIBE STATE CHECK FAILED\r\n");
        return 0U;
    }
    if (ESP_StartAsyncReceive() == 0U)
    {
        return 0U;
    }

    StatusIndicator_Notify(STATUS_INDICATOR_CONNECTED);
    Debug_Print("[ESP TASK] MQTT READY, WAITING FOR TRACE RECORD\r\n");
    Debug_Print("[ESP TASK] WAITING FOR COMMAND\r\n");
    return 1U;
}

static uint8_t MQTT_PublishRecord(const StorageRecord_t *record)
{
    char telemetry[MQTT_TELEMETRY_BUFFER_SIZE];
    char publishCommand[180];
    int telemetryLength;
    int32_t weight10;
    int32_t weightBefore10;
    int32_t weightDelta10;
    int32_t temperature10;
    int32_t humidity10;

    if (record == NULL)
    {
        return 0U;
    }

    /* 避免依赖 Keil printf 的浮点格式支持，统一转成十分位整数输出。 */
    weight10 = (int32_t)(record->weightAfter * 10.0f);
    weightBefore10 = (int32_t)(record->weightBefore * 10.0f);
    weightDelta10 = (int32_t)(record->weightDelta * 10.0f);
    temperature10 = (int32_t)(record->sensorData.temperature * 10.0f);
    humidity10 = (int32_t)(record->sensorData.humidity * 10.0f);

    /* JSON 使用 ASCII 字段和值，ESP AT 固件可以稳定按字节长度发布。 */
    telemetryLength = snprintf(telemetry, sizeof(telemetry),
        "{\"type\":\"usage\",\"device_id\":\"stm32_001\",\"seq\":%lu,"
        "\"timestamp\":\"%s\",\"operator_uid\":\"%s\","
        "\"reagent_uid\":\"%s\",\"weight_before\":%ld.%ld,"
        "\"weight_after\":%ld.%ld,\"weight_delta\":%ld.%ld,"
        "\"temperature\":%ld.%ld,\"humidity\":%ld.%ld,"
        "\"distance_cm\":%u,\"bottle_present\":%u}",
        (unsigned long)record->sequence,
        record->sensorData.timestamp,
        record->operatorId,
        record->reagentId,
        (long)(weightBefore10 / 10L),
        (long)(weightBefore10 >= 0 ? weightBefore10 % 10L : -(weightBefore10 % 10L)),
        (long)(weight10 / 10L), (long)(weight10 >= 0 ? weight10 % 10L : -(weight10 % 10L)),
        (long)(weightDelta10 / 10L),
        (long)(weightDelta10 >= 0 ? weightDelta10 % 10L : -(weightDelta10 % 10L)),
        (long)(temperature10 / 10L), (long)(temperature10 >= 0 ? temperature10 % 10L : -(temperature10 % 10L)),
        (long)(humidity10 / 10L), (long)(humidity10 >= 0 ? humidity10 % 10L : -(humidity10 % 10L)),
        record->sensorData.distanceCm,
        record->sensorData.bottlePresent);

    if ((telemetryLength <= 0) || ((uint32_t)telemetryLength >= sizeof(telemetry)))
    {
        Debug_Print("[ESP MQTT] RECORD TOO LONG\r\n");
        return 0U;
    }

    (void)snprintf(publishCommand, sizeof(publishCommand),
        "AT+MQTTPUBRAW=0,\"***/program3/device/stm32_001/telemetry\",%d,1,0\r\n",
        telemetryLength);

    if (ESP_SendCommand(publishCommand, ">", 3000U) == 0U)
    {
        Debug_Print("[ESP MQTT] PUBLISH START FAILED\r\n");
        return 0U;
    }
    if (ESP_SendCommand(telemetry, "+MQTTPUB:OK", 5000U) == 0U)
    {
        Debug_Print("[ESP MQTT] RECORD PUBLISH FAILED\r\n");
        return 0U;
    }
	
    {
        StorageCommand_t command = {STORAGE_COMMAND_MARK_SENT, record->sequence};
        StorageTask_HandleCommand(&command);
    }
    StatusIndicator_Notify(STATUS_INDICATOR_DATA);
    Debug_Print("[ESP MQTT] TRACE RECORD PUBLISHED\r\n");
    return 1U;
}

/** Run the ESP8266 connection, publish, subscribe, and command reception flow. */
void MqttAt_RunTask(void *argument)
{
    uint8_t mqttReady = 0U;
    uint8_t hasPendingRecord = 0U;
    uint16_t asyncLastLength = 0U;
    uint16_t asyncQuietTime = 0U;
    StorageRecord_t record;

    (void)argument;
    Debug_Print("[ESP TASK] START\r\n");
    osDelay(3000U);

    for (;;)
    {
        if (mqttReady == 0U)
        {
            mqttReady = MQTT_ConnectAndSubscribe();
            if (mqttReady == 0U)
            {
                StatusIndicator_Notify(STATUS_INDICATOR_ERROR);
                Debug_Print("[ESP TASK] RECONNECT IN 5 SECONDS\r\n");
                osDelay(MQTT_RECONNECT_DELAY_MS);
            }
            asyncLastLength = 0U;
            asyncQuietTime = 0U;
            continue;
        }

        osDelay(10U);

        /* 消息队列：仅 ESP8266Task 发布，保证 AT 指令和串口接收不竞争。 */
        if ((hasPendingRecord != 0U) ||
            (osMessageQueueGet(MqttPublishQueueHandle, &record, NULL, 0U) == osOK))
        {
            hasPendingRecord = 1U;
            if (MQTT_PublishRecord(&record) != 0U)
            {
                hasPendingRecord = 0U;
                (void)ESP_StartAsyncReceive();
            }
            else
            {
                /* 当前记录保留在 record 中，重连后优先重发，再读取下一条队列数据。 */
                mqttReady = 0U;
                StatusIndicator_Notify(STATUS_INDICATOR_ERROR);
                Debug_Print("[ESP MQTT] RECORD RETAINED FOR RETRY\r\n");
            }
            asyncLastLength = 0U;
            asyncQuietTime = 0U;
            continue;
        }

        if (espRxLength != asyncLastLength)
        {
            asyncLastLength = espRxLength;
            asyncQuietTime = 0U;
        }
        else if (espRxLength > 0U)
        {
            asyncQuietTime += 10U;
        }

        if ((espRxLength > 0U) && (asyncQuietTime >= 100U))
        {
            uint8_t hasDisconnected;
            uint8_t hasReady;
            uint8_t hasSubscription;
            uint8_t hasWifiConnected;
            uint8_t hasWifiGotIp;

            HAL_UART_AbortReceive(&huart2);
            espRxBuffer[espRxLength] = '\0';
            hasDisconnected = ESP_BufferContains(espRxBuffer, espRxLength,
                                                  "+MQTTDISCONNECTED:");
            hasReady = ESP_BufferContains(espRxBuffer, espRxLength, "ready");
            hasSubscription = ESP_BufferContains(espRxBuffer, espRxLength,
                                                  "+MQTTSUBRECV:");
            hasWifiConnected = ESP_BufferContains(espRxBuffer, espRxLength,
                                                   "WIFI CONNECTED");
            hasWifiGotIp = ESP_BufferContains(espRxBuffer, espRxLength,
                                               "WIFI GOT IP");

            /* Only forward useful AT messages; ESP ROM boot bytes use 74880 baud. */
            if ((hasSubscription != 0U) || (hasDisconnected != 0U))
            {
                Debug_Print("[ESP MQTT RX] ");
                HAL_UART_Transmit(&huart1, espRxBuffer, espRxLength, 1000U);
                Debug_Print("\r\n");
            }
            if (hasWifiConnected != 0U)
            {
                Debug_Print("[ESP RX] WIFI CONNECTED\r\n");
            }
            if (hasWifiGotIp != 0U)
            {
                Debug_Print("[ESP RX] WIFI GOT IP\r\n");
            }

            if ((hasDisconnected != 0U) || (hasReady != 0U))
            {
                mqttReady = 0U;
                StatusIndicator_Notify(STATUS_INDICATOR_ERROR);
                if (hasReady != 0U)
                {
                    Debug_Print("[ESP TASK] ESP8266 RESTARTED, START RECONNECT\r\n");
                }
                else
                {
                    Debug_Print("[ESP TASK] MQTT DISCONNECTED, START RECONNECT\r\n");
                }
            }

            if ((mqttReady != 0U) &&(hasSubscription != 0U) &&(ESP_BufferContains(espRxBuffer, espRxLength,"**/program3/device/stm32_001/command") != 0U))
            {
                DeviceCommand_t command = DEVICE_COMMAND_NONE;
                uint8_t validCommandEnvelope;

                /*
                 * 控制消息必须属于本设备且类型正确。即使公共 Broker 中
                 * 存在旧的保留消息，也不能误触发舵机等执行部件。
                 */
                validCommandEnvelope =
                    (ESP_BufferContains(espRxBuffer, espRxLength,
                                        "\"type\":\"command\"") != 0U) &&
                    (ESP_BufferContains(espRxBuffer, espRxLength,
                                        "\"device_id\":\"stm32_001\"") != 0U);

                if ((validCommandEnvelope != 0U) &&
                    (ESP_BufferContains(espRxBuffer, espRxLength,
                                        "\"command\":\"open_door\"") != 0U))
                    command = DEVICE_COMMAND_OPEN_DOOR;
                else if ((validCommandEnvelope != 0U) &&
                         (ESP_BufferContains(espRxBuffer, espRxLength,
                                             "\"command\":\"close_door\"") != 0U))
                    command = DEVICE_COMMAND_CLOSE_DOOR;

                if (command != DEVICE_COMMAND_NONE)
                {
                    (void)osMessageQueuePut(DeviceCommandQueueHandle,
                                            &command, 0U, 0U);
                    Debug_Print("[ESP MQTT] DEVICE COMMAND QUEUED\r\n");
                }
                else
                {
                    Debug_Print("[ESP MQTT] INVALID COMMAND IGNORED\r\n");
                }
                StatusIndicator_Notify(STATUS_INDICATOR_DATA);
            }

            espRxLength = 0U;
            asyncLastLength = 0U;
            asyncQuietTime = 0U;
            memset(espRxBuffer, 0, sizeof(espRxBuffer));
            __HAL_UART_CLEAR_OREFLAG(&huart2);
            if (mqttReady != 0U)
            {
                (void)HAL_UART_Receive_IT(&huart2, &espRxByte, 1U);
            }
        }
    }
}

/** Receive one USART2 byte and continue the interrupt reception. */
void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
    if (huart->Instance == USART2)
    {
        if (espRxLength < (ESP_RX_BUFFER_SIZE - 1U))
        {
            espRxBuffer[espRxLength++] = espRxByte;
        }
        HAL_UART_Receive_IT(&huart2, &espRxByte, 1U);
    }
}

/** Recover USART2 reception after a UART noise or overrun error. */
void HAL_UART_ErrorCallback(UART_HandleTypeDef *huart)
{
    if (huart->Instance == USART2)
    {
        __HAL_UART_CLEAR_OREFLAG(&huart2);
        HAL_UART_Receive_IT(&huart2, &espRxByte, 1U);
    }
}
