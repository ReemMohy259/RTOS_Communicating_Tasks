#include <stdio.h>
#include <stdlib.h>
#include "diag/trace.h"
#include <string.h>

/* Kernel includes. */
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "timers.h"
#include "semphr.h"

#define CCM_RAM __attribute__((section(".ccmram")))

/*----------------------------------------------------------------------------------------------------------------------------*/
#define SENDER1_PRIORITY         1
#define SENDER2_PRIORITY         1
#define SENDER3_PRIORITY         2
#define RECIEVER_PRIORITY        3
#define MAX_RECEIVED_MESSAGES    1000
#define TASKS_STACK_SIZE         1000
#define QUEUE_LENGTH             3
#define ARRAY_SIZE               20
#define LOWER_ARRAY_SIZE         6
#define UPPER_ARRAY_SIZE         6
#define MAX_NUM_OF_RESET_CALLS   6
#define TRECEIVER                pdMS_TO_TICKS( 100 ) /*Receiver timer period in ticks*/

/*-------------------------------------------(Global structure & variables)---------------------------------------------------*/

/*Global structure that contains all global messages counters used*/
static struct sCounters{

	uint32_t ulBlockedMessage1;
	uint32_t ulSendingMessage1;
	uint32_t ulBlockedMessage2;
	uint32_t ulSendingMessage2;
	uint32_t ulBlockedMessage3;
	uint32_t ulSendingMessage3;
	uint32_t ulReceivedMessage;
	uint32_t ulTotalTsender;
	uint32_t ulTotalTsender1;
	uint32_t ulTotalTsender2;
	uint32_t ulTotalTsender3;
	uint32_t totalRandomValue;

};

static struct sArrays{

	/*Lower and upper bounds array of uniform distribution for a random value*/
	uint32_t ulLowerBounds[LOWER_ARRAY_SIZE] ;
	uint32_t ulUpperBounds[UPPER_ARRAY_SIZE] ;
	uint8_t ucIndex;  /*Index that access the above arrays*/
};

static struct sTimersHandler{

	TimerHandle_t sender1Timer;
	TimerHandle_t sender2Timer;
	TimerHandle_t sender3Timer;
	TimerHandle_t receiverTimer;
	TickType_t Tsender;
}timers;   /*Global structure that contains all global timers variables used*/

static struct sSemaphoresHandle{

	SemaphoreHandle_t recevierSemp;
	SemaphoreHandle_t sender1Semp;
	SemaphoreHandle_t sender2Semp;
	SemaphoreHandle_t sender3Semp;
}semaphore;  /*Global structure that contains all semaphores used*/

struct sCounters counters = {0};

struct sArrays lowerUpperArr = { .ulLowerBounds={50, 80, 110, 140, 170, 200},\
		.ulUpperBounds = {150, 200, 250, 300, 350, 400} , .ucIndex = 0};

QueueHandle_t g_Queue;  /*Global Queue used for task communication*/

/*------------------------------------------------(Functions Deceleration)------------------------------------------------------*/

/*Generate a random value in ticks within a range of lower and upper bounds*/
TickType_t TicksRandomValue(uint32_t lowerBound, uint32_t upperBound)
{
	uint32_t randomValue = rand();
	randomValue = randomValue % (upperBound - lowerBound + 1);
	randomValue += lowerBound;  /*Make the random value in the required range*/

	counters.totalRandomValue += 1;  /*To count the total number of random value generated in each iteration*/

	/*Return the random value in ticks*/
	return pdMS_TO_TICKS(randomValue);
}

/*The reset function that is called by receiver timer callback function*/


/*Reset function that is called when the program just starts its called by main()*/
void mainReset(void)
{
	/*Reset the whole structure that contain the number of sending,blocked and received messages*/
	memset(&counters, 0, sizeof(counters));

	xQueueReset(g_Queue);   /*Clears the queue*/

	lowerUpperArr.ucIndex = 0;
}

void Reset(void)
{
	static int numOfResetCalls = 0;
	uint32_t totalNumOfSentMess = counters.ulSendingMessage1 + counters.ulSendingMessage2+ counters.ulSendingMessage3;
	uint32_t totalNumOfBlockedMess = counters.ulBlockedMessage1 + counters.ulBlockedMessage2+ counters.ulBlockedMessage3;

	/*Print the total number of successfully sent messages and the total number of blocked messages*/
	printf("@ ITERATION NUMBER %d  \n", (numOfResetCalls + 1));
	printf("Total number of successfully sent messages = %lu \n", totalNumOfSentMess);
	printf("Total number of blocked messages = %lu \n", totalNumOfBlockedMess);
	printf("Total number of received messages = %lu \n", counters.ulReceivedMessage);

	/*Print the total value of generated random sender period and its number to calculate the average*/
	printf("Total value of sender timer periods (Tsender) generated = %lu \n",counters.ulTotalTsender);
	printf("Total number of generated random value = %lu \n",counters.totalRandomValue);
	printf("Average sender timer period = %lu \n",(counters.ulTotalTsender/counters.totalRandomValue));

	/*Print the statistics per sender tasks.*/
	printf("# The statistics per sender tasks are: \n");
	printf("Sender1 => Number of sending messages = %lu  &  Number of blocked messages = %lu  &  Total value of (Tsender1) generated = %lu \n",\
			counters.ulSendingMessage1, counters.ulBlockedMessage1, counters.ulTotalTsender1);
	printf("Sender2 => Number of sending messages = %lu  &  Number of blocked messages = %lu  &  Total value of (Tsender2) generated = %lu \n",\
			counters.ulSendingMessage2, counters.ulBlockedMessage2, counters.ulTotalTsender2);
	printf("Sender3 => Number of sending messages = %lu  &  Number of blocked messages = %lu  &  Total value of (Tsender3) generated = %lu \n",\
			counters.ulSendingMessage3, counters.ulBlockedMessage3, counters.ulTotalTsender3);

	/*Reset the whole structure that contain the number of sending,blocked and received messages*/
	memset(&counters, 0, sizeof(counters));

	xQueueReset(g_Queue); /*Clears the queue*/

	lowerUpperArr.ucIndex += 1; /*switch to the next bounds to generate new random value*/

	numOfResetCalls ++;

	/*Check if all values in the lower and upper bound array are used*/
	if(numOfResetCalls == MAX_NUM_OF_RESET_CALLS)
	{
		/*Destroy the timers*/
		xTimerDelete(timers.sender1Timer , 0);
		xTimerDelete(timers.sender2Timer , 0);
		xTimerDelete(timers.sender3Timer , 0);
		xTimerDelete(timers.receiverTimer , 0);

		printf("\t\t\t\t=> These Results for a Queue of Length = %d <= \n", QUEUE_LENGTH);

		printf("******************************************************** GAME OVER ******************************************************* \n");

		exit(0); /*To terminate the program*/
	}
}

/*----------------------------------------------------(Tasks Deceleration)----------------------------------------------------*/

void Sender1Task( void *pvParameters )
{
	char cMessage[ARRAY_SIZE];  /*Array to store the string "Time is XYZ" that will send via a queue*/
	BaseType_t xStatus;
	TickType_t currentTimeInTicks;

	while(1)
	{
		currentTimeInTicks = xTaskGetTickCount();  /*To return the current time in ticks*/
		snprintf(cMessage, ARRAY_SIZE, "Time is %lu", currentTimeInTicks);  /*To concatenate the string in a variable number*/

		if(xSemaphoreTake(semaphore.sender1Semp, portMAX_DELAY) == pdTRUE)  /*Sender task will wake up*/
		{
			xStatus = xQueueSend(g_Queue, cMessage, 0);
			if(xStatus == pdTRUE)  /*Sending operation succeeded*/
			{
				counters.ulSendingMessage1 += 1;

				// printf("Sending message from sender1: %s \n",cMessage);
			}
			else
			{
				counters.ulBlockedMessage1 += 1;
			}
		}
	}
}

void Sender2Task( void *pvParameters )
{
	char cMessage[ARRAY_SIZE];  /*Array to store the string "Time is XYZ" that will send via a queue*/
	BaseType_t xStatus;
	TickType_t currentTimeInTicks;

	while(1)
	{
		currentTimeInTicks = xTaskGetTickCount();  /*To return the current time in ticks*/
		snprintf(cMessage, ARRAY_SIZE, "Time is %lu", currentTimeInTicks);  /*To concatenate the string in a variable number*/

		if(xSemaphoreTake(semaphore.sender2Semp, portMAX_DELAY) == pdTRUE)  /*Sender task will wake up*/
		{
			xStatus = xQueueSend(g_Queue, cMessage, 0);
			if(xStatus == pdTRUE)  /*Sending operation succeeded*/
			{
				counters.ulSendingMessage2 += 1;

				// printf("Sending message from sender2: %s \n",cMessage);
			}
			else
			{
				counters.ulBlockedMessage2 += 1;
			}
		}
	}
}

void Sender3Task( void *pvParameters )
{
	char cMessage[ARRAY_SIZE];  /*Array to store the string "Time is XYZ" that will send via a queue*/
	BaseType_t xStatus;
	TickType_t currentTimeInTicks;

	while(1)
	{
		currentTimeInTicks = xTaskGetTickCount();  /*To return the current time in ticks*/
		snprintf(cMessage, ARRAY_SIZE, "Time is %lu", currentTimeInTicks);  /*To concatenate the string in a variable number*/

		if(xSemaphoreTake(semaphore.sender3Semp, portMAX_DELAY) == pdTRUE)  /*Sender task will wake up*/
		{
			xStatus = xQueueSend(g_Queue, cMessage, 0);
			if(xStatus == pdTRUE)  /*Sending operation succeeded*/
			{
				counters.ulSendingMessage3 += 1;

				// printf("Sending message from sender3: %s \n",cMessage);
			}
			else
			{
				counters.ulBlockedMessage3 += 1;
			}
		}
	}
}

static void ReceiverTask( void *pvParameters )
{
	char cReceivedMessage[ARRAY_SIZE];  /*array that we will store the received the message from queue*/
	BaseType_t xStatus;

	while(1)
	{
		if(xSemaphoreTake(semaphore.recevierSemp, portMAX_DELAY) == pdTRUE)  /*Receiver task will wake up*/
		{
			xStatus = xQueueReceive(g_Queue, cReceivedMessage, 0);

			if(xStatus == pdTRUE)  /*Receiving operation succeeded*/
			{
				counters.ulReceivedMessage += 1;
			}
		}
	}
}

/*-----------------------------------(Callback Functions Deceleration)-------------------------------------------*/

static void sender1TimerCallback( TimerHandle_t xTimer )
{
	xSemaphoreGive(semaphore.sender1Semp);  /*Release the semaphore that sender1 blocked on it*/

	/*Change the period of the sender1 timer to another random value*/
	timers.Tsender = TicksRandomValue(lowerUpperArr.ulLowerBounds[lowerUpperArr.ucIndex],\
			lowerUpperArr.ulUpperBounds[lowerUpperArr.ucIndex]);

	counters.ulTotalTsender += timers.Tsender;  /*Add the new period in the total counter*/
	counters.ulTotalTsender1 += timers.Tsender; /*Add the new period in the counter of the first sender*/

	xTimerChangePeriod( timers.sender1Timer, timers.Tsender, 0);
}

static void sender2TimerCallback( TimerHandle_t xTimer )
{
	xSemaphoreGive(semaphore.sender2Semp);  /*Release the semaphore that sender2 blocked on it*/

	/*Change the period of the sender2 timer to another random value*/
	timers.Tsender = TicksRandomValue(lowerUpperArr.ulLowerBounds[lowerUpperArr.ucIndex], \
			lowerUpperArr.ulUpperBounds[lowerUpperArr.ucIndex] );

	counters.ulTotalTsender += timers.Tsender;  /*Add the new period in the total counter*/
	counters.ulTotalTsender2 += timers.Tsender;  /*Add the new period in the counter of the second sender*/

	xTimerChangePeriod( timers.sender2Timer, timers.Tsender, 0);
}

static void sender3TimerCallback( TimerHandle_t xTimer )
{
	xSemaphoreGive(semaphore.sender3Semp);  /*Release the semaphore that sender3 blocked on it*/

	/*Change the period of the sender3 timer to another random value*/
	timers.Tsender = TicksRandomValue(lowerUpperArr.ulLowerBounds[lowerUpperArr.ucIndex], \
			lowerUpperArr.ulUpperBounds[lowerUpperArr.ucIndex] );

	counters.ulTotalTsender += timers.Tsender;  /*Add the new period in the total counter*/
	counters.ulTotalTsender3 += timers.Tsender;  /*Add the new period in the counter of the third sender*/

	xTimerChangePeriod( timers.sender3Timer, timers.Tsender, 0);
}

static void receiverTimerCallback( TimerHandle_t xTimer )
{
	xSemaphoreGive(semaphore.recevierSemp);  /*Release the semaphore that receiver task blocked on it*/

	if(counters.ulReceivedMessage == MAX_RECEIVED_MESSAGES )
	{
		Reset();
	}
}

/*--------------------------------------------(main function)-----------------------------------------------------*/
int main(void)
{
	volatile BaseType_t xReceiverTimerStarted;
	volatile BaseType_t xSender1TimerStarted;
	volatile BaseType_t xSender2TimerStarted;
	volatile BaseType_t xSender3TimerStarted;

	g_Queue = xQueueCreate(QUEUE_LENGTH , sizeof(char*)); /*Create a queue that holds a certain numbers of strings*/

	mainReset(); /*Call the main reset function first to initialize the system*/

	timers.Tsender = TicksRandomValue(lowerUpperArr.ulLowerBounds[lowerUpperArr.ucIndex], \
			lowerUpperArr.ulUpperBounds[lowerUpperArr.ucIndex] );  /*Generate a random value for sender timer period */

	counters.ulTotalTsender = timers.Tsender;
	counters.ulTotalTsender1 = timers.Tsender;
	counters.ulTotalTsender2 = timers.Tsender;
	counters.ulTotalTsender3 = timers.Tsender;

	/*Create all semaphores used*/
	semaphore.recevierSemp = xSemaphoreCreateBinary();
	semaphore.sender1Semp = xSemaphoreCreateBinary();
	semaphore.sender2Semp = xSemaphoreCreateBinary();
	semaphore.sender3Semp = xSemaphoreCreateBinary();

	/*Create all tasks with NULL parameter*/
	xTaskCreate( Sender1Task, "Sender1", TASKS_STACK_SIZE, NULL, SENDER1_PRIORITY, NULL );
	xTaskCreate( Sender2Task, "Sender2", TASKS_STACK_SIZE, NULL, SENDER2_PRIORITY, NULL );
	xTaskCreate( Sender3Task, "Sender3", TASKS_STACK_SIZE, NULL, SENDER3_PRIORITY, NULL );
	xTaskCreate( ReceiverTask,"Receiver",TASKS_STACK_SIZE, NULL, RECIEVER_PRIORITY,NULL );

	/*Create all auto reload timers used*/
	timers.receiverTimer = xTimerCreate("ReceiverAutoTimer", TRECEIVER, pdTRUE, 0, receiverTimerCallback );
	timers.sender1Timer = xTimerCreate("Sender1AutoTimer", timers.Tsender, pdTRUE, 0, sender1TimerCallback );
	timers.sender2Timer = xTimerCreate("Sender2AutoTimer", timers.Tsender, pdTRUE, 0, sender2TimerCallback );
	timers.sender3Timer = xTimerCreate("Sender3AutoTimer", timers.Tsender, pdTRUE, 0, sender3TimerCallback );

	if( timers.receiverTimer != NULL && timers.sender1Timer!= NULL && \
			timers.sender2Timer!= NULL && timers.sender3Timer!= NULL )   /*Check if all timers created successfully*/
	{
		/*Start all timers now*/
		xReceiverTimerStarted = xTimerStart(timers.receiverTimer, 0);
		xSender1TimerStarted = xTimerStart(timers.sender1Timer, 0);
		xSender2TimerStarted = xTimerStart(timers.sender2Timer, 0);
		xSender3TimerStarted = xTimerStart(timers.sender3Timer, 0);
	}

	if( xReceiverTimerStarted == pdPASS && xSender1TimerStarted == pdPASS && \
			xSender2TimerStarted == pdPASS && xSender3TimerStarted == pdPASS  )   /*Check if all timers started successfully*/
	{
		vTaskStartScheduler();  /*Start executing the program*/
	}

	/*This line should not be reached*/
	for(;;);

	return 0;

}


/*-----------------------------------------------------------------------------------------------------------------*/

#pragma GCC diagnostic pop

void vApplicationMallocFailedHook( void )
{
	/* Called if a call to pvPortMalloc() fails because there is insufficient
	free memory available in the FreeRTOS heap.  pvPortMalloc() is called
	internally by FreeRTOS API functions that create tasks, queues, software
	timers, and semaphores.  The size of the FreeRTOS heap is set by the
	configTOTAL_HEAP_SIZE configuration constant in FreeRTOSConfig.h. */
	for( ;; );
}
/*-----------------------------------------------------------*/

void vApplicationStackOverflowHook( TaskHandle_t pxTask, char *pcTaskName )
{
	( void ) pcTaskName;
	( void ) pxTask;

	/* Run time stack overflow checking is performed if
	configconfigCHECK_FOR_STACK_OVERFLOW is defined to 1 or 2.  This hook
	function is called if a stack overflow is detected. */
	for( ;; );
}
/*-----------------------------------------------------------*/

void vApplicationIdleHook( void )
{
	volatile size_t xFreeStackSpace;

	/* This function is called on each cycle of the idle task.  In this case it
	does nothing useful, other than report the amout of FreeRTOS heap that
	remains unallocated. */
	xFreeStackSpace = xPortGetFreeHeapSize();

	if( xFreeStackSpace > 100 )
	{
		/* By now, the kernel has allocated everything it is going to, so
		if there is a lot of heap remaining unallocated then
		the value of configTOTAL_HEAP_SIZE in FreeRTOSConfig.h can be
		reduced accordingly. */
	}
}

void vApplicationTickHook(void) {
}

StaticTask_t xIdleTaskTCB CCM_RAM;
StackType_t uxIdleTaskStack[configMINIMAL_STACK_SIZE] CCM_RAM;

void vApplicationGetIdleTaskMemory(StaticTask_t **ppxIdleTaskTCBBuffer, StackType_t **ppxIdleTaskStackBuffer, uint32_t *pulIdleTaskStackSize) {
	/* Pass out a pointer to the StaticTask_t structure in which the Idle task's
  state will be stored. */
	*ppxIdleTaskTCBBuffer = &xIdleTaskTCB;

	/* Pass out the array that will be used as the Idle task's stack. */
	*ppxIdleTaskStackBuffer = uxIdleTaskStack;

	/* Pass out the size of the array pointed to by *ppxIdleTaskStackBuffer.
  Note that, as the array is necessarily of type StackType_t,
  configMINIMAL_STACK_SIZE is specified in words, not bytes. */
	*pulIdleTaskStackSize = configMINIMAL_STACK_SIZE;
}

static StaticTask_t xTimerTaskTCB CCM_RAM;
static StackType_t uxTimerTaskStack[configTIMER_TASK_STACK_DEPTH] CCM_RAM;

/* configUSE_STATIC_ALLOCATION and configUSE_TIMERS are both set to 1, so the
application must provide an implementation of vApplicationGetTimerTaskMemory()
to provide the memory that is used by the Timer service task. */
void vApplicationGetTimerTaskMemory(StaticTask_t **ppxTimerTaskTCBBuffer, StackType_t **ppxTimerTaskStackBuffer, uint32_t *pulTimerTaskStackSize) {
	*ppxTimerTaskTCBBuffer = &xTimerTaskTCB;
	*ppxTimerTaskStackBuffer = uxTimerTaskStack;
	*pulTimerTaskStackSize = configTIMER_TASK_STACK_DEPTH;
}
