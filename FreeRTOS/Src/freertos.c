/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * File Name          : freertos.c
  * Description        : FreeRTOS object creation and task entry dispatch.
  ******************************************************************************
  */
/* USER CODE END Header */

/* Includes ------------------------------------------------------------------*/
#include "FreeRTOS.h"
#include "task.h"
#include "main.h"
#include "cmsis_os.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include "app_types.h"
#include "app_tasks.h"
#include "mqtt_at.h"
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
/* USER CODE BEGIN Variables */

/* USER CODE END Variables */
/* Definitions for ESP8266Task */
osThreadId_t ESP8266TaskHandle;
const osThreadAttr_t ESP8266Task_attributes = {
  .name = "ESP8266Task",
  .stack_size = 512 * 4,
  .priority = (osPriority_t) osPriorityAboveNormal,
};
/* Definitions for SensorTask */
osThreadId_t SensorTaskHandle;
const osThreadAttr_t SensorTask_attributes = {
  .name = "SensorTask",
  .stack_size = 256 * 4,
  .priority = (osPriority_t) osPriorityNormal,
};
/* Definitions for RfidTask */
osThreadId_t RfidTaskHandle;
const osThreadAttr_t RfidTask_attributes = {
  .name = "RfidTask",
  .stack_size = 256 * 4,
  .priority = (osPriority_t) osPriorityNormal,
};
/* Definitions for TraceTask */
osThreadId_t TraceTaskHandle;
const osThreadAttr_t TraceTask_attributes = {
  .name = "TraceTask",
  .stack_size = 384 * 4,
  .priority = (osPriority_t) osPriorityAboveNormal,
};
/* Definitions for StorageTask */
osThreadId_t StorageTaskHandle;
const osThreadAttr_t StorageTask_attributes = {
  .name = "StorageTask",
  .stack_size = 384 * 4,
  .priority = (osPriority_t) osPriorityNormal,
};
/* Definitions for HealthTask */
osThreadId_t HealthTaskHandle;
const osThreadAttr_t HealthTask_attributes = {
  .name = "HealthTask",
  .stack_size = 256 * 4,
  .priority = (osPriority_t) osPriorityNormal,
};
/* Definitions for RfidEventQueue */
osMessageQueueId_t RfidEventQueueHandle;
const osMessageQueueAttr_t RfidEventQueue_attributes = {
  .name = "RfidEventQueue"
};
/* Definitions for SensorDataQueue */
osMessageQueueId_t SensorDataQueueHandle;
const osMessageQueueAttr_t SensorDataQueue_attributes = {
  .name = "SensorDataQueue"
};
/* Definitions for StorageQueue */
osMessageQueueId_t StorageQueueHandle;
const osMessageQueueAttr_t StorageQueue_attributes = {
  .name = "StorageQueue"
};
/* Definitions for MqttPublishQueue */
osMessageQueueId_t MqttPublishQueueHandle;
const osMessageQueueAttr_t MqttPublishQueue_attributes = {
  .name = "MqttPublishQueue"
};
/* Definitions for DeviceCommandQueue */
osMessageQueueId_t DeviceCommandQueueHandle;
const osMessageQueueAttr_t DeviceCommandQueue_attributes = {
  .name = "DeviceCommandQueue"
};
/* Definitions for StorageCommandQueue */
osMessageQueueId_t StorageCommandQueueHandle;
const osMessageQueueAttr_t StorageCommandQueue_attributes = {
  .name = "StorageCommandQueue"
};
/* Definitions for SpiMutex */
osMutexId_t SpiMutexHandle;
const osMutexAttr_t SpiMutex_attributes = {
  .name = "SpiMutex"
};
/* Definitions for SensorReadySem */
osSemaphoreId_t SensorReadySemHandle;
const osSemaphoreAttr_t SensorReadySem_attributes = {
  .name = "SensorReadySem"
};

/* Private function prototypes -----------------------------------------------*/
/* USER CODE BEGIN FunctionPrototypes */

/* USER CODE END FunctionPrototypes */

void StartESP8266Task(void *argument);
void StartSensorTask(void *argument);
void StartRfidTask(void *argument);
void StartTraceTask(void *argument);
void StartStorageTask(void *argument);
void StartHealthTask(void *argument);

void MX_FREERTOS_Init(void); /* (MISRA C 2004 rule 8.1) */

/**
  * @brief  FreeRTOS initialization
  * @param  None
  * @retval None
  */
void MX_FREERTOS_Init(void) {
  /* USER CODE BEGIN Init */

  /* USER CODE END Init */
  /* Create the mutex(es) */
  /* creation of SpiMutex */
  SpiMutexHandle = osMutexNew(&SpiMutex_attributes);

  /* USER CODE BEGIN RTOS_MUTEX */
  /* add mutexes, ... */
  /* USER CODE END RTOS_MUTEX */

  /* Create the semaphores(s) */
  /* creation of SensorReadySem */
  SensorReadySemHandle = osSemaphoreNew(1, 0, &SensorReadySem_attributes);

  /* USER CODE BEGIN RTOS_SEMAPHORES */
  /* add semaphores, ... */
  /* USER CODE END RTOS_SEMAPHORES */

  /* USER CODE BEGIN RTOS_TIMERS */
  /* start timers, add new ones, ... */
  /* USER CODE END RTOS_TIMERS */

  /* Create the queue(s) */
  /* creation of RfidEventQueue */
  RfidEventQueueHandle = osMessageQueueNew (8, sizeof(RfidEvent_t), &RfidEventQueue_attributes);

  /* creation of SensorDataQueue */
  SensorDataQueueHandle = osMessageQueueNew (8, sizeof(SensorData_t), &SensorDataQueue_attributes);

  /* creation of StorageQueue */
  StorageQueueHandle = osMessageQueueNew (10, sizeof(StorageRecord_t), &StorageQueue_attributes);

  /* creation of MqttPublishQueue */
  MqttPublishQueueHandle = osMessageQueueNew (8, sizeof(StorageRecord_t), &MqttPublishQueue_attributes);

  /* creation of DeviceCommandQueue */
  DeviceCommandQueueHandle = osMessageQueueNew (4, sizeof(DeviceCommand_t), &DeviceCommandQueue_attributes);
  /* creation of StorageCommandQueue */
  StorageCommandQueueHandle = osMessageQueueNew (8, sizeof(StorageCommand_t), &StorageCommandQueue_attributes);

  /* USER CODE BEGIN RTOS_QUEUES */
  /* add queues, ... */
  /* USER CODE END RTOS_QUEUES */

  /* Create the thread(s) */
  /* creation of ESP8266Task */
  ESP8266TaskHandle = osThreadNew(StartESP8266Task, NULL, &ESP8266Task_attributes);

  /* creation of SensorTask */
  SensorTaskHandle = osThreadNew(StartSensorTask, NULL, &SensorTask_attributes);

  /* creation of RfidTask */
  RfidTaskHandle = osThreadNew(StartRfidTask, NULL, &RfidTask_attributes);

  /* creation of TraceTask */
  TraceTaskHandle = osThreadNew(StartTraceTask, NULL, &TraceTask_attributes);

  /* creation of StorageTask */
  StorageTaskHandle = osThreadNew(StartStorageTask, NULL, &StorageTask_attributes);

  /* creation of HealthTask */
  HealthTaskHandle = osThreadNew(StartHealthTask, NULL, &HealthTask_attributes);

  /* USER CODE BEGIN RTOS_THREADS */
  /* add threads, ... */
  /* USER CODE END RTOS_THREADS */

  /* USER CODE BEGIN RTOS_EVENTS */
  /* add events, ... */
  /* USER CODE END RTOS_EVENTS */

}

/* USER CODE BEGIN Header_StartESP8266Task */
/**
* @brief Function implementing the ESP8266Task thread.
* @param argument: Not used
* @retval None
*/
/* USER CODE END Header_StartESP8266Task */
void StartESP8266Task(void *argument)
{
  /* USER CODE BEGIN StartESP8266Task */
  MqttAt_RunTask(argument);
  /* USER CODE END StartESP8266Task */
}

/* USER CODE BEGIN Header_StartSensorTask */
/**
* @brief Function implementing the SensorTask thread.
* @param argument: Not used
* @retval None
*/
/* USER CODE END Header_StartSensorTask */
void StartSensorTask(void *argument)
{
  /* USER CODE BEGIN StartSensorTask */
  AppTask_RunSensor(argument);
  /* USER CODE END StartSensorTask */
}

/* USER CODE BEGIN Header_StartRfidTask */
/**
* @brief Function implementing the RfidTask thread.
* @param argument: Not used
* @retval None
*/
/* USER CODE END Header_StartRfidTask */
void StartRfidTask(void *argument)
{
  /* USER CODE BEGIN StartRfidTask */
  AppTask_RunRfid(argument);
  /* USER CODE END StartRfidTask */
}

/* USER CODE BEGIN Header_StartTraceTask */
/**
* @brief Function implementing the TraceTask thread.
* @param argument: Not used
* @retval None
*/
/* USER CODE END Header_StartTraceTask */
void StartTraceTask(void *argument)
{
  /* USER CODE BEGIN StartTraceTask */
  AppTask_RunTrace(argument);
  /* USER CODE END StartTraceTask */
}

/* USER CODE BEGIN Header_StartStorageTask */
/**
* @brief Function implementing the StorageTask thread.
* @param argument: Not used
* @retval None
*/
/* USER CODE END Header_StartStorageTask */
void StartStorageTask(void *argument)
{
  /* USER CODE BEGIN StartStorageTask */
  AppTask_RunStorage(argument);
  /* USER CODE END StartStorageTask */
}

/* USER CODE BEGIN Header_StartHealthTask */
/**
* @brief Function implementing the HealthTask thread.
* @param argument: Not used
* @retval None
*/
/* USER CODE END Header_StartHealthTask */
void StartHealthTask(void *argument)
{
  /* USER CODE BEGIN StartHealthTask */
  AppTask_RunHealth(argument);
  /* USER CODE END StartHealthTask */
}

/* Private application code --------------------------------------------------*/
/* USER CODE BEGIN Application */

/* USER CODE END Application */

