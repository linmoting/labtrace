/**
 * @file app_tasks.c
 * @brief 实验室试剂追溯系统的 FreeRTOS 任务与第一版业务流程。
 * @details RfidTask 产生刷卡事件，TraceTask 驱动业务状态机，SensorTask 按通知
 *          采集传感器快照，StorageTask 将完整记录写入 W25Q64。
 */
#include "app_tasks.h"
#include "app_types.h"
#include "cmsis_os2.h"
#include "bsp_KeyLedBuzzer.h"
#include "bsp_dht11.h"
#include "bsp_ds3231.h"
#include "bsp_hc_sr04.h"
#include "bsp_hx711.h"
#include "bsp_rc522.h"
#include "bsp_servo.h"
#include "bsp_w25qxx.h"
#include "iwdg.h"
#include "usart.h"
#include <stdio.h>
#include <string.h>

#define BOTTLE_DETECT_DISTANCE_CM 15U
#define BOTTLE_CONFIRM_COUNT 3U
#define WEIGHT_CHANGE_DEADBAND_G 1.0f
#define SCALE_SETTLE_TIME_MS 800U
#define WEIGHT_READ_RETRY_COUNT 3U
#define SENSOR_RESULT_TIMEOUT_MS 7000U
#define STORAGE_LOG_BASE_ADDRESS 0x00010000UL
#define STORAGE_LOG_SLOT_SIZE 128U
#define STORAGE_LOG_SLOT_COUNT 32U
#define TASK_STACK_WARNING_BYTES 64U
#define STACK_REPORT_INTERVAL_MS 10000U

#define SLOT_MARKER_OFFSET 0U
#define SLOT_MAGIC_OFFSET 1U
#define SLOT_LENGTH_OFFSET 5U
#define SLOT_SEQUENCE_OFFSET 7U
#define SLOT_RECORD_OFFSET 11U
#define SLOT_CRC_OFFSET (SLOT_RECORD_OFFSET + sizeof(StorageRecord_t))
#define SLOT_MAGIC 0x31525453UL
#define SLOT_PENDING 0xA5U
#define SLOT_SENT 0x00U
static uint32_t nextWriteSlot = STORAGE_LOG_SLOT_COUNT;
static uint32_t nextRecordSequence = 1U;
static uint8_t storageHasPending;
static uint8_t pendingQueued[STORAGE_LOG_SLOT_COUNT];

extern osMessageQueueId_t RfidEventQueueHandle;
extern osMessageQueueId_t SensorDataQueueHandle;
extern osMessageQueueId_t StorageQueueHandle;
extern osMessageQueueId_t MqttPublishQueueHandle;
extern osMessageQueueId_t DeviceCommandQueueHandle;
extern osMessageQueueId_t StorageCommandQueueHandle;
extern osMutexId_t SpiMutexHandle;
extern osSemaphoreId_t SensorReadySemHandle;
extern osThreadId_t ESP8266TaskHandle;
extern osThreadId_t SensorTaskHandle;
extern osThreadId_t RfidTaskHandle;
extern osThreadId_t TraceTaskHandle;
extern osThreadId_t StorageTaskHandle;

static void App_Print(const char *text)
{
    /* 串口助手按 CRLF 换行；必须一次完整发送，不能拆成两次。 */
    HAL_UART_Transmit(&huart1, (uint8_t *)text, (uint16_t)strlen(text), 1000U);
}

static void App_FormatUid(char *text, size_t textSize, const uint8_t uid[4])
{
    (void)snprintf(text, textSize, "%02X%02X%02X%02X", uid[0], uid[1], uid[2], uid[3]);
}

static void App_CopyFixedTime(char timestamp[20])
{
    /* DS3231 到货前使用固定值；之后只需在此处调用 DS3231_ReadDateTime。 */
    strcpy(timestamp, APP_FIXED_TIMESTAMP);
}

static void App_PrintRecord(const StorageRecord_t *record)
{
    char message[240];
    int32_t temperature10 = (int32_t)(record->sensorData.temperature * 10.0f);
    int32_t humidity10 = (int32_t)(record->sensorData.humidity * 10.0f);
    int32_t weightBefore10 = (int32_t)(record->weightBefore * 10.0f);
    int32_t weightAfter10 = (int32_t)(record->weightAfter * 10.0f);
    int32_t weightDelta10 = (int32_t)(record->weightDelta * 10.0f);

    (void)snprintf(message, sizeof(message),
                   "[记录] operator=%s reagent=%s time=%s "
                   "before=%ld.%ldg after=%ld.%ldg used=%ld.%ldg "
                   "temperature=%ld.%ldC humidity=%ld.%ld%% distance=%ucm\r\n",
                   record->operatorId, record->reagentId, record->sensorData.timestamp,
                   (long)(weightBefore10 / 10L),
                   (long)(weightBefore10 >= 0 ? weightBefore10 % 10L : -(weightBefore10 % 10L)),
                   (long)(weightAfter10 / 10L),
                   (long)(weightAfter10 >= 0 ? weightAfter10 % 10L : -(weightAfter10 % 10L)),
                   (long)(weightDelta10 / 10L),
                   (long)(weightDelta10 >= 0 ? weightDelta10 % 10L : -(weightDelta10 % 10L)),
                   (long)(temperature10 / 10L), (long)(temperature10 % 10L),
                   (long)(humidity10 / 10L), (long)(humidity10 % 10L),
                   record->sensorData.distanceCm);
    App_Print(message);
}

static void App_PrintWeight(const char *label, float weight)
{
    char message[96];
    int32_t weight10 = (int32_t)(weight * 10.0f);

    (void)snprintf(message, sizeof(message),
                   "[流程] %s: %ld.%ldg\r\n", label,
                   (long)(weight10 / 10L),
                   (long)(weight10 >= 0 ? weight10 % 10L : -(weight10 % 10L)));
    App_Print(message);
}

static uint32_t StorageCache_Crc32(const uint8_t *data, uint32_t length)
{
    uint32_t crc = 0xFFFFFFFFUL;
    uint32_t i;
    uint8_t bit;

    for (i = 0U; i < length; ++i)
    {
        crc ^= data[i];
        for (bit = 0U; bit < 8U; ++bit)
            crc = (crc >> 1U) ^ ((crc & 1U) ? 0xEDB88320UL : 0U);
    }
    return ~crc;
}

static uint8_t StorageCache_ReadSlot(uint32_t slot, uint8_t *marker,
                                     StorageRecord_t *record)
{
    uint8_t header[10];
    uint32_t address = STORAGE_LOG_BASE_ADDRESS + slot * STORAGE_LOG_SLOT_SIZE;
    uint32_t magic;
    uint16_t length;
    uint32_t sequence;
    uint32_t expectedCrc;
    uint32_t actualCrc;
    uint8_t crcData[4U + sizeof(StorageRecord_t)];

    if (W25QXX_Read(address, marker, 1U) == 0U) return 0U;
    if (*marker == 0xFFU) return 1U;
    if ((*marker != SLOT_PENDING) && (*marker != SLOT_SENT)) return 0U;
    if (W25QXX_Read(address + SLOT_MAGIC_OFFSET, header, sizeof(header)) == 0U) return 0U;
    memcpy(&magic, &header[0], sizeof(magic));
    memcpy(&length, &header[4], sizeof(length));
    memcpy(&sequence, &header[6], sizeof(sequence));
    if ((magic != SLOT_MAGIC) || (length != sizeof(StorageRecord_t))) return 0U;
    if (W25QXX_Read(address + SLOT_RECORD_OFFSET, (uint8_t *)record, length) == 0U) return 0U;
    memcpy(&crcData[0], &sequence, sizeof(sequence));
    memcpy(&crcData[4], record, sizeof(StorageRecord_t));
    if (W25QXX_Read(address + SLOT_CRC_OFFSET, (uint8_t *)&expectedCrc,
                    sizeof(expectedCrc)) == 0U) return 0U;
    actualCrc = StorageCache_Crc32(crcData, sizeof(crcData));
    return (actualCrc == expectedCrc && record->sequence == sequence) ? 1U : 0U;
}

static void StorageCache_QueueUnqueuedPending(void)
{
    uint32_t slot;
    uint8_t marker;
    StorageRecord_t record;

    for (slot = 0U; slot < STORAGE_LOG_SLOT_COUNT; ++slot)
    {
        if (pendingQueued[slot] != 0U) continue;
        if ((StorageCache_ReadSlot(slot, &marker, &record) != 0U) &&
            (marker == SLOT_PENDING) &&
            (osMessageQueuePut(MqttPublishQueueHandle, &record, 0U, 0U) == osOK))
            pendingQueued[slot] = 1U;
    }
}

/* 启动扫描恢复 pending 队列和首个从未写入的槽位。 */
static void StorageCache_ScanOnBoot(void)
{
    uint32_t slot;
    uint8_t marker;
    uint8_t slotData[STORAGE_LOG_SLOT_SIZE];
    StorageRecord_t record;
    nextWriteSlot = STORAGE_LOG_SLOT_COUNT;
    nextRecordSequence = 1U;
    storageHasPending = 0U;
    memset(pendingQueued, 0, sizeof(pendingQueued));

    for (slot = 0U; slot < STORAGE_LOG_SLOT_COUNT; ++slot)
    {
        if (W25QXX_Read(STORAGE_LOG_BASE_ADDRESS + slot * STORAGE_LOG_SLOT_SIZE,
                        slotData, sizeof(slotData)) == 0U) continue;
        marker = slotData[SLOT_MARKER_OFFSET];
        if (marker == 0xFFU)
        {
            uint16_t i;
            uint8_t blank = 1U;
            for (i = 0U; i < sizeof(slotData); ++i)
            {
                if (slotData[i] != 0xFFU) { blank = 0U; break; }
            }
            if ((blank != 0U) && (nextWriteSlot == STORAGE_LOG_SLOT_COUNT))
                nextWriteSlot = slot;
            continue;
        }
        if (StorageCache_ReadSlot(slot, &marker, &record) == 0U)
        {
            App_Print("[存储] 检测到无效槽位，保留现场并跳过\r\n");
            storageHasPending = 1U;
            continue;
        }
        if (marker == SLOT_PENDING)
        {
            storageHasPending = 1U;
            if (osMessageQueuePut(MqttPublishQueueHandle, &record, 0U, 0U) == osOK)
                pendingQueued[slot] = 1U;
        }
        if (record.sequence >= nextRecordSequence)
            nextRecordSequence = record.sequence + 1U;
    }
}

/* 完整写入并回读校验后，才允许把记录交给 MQTT。 */
static uint8_t StorageCache_WritePending(const StorageRecord_t *record)
{
    uint8_t buffer[STORAGE_LOG_SLOT_SIZE];
    uint32_t magic = SLOT_MAGIC;
    uint16_t length = sizeof(StorageRecord_t);
    uint32_t crc;
    uint32_t address;
    uint32_t slot;
    StorageRecord_t verifyRecord;
    uint8_t marker;
    uint8_t hasUnconfirmedRecords = storageHasPending;

    if (nextWriteSlot >= STORAGE_LOG_SLOT_COUNT)
    {
        if (hasUnconfirmedRecords != 0U)
        {
            App_Print("[存储] 缓存区已满且仍有待确认记录，拒绝覆盖\r\n");
            return 0U;
        }
        if (W25QXX_EraseSector(STORAGE_LOG_BASE_ADDRESS) == 0U) return 0U;
        nextWriteSlot = 0U;
    }

    memset(buffer, 0xFF, sizeof(buffer));
    memcpy(&buffer[SLOT_MAGIC_OFFSET], &magic, sizeof(magic));
    memcpy(&buffer[SLOT_LENGTH_OFFSET], &length, sizeof(length));
    memcpy(&buffer[SLOT_SEQUENCE_OFFSET], &record->sequence, sizeof(record->sequence));
    memcpy(&buffer[SLOT_RECORD_OFFSET], record, sizeof(*record));
    {
        uint8_t crcData[4U + sizeof(StorageRecord_t)];
        memcpy(&crcData[0], &record->sequence, sizeof(record->sequence));
        memcpy(&crcData[4], record, sizeof(*record));
        crc = StorageCache_Crc32(crcData, sizeof(crcData));
    }
    memcpy(&buffer[SLOT_CRC_OFFSET], &crc, sizeof(crc));
    address = STORAGE_LOG_BASE_ADDRESS + nextWriteSlot * STORAGE_LOG_SLOT_SIZE;
    if (W25QXX_PageProgram(address + SLOT_MAGIC_OFFSET, &buffer[SLOT_MAGIC_OFFSET],
                           (uint16_t)(SLOT_CRC_OFFSET + sizeof(crc) - SLOT_MAGIC_OFFSET)) == 0U)
    {
        App_Print("[存储] 槽位主体写入失败\r\n");
        return 0U;
    }
    marker = SLOT_PENDING;
    if (W25QXX_PageProgram(address, &marker, 1U) == 0U ||
        StorageCache_ReadSlot(nextWriteSlot, &marker, &verifyRecord) == 0U ||
        marker != SLOT_PENDING || verifyRecord.sequence != record->sequence)
    {
        App_Print("[存储] 槽位回读校验失败\r\n");
        return 0U;
    }
    ++nextWriteSlot;
    storageHasPending = 1U;
    return 1U;
}

static void StorageCache_MarkSent(uint32_t sequence)
{
    uint32_t slot;
    StorageRecord_t rec;
    uint8_t marker;
    uint8_t matched = 0U;
    uint8_t markFailed = 0U;

    for (slot = 0U; slot < STORAGE_LOG_SLOT_COUNT; slot++)
    {
        if (StorageCache_ReadSlot(slot, &marker, &rec) == 0U) continue;
        if ((marker == SLOT_PENDING) && (rec.sequence == sequence))
        {
            uint8_t sent = SLOT_SENT;
            matched = 1U;
            if (W25QXX_PageProgram(STORAGE_LOG_BASE_ADDRESS + slot * STORAGE_LOG_SLOT_SIZE,
                                   &sent, 1U) == 0U)
            {
                App_Print("[存储] sent 状态更新失败，记录将重启后重发\r\n");
                markFailed = 1U;
            }
        }
    }
    if ((matched != 0U) && (markFailed == 0U))
    {
        storageHasPending = 0U;
        for (slot = 0U; slot < STORAGE_LOG_SLOT_COUNT; ++slot)
        {
            if (StorageCache_ReadSlot(slot, &marker, &rec) == 0U)
            {
                uint8_t rawMarker;
                if ((W25QXX_Read(STORAGE_LOG_BASE_ADDRESS +
                                  slot * STORAGE_LOG_SLOT_SIZE,
                                  &rawMarker, 1U) != 0U) && (rawMarker != 0xFFU))
                    storageHasPending = 1U;
            }
            else if (marker == SLOT_PENDING)
            {
                storageHasPending = 1U;
            }
        }
        if (storageHasPending == 0U) memset(pendingQueued, 0, sizeof(pendingQueued));
        else StorageCache_QueueUnqueuedPending();
    }
}

void AppTask_TestDs3231Once(void)
{
    static uint8_t tested = 0U;
    DS3231_DateTime_t dateTime;
    char message[80];

    if (tested != 0U) return;
    tested = 1U;
    if ((DS3231_IsReady() == 0U) || (DS3231_ReadDateTime(&dateTime) == 0U))
    {
        App_Print("[测试] DS3231 未连接，当前使用固定时间\r\n");
        return;
    }
    (void)snprintf(message, sizeof(message),
                   "[测试] DS3231 读取成功: 20%02u-%02u-%02u %02u:%02u:%02u\r\n",
                   dateTime.year, dateTime.month, dateTime.date,
                   dateTime.hours, dateTime.minutes, dateTime.seconds);
    App_Print(message);
}

void AppTask_RunSensor(void *argument)
{
    SensorData_t data;
    DHT11_Data_t dht;
    uint32_t distance;
    float weight;
    uint8_t attempt;
    (void)argument;
    HCSR04_Init();
    DHT11_Init();
    HX711_Init();
    App_Print("[系统] 传感器任务初始化成功\r\n");

    for (;;)
    {
        /* 二值信号量：TraceTask 确认试剂卡后才允许采集完整快照。 */
        (void)osSemaphoreAcquire(SensorReadySemHandle, osWaitForever);
        memset(&data, 0, sizeof(data));
        App_CopyFixedTime(data.timestamp);

        /* 放置或放回瓶子后先等待机械振动衰减，再进行真正的稳定性判断。 */
        osDelay(SCALE_SETTLE_TIME_MS);
        for (attempt = 0U; attempt < WEIGHT_READ_RETRY_COUNT; ++attempt)
        {
            if (HX711_ReadStableWeightGram(&weight) != 0U)
            {
                break;
            }
            App_Print("[流程] HX711 重量异常，请勿触碰称台，正在重新测量\r\n");
            osDelay(200U);
        }
        if (attempt >= WEIGHT_READ_RETRY_COUNT)
        {
            App_Print("[流程] HX711 连续三次读取失败，本次采集取消\r\n");
            continue;
        }
        data.weight = weight;
        App_Print("[流程] HX711 重量稳定读取成功\r\n");
        if (DHT11_Read(&dht) == 0U)
        {
            App_Print("[流程] DHT11 温湿度读取失败\r\n");
            continue;
        }
        data.temperature = (float)dht.temperatureInteger + (float)dht.temperatureDecimal / 10.0f;
        data.humidity = (float)dht.humidityInteger + (float)dht.humidityDecimal / 10.0f;
        App_Print("[流程] DHT11 温湿度读取成功\r\n");
        if (HCSR04_ReadMedianDistanceCm(&distance) == 0U)
        {
            App_Print("[流程] HC-SR04 距离复核失败\r\n");
            continue;
        }
        data.distanceCm = (uint16_t)distance;
        data.bottlePresent = (distance <= BOTTLE_DETECT_DISTANCE_CM) ? 1U : 0U;
        /* 消息队列：将完整传感器快照安全交给 TraceTask。 */
        if (osMessageQueuePut(SensorDataQueueHandle, &data, 0U, 0U) == osOK)
            App_Print("[流程] 传感器数据入队成功\r\n");
    }
}

void AppTask_RunRfid(void *argument)
{
    uint8_t uid[4], lastUid[4] = {0U};
    uint8_t hasLastUid = 0U;
    RfidEvent_t event;
    RC522_CardType_t cardType;
    RC522_Status_t status;
    (void)argument;
    /* SPI 互斥锁：统一保护 RC522/W25Q64 的 SPI 访问。 */
    (void)osMutexAcquire(SpiMutexHandle, osWaitForever);
    status = (RC522_Init() != 0U) ? RC522_STATUS_OK : RC522_STATUS_ERROR;
    (void)osMutexRelease(SpiMutexHandle);
    App_Print((status == RC522_STATUS_OK) ? "[系统] RC522 初始化成功\r\n" : "[系统] RC522 初始化失败\r\n");

    for (;;)
    {
        if (osMutexAcquire(SpiMutexHandle, 100U) == osOK)
        {
            status = RC522_ReadCard(uid, &cardType);
            (void)osMutexRelease(SpiMutexHandle);
        }
        else status = RC522_STATUS_ERROR;
        if (status == RC522_STATUS_NO_CARD) hasLastUid = 0U;
        else if ((status == RC522_STATUS_OK) && ((hasLastUid == 0U) || (memcmp(uid, lastUid, 4U) != 0)))
        {
            memcpy(lastUid, uid, 4U);
            hasLastUid = 1U;
            memset(&event, 0, sizeof(event));
            App_FormatUid(event.cardId, sizeof(event.cardId), uid);
            event.cardType = (uint8_t)cardType;
            App_CopyFixedTime(event.timestamp);
            /* RFID 事件队列：读卡任务不直接控制舵机或业务状态。 */
            if (osMessageQueuePut(RfidEventQueueHandle, &event, 0U, 0U) == osOK)
            {
                App_Print((cardType == RC522_CARD_OPERATOR) ? "[流程] 操作员卡识别成功\r\n" :
                          (cardType == RC522_CARD_REAGENT) ? "[流程] 试剂瓶卡识别成功\r\n" :
                          "[流程] 未登记卡已忽略\r\n");
            }
        }
        osDelay(100U);
    }
}

void AppTask_RunTrace(void *argument)
{
    typedef enum
    {
        TRACE_WAIT_OPERATOR = 0,
        TRACE_WAIT_BOTTLE_BEFORE,
        TRACE_WAIT_REAGENT_BEFORE,
        TRACE_WAIT_SENSOR_BEFORE,
        TRACE_WAIT_BOTTLE_REMOVED,
        TRACE_WAIT_BOTTLE_RETURNED,
        TRACE_WAIT_REAGENT_AFTER,
        TRACE_WAIT_SENSOR_AFTER
    } TraceState_t;
    TraceState_t state = TRACE_WAIT_OPERATOR;
    RfidEvent_t event;
    SensorData_t sensor;
    StorageRecord_t record;
    char operatorId[16] = {0};
    char reagentId[16] = {0};
    float weightBefore = 0.0f;
    uint32_t distance;
    uint8_t detectedCount = 0U;
    DeviceCommand_t command;
    (void)argument;
    if (Servo_Init() != 0U) { Servo_Close(); App_Print("[系统] 舵机初始化成功\r\n"); }
    App_Print("[流程] 等待操作员刷员工卡\r\n");

    for (;;)
    {
        /* 控制命令队列：舵机始终由 TraceTask 操作，避免跨任务竞争。 */
        if (osMessageQueueGet(DeviceCommandQueueHandle, &command, NULL, 0U) == osOK)
        {
            if (command == DEVICE_COMMAND_OPEN_DOOR)
            {
                Servo_Open();
                App_Print("[控制] MQTT 远程开门执行成功\r\n");
            }
            else if (command == DEVICE_COMMAND_CLOSE_DOOR)
            {
                Servo_Close();
                App_Print("[控制] MQTT 远程关门执行成功\r\n");
            }
        }

        if (state == TRACE_WAIT_OPERATOR)
        {
            if ((osMessageQueueGet(RfidEventQueueHandle, &event, NULL, 100U) == osOK) &&
                (event.cardType == (uint8_t)RC522_CARD_OPERATOR))
            {
                strcpy(operatorId, event.cardId);
                Servo_Open();
                App_Print("[流程] 操作员身份匹配成功，舵机开门成功\r\n");
                App_Print("[流程] 请将试剂瓶放到称重台\r\n");
                detectedCount = 0U;
                state = TRACE_WAIT_BOTTLE_BEFORE;
            }
        }
        else if (state == TRACE_WAIT_BOTTLE_BEFORE)
        {
            if ((HCSR04_ReadMedianDistanceCm(&distance) != 0U) && (distance <= BOTTLE_DETECT_DISTANCE_CM)) ++detectedCount;
            else detectedCount = 0U;
            if (detectedCount >= BOTTLE_CONFIRM_COUNT)
            {
                App_Print("[流程] 已检测到试剂瓶，请刷试剂瓶卡\r\n");
                state = TRACE_WAIT_REAGENT_BEFORE;
            }
            osDelay(100U);
        }
        else if (state == TRACE_WAIT_REAGENT_BEFORE)
        {
            if ((osMessageQueueGet(RfidEventQueueHandle, &event, NULL, 100U) == osOK) &&
                (event.cardType == (uint8_t)RC522_CARD_REAGENT))
            {
                strcpy(reagentId, event.cardId);
                App_Print("[流程] 试剂瓶身份确认成功，开始领用前称重\r\n");
                /* 第一次释放信号量：请求 SensorTask 采集领用前快照。 */
                (void)osSemaphoreRelease(SensorReadySemHandle);
                state = TRACE_WAIT_SENSOR_BEFORE;
            }
        }
        else if (state == TRACE_WAIT_SENSOR_BEFORE)
        {
            if (osMessageQueueGet(SensorDataQueueHandle, &sensor, NULL,
                                  SENSOR_RESULT_TIMEOUT_MS) != osOK)
            {
                App_Print("[流程] 领用前称重超时，请重新刷试剂瓶卡\r\n");
                state = TRACE_WAIT_REAGENT_BEFORE;
            }
            else if (sensor.bottlePresent == 0U)
            {
                App_Print("[流程] 领用前复核未检测到试剂瓶，请重新放置\r\n");
                detectedCount = 0U;
                state = TRACE_WAIT_BOTTLE_BEFORE;
            }
            else
            {
                weightBefore = sensor.weight;
                App_PrintWeight("weight before", weightBefore);
                App_Print("[流程] 领用前重量记录成功，请取走试剂瓶并使用\r\n");
                detectedCount = 0U;
                state = TRACE_WAIT_BOTTLE_REMOVED;
            }
        }
        else if (state == TRACE_WAIT_BOTTLE_REMOVED)
        {
            /* 无瓶时可能测到远处背景，也可能没有回波而超时，两者都表示瓶子已取走。 */
            if ((HCSR04_ReadMedianDistanceCm(&distance) == 0U) ||
                (distance > BOTTLE_DETECT_DISTANCE_CM))
                ++detectedCount;
            else
                detectedCount = 0U;

            if (detectedCount >= BOTTLE_CONFIRM_COUNT)
            {
                App_Print("[流程] 已检测到试剂瓶取走，使用后请放回称重台\r\n");
                detectedCount = 0U;
                state = TRACE_WAIT_BOTTLE_RETURNED;
            }
            osDelay(100U);
        }
        else if (state == TRACE_WAIT_BOTTLE_RETURNED)
        {
            if ((HCSR04_ReadMedianDistanceCm(&distance) != 0U) &&
                (distance <= BOTTLE_DETECT_DISTANCE_CM))
                ++detectedCount;
            else
                detectedCount = 0U;

            if (detectedCount >= BOTTLE_CONFIRM_COUNT)
            {
                App_Print("[流程] 已检测到试剂瓶放回，请再次刷同一张试剂瓶卡\r\n");
                state = TRACE_WAIT_REAGENT_AFTER;
            }
            osDelay(100U);
        }
        else if (state == TRACE_WAIT_REAGENT_AFTER)
        {
            if ((osMessageQueueGet(RfidEventQueueHandle, &event, NULL, 100U) == osOK) &&
                (event.cardType == (uint8_t)RC522_CARD_REAGENT))
            {
                if (strcmp(event.cardId, reagentId) != 0)
                {
                    App_Print("[流程] 试剂瓶卡不一致，请放回原试剂瓶并重新刷卡\r\n");
                }
                else
                {
                    App_Print("[流程] 试剂瓶复核成功，开始领用后称重\r\n");
                    /* 第二次释放信号量：请求 SensorTask 采集领用后快照。 */
                    (void)osSemaphoreRelease(SensorReadySemHandle);
                    state = TRACE_WAIT_SENSOR_AFTER;
                }
            }
        }
        else if (state == TRACE_WAIT_SENSOR_AFTER)
        {
            if (osMessageQueueGet(SensorDataQueueHandle, &sensor, NULL,
                                  SENSOR_RESULT_TIMEOUT_MS) != osOK)
            {
                App_Print("[流程] 领用后称重超时，请重新刷试剂瓶卡\r\n");
                state = TRACE_WAIT_REAGENT_AFTER;
            }
            else if (sensor.bottlePresent == 0U)
            {
                App_Print("[流程] 领用后复核未检测到试剂瓶，请重新放回\r\n");
                detectedCount = 0U;
                state = TRACE_WAIT_BOTTLE_RETURNED;
            }
            else
            {
                memset(&record, 0, sizeof(record));
                record.sensorData = sensor;
                record.weightBefore = weightBefore;
                record.weightAfter = sensor.weight;
                record.weightDelta = record.weightBefore - record.weightAfter;
                if ((record.weightDelta > -WEIGHT_CHANGE_DEADBAND_G) &&
                    (record.weightDelta < WEIGHT_CHANGE_DEADBAND_G))
                    record.weightDelta = 0.0f;
                App_PrintWeight("weight after", record.weightAfter);
                App_PrintWeight("weight used", record.weightDelta);
                record.sequence = 0U; /* StorageTask 分配并持久化设备内递增序号。 */
                strcpy(record.operatorId, operatorId);
                strcpy(record.reagentId, reagentId);
                App_PrintRecord(&record);

                /* 存储队列：TraceTask 不等待 Flash 擦写。 */
                if (osMessageQueuePut(StorageQueueHandle, &record, 0U, 0U) == osOK)
                {
                    App_Print("[流程] 记录已提交存储任务，等待 Flash 落盘\r\n");
                }
                else
                {
                    App_Print("[流程] W25Q64 存储队列已满，记录未提交\r\n");
                }

                Servo_Close();
                App_Print("[流程] 舵机关门成功，本次领用流程完成\r\n");
                App_Print("[流程] 等待下一位操作员刷员工卡\r\n");
                state = TRACE_WAIT_OPERATOR;
            }
        }
    }
}

void AppTask_RunStorage(void *argument)
{
    StorageRecord_t record;
    StorageCommand_t command;

    (void)argument;
    (void)osMutexAcquire(SpiMutexHandle, osWaitForever);
    W25QXX_Init();
    StorageCache_ScanOnBoot();
    (void)osMutexRelease(SpiMutexHandle);
    App_Print("[系统] W25Q64 存储任务初始化成功\r\n");
    for (;;)
    {
        if (osMessageQueueGet(StorageCommandQueueHandle, &command, NULL, 0U) == osOK)
        {
            (void)osMutexAcquire(SpiMutexHandle, osWaitForever);
            if (command.type == STORAGE_COMMAND_MARK_SENT)
                StorageCache_MarkSent(command.sequence);
            (void)osMutexRelease(SpiMutexHandle);
        }
        else if (osMessageQueueGet(StorageQueueHandle, &record, NULL, 20U) == osOK)
        {
            uint8_t stored;
            record.sequence = nextRecordSequence++;
            (void)osMutexAcquire(SpiMutexHandle, osWaitForever);
            stored = StorageCache_WritePending(&record);
            if (stored != 0U)
            {
                pendingQueued[nextWriteSlot - 1U] = 0U;
            }
            (void)osMutexRelease(SpiMutexHandle);
            if (stored != 0U)
            {
                if (osMessageQueuePut(MqttPublishQueueHandle, &record, 0U, 1000U) == osOK)
                {
                    pendingQueued[nextWriteSlot - 1U] = 1U;
                    App_Print("[存储] Flash 校验成功，记录进入 MQTT 队列\r\n");
                }
                else
                    App_Print("[存储] 记录已落盘，MQTT 队列满，重启后可补发\r\n");
            }
            else
            {
                App_Print("[存储] 记录未能落盘，本次不允许上报\r\n");
                StatusIndicator_Notify(STATUS_INDICATOR_ERROR);
            }
        }
    }
}

void StorageTask_HandleCommand(const StorageCommand_t *command)
{
    if ((command != NULL) && (StorageCommandQueueHandle != NULL))
        (void)osMessageQueuePut(StorageCommandQueueHandle, command, 0U, 0U);
}

void AppTask_RunHealth(void *argument)
{
    const osThreadId_t monitoredTasks[] =
    {
        ESP8266TaskHandle,
        SensorTaskHandle,
        RfidTaskHandle,
        TraceTaskHandle,
        StorageTaskHandle
    };
    const char *taskNames[] =
    {
        "ESP8266Task", "SensorTask", "RfidTask", "TraceTask", "StorageTask"
    };
    uint32_t elapsedMs = 0U;
    uint32_t index;
    char message[96];

    (void)argument;
    StatusIndicator_Init();
    App_Print("\r\n[系统] 试剂追溯终端启动\r\n");
    App_Print("[系统] DS3231 暂用固定时间: " APP_FIXED_TIMESTAMP "\r\n");

    for (;;)
    {
        StatusIndicator_Process();
        HAL_IWDG_Refresh(&hiwdg);
        osDelay(20U);
        elapsedMs += 20U;

        if (elapsedMs >= STACK_REPORT_INTERVAL_MS)
        {
            elapsedMs = 0U;
            for (index = 0U; index < (sizeof(monitoredTasks) / sizeof(monitoredTasks[0])); ++index)
            {
                uint32_t freeStackBytes = osThreadGetStackSpace(monitoredTasks[index]);
                if (freeStackBytes < TASK_STACK_WARNING_BYTES)
                {
                    (void)snprintf(message, sizeof(message),
                                   "[Health] %s stack low: %lu bytes free\r\n",
                                   taskNames[index], (unsigned long)freeStackBytes);
                    App_Print(message);
                }
            }
        }
    }
}
