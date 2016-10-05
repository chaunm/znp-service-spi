/*
 ============================================================================
 Name        : ZigbeeHost.c
 Author      : ChauNM
 Version     :
 Copyright   :
 Description : C Ansi-style
 ============================================================================
 */

#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <getopt.h>
#include "SpiCommunication.h"
#include "znp.h"
#include "ZnpCommandState.h"
#include "zcl.h"
#include "DeviceManager/DevicesManager.h"
#include "log.h"
#include "universal.h"
#include "ZnpActor.h"
#include "fluent-logger/fluent-logger.h"

void PrintHelpMenu() {
	printf("program: ZigbeeHostAMA\n"
			"using ./ZigbeeHostAMA --port [] --id [] --token []\n"
			"--serial: Serial port used to communicate with ZNP device (ex.: ttyUSB0, ttyAMA0..)\n"
			"--id: guid of the znp actor\n"
			"--token: pasword to the broker of the znp actor, this option can be omitted\n"
			"--host: mqtt server address can be ommitted\n"
			"--port: mqtt port - can be omitted\n"
			"--update: time for updating online message to system");
}

int main(int argc, char* argv[])
{

	pthread_t SpiProcessThread;
	pthread_t SpiOutputThread;
	pthread_t SpiHandleThread;
	//pthread_t DemoActorThread;
	PSPI	pSpi;
	BOOL bResult = FALSE;
	BYTE nRetry = 0;
//	/int SerialThreadErr;

	/* get option */

	int opt= 0;
	char *token = NULL;
	char *guid = NULL;
	char *SerialPort = NULL;
	char *mqttHost = NULL;
	WORD mqttPort = 0;
	WORD ttl = 0;

	// specific the expected option
	static struct option long_options[] = {
			{"id",      required_argument, 0, 'i' },
			{"token", 	required_argument, 0, 't' },
			{"serial",    required_argument, 0, 's' },
			{"update", 	required_argument, 0, 'u' },
			{"host", required_argument, 0, 'H'},
			{"port", required_argument, 0, 'p'}
	};
	int long_index;
	/* Process option */
	while ((opt = getopt_long(argc, argv,":hi:t:s:u:H:p:",
			long_options, &long_index )) != -1) {
		switch (opt) {
		case 'h' :
			PrintHelpMenu();
			return EXIT_SUCCESS;
			break;
		case 's' :
			SerialPort = StrDup(optarg);
			break;
		case 'i':
			guid = StrDup(optarg);
			break;
		case 't':
			token = StrDup(optarg);
			break;
		case 'u':
			ttl = atoi(optarg);
			break;
		case 'H':
			mqttHost = StrDup(optarg);
			break;
		case 'p':
			mqttPort = atoi(optarg);
			break;
		case ':':
			if ((optopt == 'i') || optopt == 'p')
			{
				printf("invalid option(s), using -h for help\n");
				return EXIT_FAILURE;
			}
			break;
		default:
			break;
		}
	}
	if ((SerialPort == NULL) || (guid == NULL))
	{
		printf("invalid options, using -h for help\n");
		return EXIT_FAILURE;
	}
	/* All option valid, start program */
	LOGGEROPTION loggerOpt;
	loggerOpt.sender = StrDup(guid);
	loggerOpt.host = StrDup(mqttHost);
	ACTOROPTION option;
	option.guid = guid;
	option.psw = token;
	option.host = mqttHost;
	option.port = mqttPort;

	LogWrite("Zigbee host start. start init ZNP");
	// Init device organization list
	DeviceListInit();
	/* Start Logger */
	FluentLoggerInit(&loggerOpt);
	/* Start Znp actor */
	FLUENT_LOGGER_INFO("znp actor start");
	ZnpActorStart(&option);
	/* open serial port and init queue for serial communication */
	char* PortName = malloc(strlen("/dev/") + strlen(SerialPort) + 1);
	memset(PortName, 0, strlen("/dev/") + strlen(SerialPort) + 1);
	sprintf(PortName, "%s%s", "/dev/", SerialPort);
	while (bResult == FALSE)
	{
		//pSerialPort = SerialOpen(PortName, B115200);
		pSpi = SpiInit();
		if (pSpi == NULL)
		{
			FLUENT_LOGGER_ERROR("Spi open failed")
			printf("Can not open Spi port\n");
			return EXIT_FAILURE;
		}
		free(PortName);
		// Initial Serial port handle process
		pthread_create(&SpiProcessThread, NULL, (void*)&SpiProcessIncomingData, (void*)pSpi);
		pthread_create(&SpiOutputThread, NULL, (void*)&SpiOutputDataProcess, (void*)pSpi);
		//pthread_create(&SpiProcessThread, NULL, (void*)&SpiInOut, (void*)pSpi);
		pthread_create(&SpiHandleThread, NULL, (void*)&SpiInputDataProcess, (void*)pSpi);
		// init znp device
		bResult = ZnpInit(pSpi, ttl);
		if (bResult == FALSE)
		{
			// close serial port to retry
			pthread_cancel(SpiProcessThread);
			pthread_cancel(SpiOutputThread);
			pthread_cancel(SpiHandleThread);
			//SerialClose(pSerialPort);
			nRetry++;
			if (nRetry == 5)
			{
				printf("can not start ZNP after 5 times, exit program\n");
				LogWrite("Can't not start ZNP after 5 times, exit program");
				FLUENT_LOGGER_ERROR("Can't start ZNP");
				ZnpActorPublishZnpStatus("status.offline.znp_start_error");
				sleep(3);
				exit(0);
			}
			printf("ZNP reset fail, retry\n");
			FLUENT_LOGGER_WARN("couldn't start ZNP, retry");
			LogWrite("ZNP reset failed, retry");
		}
		ZnpActorPublishZnpStatus("status.online");
		printf("ZNP start success\n");
		LogWrite("ZNP start success");
		FLUENT_LOGGER_INFO("ZNP Start success");
	}

	while (1)
	{
		DeviceProcessTimeout();
		ZclMsgStatusProcess();
		ZnpCmdStateProcess();
		ZnpStateProcess();
		sleep(1);
	}
	//SerialClose(pSerialPort);
	return EXIT_SUCCESS;
}
