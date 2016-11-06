/*
 * serialcommunication.c

 *
 *  Created on: Jan 14, 2016
 *      Author: ChauNM
 */
#include <stdio.h>
#include "SpiCommunication.h"
#include "queue.h"
#include "znp.h"
#include "wiringPi.h"
#include "wiringPiSPI.h"
#include "../fluent-logger/fluent-logger.h"

#define SRDY_PIN	6
#define MRDY_PIN 	5
#define RESET_PIN	26

static BYTE 	g_pReceivePackage[MAX_SERIAL_PACKAGE_SIZE];
static WORD 	g_nPackageIndex;
static BOOL 	sReadyState;
static BOOL 	spiHold;
static unsigned char 	spiPollCommand[3] = {0x00, 0x00, 0x00};

PSPI SpiInit()
{
	char command[255];
	sprintf(command, "gpio export %d in", SRDY_PIN);
	system(command);
	sprintf(command, "gpio export %d out", MRDY_PIN);
	system(command);
	sprintf(command, "gpio export %d out", RESET_PIN);
	system(command);
	PSPI pSpi = (PSPI)malloc(sizeof(SPI));
	wiringPiSetupSys();
	pinMode(RESET_PIN, OUTPUT);
	pinMode(SRDY_PIN, INPUT);
	pinMode(MRDY_PIN, OUTPUT);
	digitalWrite(MRDY_PIN, HIGH);
	digitalWrite(RESET_PIN, LOW);
	sleep(1);
	digitalWrite(RESET_PIN, HIGH);
	sleep(1);
	if (wiringPiSPISetup(0, 1000000) == -1)
	{
		printf("SPI failed\n");
		free(pSpi);
		return NULL;
	}
	else
		printf("SPI loaded\n");
	pSpi->pInputQueue = QueueCreate(SERIAL_QUEUE_SIZE, MAX_SERIAL_PACKAGE_SIZE);
	pSpi->pOutputQueue = QueueCreate(SERIAL_QUEUE_SIZE, MAX_SERIAL_PACKAGE_SIZE);
	//while (digitalRead(SRDY_PIN) == LOW);
	//sReadyState = digitalRead(SRDY_PIN);
	sReadyState = 1;
	spiHold = FALSE;
	return pSpi;
}

static VOID SpiHandleIncomingByte(PSPI pSpi, BYTE byData)
{
	BYTE nIndex;
	g_pReceivePackage[g_nPackageIndex] = byData;
	if (g_nPackageIndex == (g_pReceivePackage[1] + 3))
	{
		BYTE byCRC = 0;
		unsigned int nContentLength = g_pReceivePackage[1] + 3;
		for (nIndex = 1; nIndex <= nContentLength; nIndex++)
		{
			byCRC ^= g_pReceivePackage[nIndex];
		}
		g_pReceivePackage[g_nPackageIndex + 1] = byCRC;
		QueuePush((void *)g_pReceivePackage, g_nPackageIndex + 2, pSpi->pInputQueue);
		g_nPackageIndex = 0;
	}
	else
	{
		g_nPackageIndex++;
	}
}

static VOID SpiProcessIncomingData(PSPI pSpi)
{
	BYTE receiveByte;

	while (spiHold == TRUE);
	while ((sReadyState == 1) && (digitalRead(SRDY_PIN) == LOW))
	{
		spiHold = TRUE;
		sReadyState = 0;
		// receive AREQ - set MRDY low then wait for SRDY low
		digitalWrite(MRDY_PIN, LOW);
		// ZNP has AREQ data - send poll command to receive data
		wiringPiSPIDataRW(0, spiPollCommand, 3);
		memset(spiPollCommand, 0, 3);
		// wait for SRDY high
		while (digitalRead(SRDY_PIN) == LOW);
		sReadyState = 1;
		usleep(10);
		// start receive data
		g_pReceivePackage[0] = 0xFE;
		g_nPackageIndex = 1;
		while(g_nPackageIndex != 0)
		{
			receiveByte = 0;
			wiringPiSPIDataRW(0, &receiveByte, 1);
			SpiHandleIncomingByte(pSpi, receiveByte);
		}
		digitalWrite(MRDY_PIN, HIGH);
		spiHold = FALSE;
	}
}

static void SpiOutputDataProcess(PSPI pSpi)
{
	QUEUECONTENT stOutputContent;
	BYTE nIndex;
	BYTE dataType;
	BYTE receiveByte;
	if (QueueGetState(pSpi->pOutputQueue) == QUEUE_ACTIVE)
	{
		stOutputContent = QueueGetContent(pSpi->pOutputQueue);
		if (stOutputContent.nSize > 0)
		{
			while(spiHold == TRUE);
			//print data for debugging purpose
			printf("<< ");
			for (nIndex = 0; nIndex < stOutputContent.nSize; nIndex++)
				printf("0x%02X ", stOutputContent.pData[nIndex]);
			printf("\n");
			dataType = (stOutputContent.pData[2] & 0xE0) >> 5;
			// set MRDY low for starting transfer
			sReadyState = 1;
			spiHold = TRUE;
			digitalWrite(MRDY_PIN, LOW);
			while (digitalRead(SRDY_PIN) == HIGH);
			//usleep(100);
			sReadyState = 0;
			// write request
			wiringPiSPIDataRW(0, (void*)(stOutputContent.pData + 1), stOutputContent.nSize - 2);
			memset(stOutputContent.pData, 0, MAX_SERIAL_PACKAGE_SIZE);
			while (digitalRead(SRDY_PIN) == LOW);
			sReadyState = 1;
			usleep(100);
			// if a SREQ then read for SRSP
			if (dataType == 1)
			{
				g_pReceivePackage[0] = 0xFE;
				g_nPackageIndex = 1;
				while(g_nPackageIndex != 0)
				{
					receiveByte = 0;
					wiringPiSPIDataRW(0, &receiveByte, 1);
					SpiHandleIncomingByte(pSpi, receiveByte);
				}
			}
			//usleep(1000);
			digitalWrite(MRDY_PIN, HIGH);
			//while(digitalRead(SRDY_PIN) == LOW);
			spiHold = FALSE;
			QueueFinishProcBuffer(pSpi->pOutputQueue);
		}
	}
}

void SpiInOut(PSPI pSpi)
{
	while (1)
	{
		SpiProcessIncomingData(pSpi);
		SpiOutputDataProcess(pSpi);
		usleep(1000);
	}

}
void SpiInputDataProcess(PSPI pSpi)
{
	QUEUECONTENT stInputContent;
	BYTE nIndex;
	while(1)
	{
		if (QueueGetState(pSpi->pInputQueue) == QUEUE_ACTIVE)
		{
			stInputContent = QueueGetContent(pSpi->pInputQueue);
			if (stInputContent.nSize > 0)
			{
				// print data for debugging purpose
				printf(">> ");
				for (nIndex = 0; nIndex < stInputContent.nSize; nIndex++)
				{
					printf("0x%02X ", g_pReceivePackage[nIndex]);
				}
				printf("\n");
				// put handler function here
				ZnpHandleCommand(stInputContent.pData, stInputContent.nSize);
				QueueFinishProcBuffer(pSpi->pInputQueue);
			}
		}
		usleep(1000);
	}
}

BYTE SpiOutput(PSPI pSpi, PBYTE pData, BYTE nSize)
{
	return QueuePush(pData, nSize, pSpi->pOutputQueue);
}
