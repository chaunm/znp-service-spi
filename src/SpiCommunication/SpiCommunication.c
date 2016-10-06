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
	PSPI pSpi = (PSPI)malloc(sizeof(SPI));
	wiringPiSetupSys();
	pinMode(MRDY_PIN, OUTPUT);
	digitalWrite(MRDY_PIN, HIGH);
	pinMode(SRDY_PIN, INPUT);
	//
	sReadyState = digitalRead(SRDY_PIN);
	spiHold = FALSE;
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

VOID SpiProcessIncomingData(PSPI pSpi)
{
	BYTE receiveByte;
	while(1)
	{
		if ((sReadyState == 1) && (digitalRead(SRDY_PIN) == LOW) && (spiHold == FALSE))
		{
			spiHold = TRUE;
			sReadyState = 0;
			// receive AREQ - set MRDY low then wait for SRDY low
			digitalWrite(MRDY_PIN, LOW);
			while (digitalRead(SRDY_PIN) == HIGH);
			// ZNP has AREQ data - send poll command to receive data
			wiringPiSPIDataRW(0, spiPollCommand, 3);
			// wait for SRDY high
			while (digitalRead(SRDY_PIN) == LOW);
			sReadyState = 1;
			usleep(20);
			// start receive data
			g_pReceivePackage[0] = 0xFE;
			g_nPackageIndex = 1;
			while(g_nPackageIndex != 0)
			{
				receiveByte = 0;
				wiringPiSPIDataRW(0, &receiveByte, 1);
				SpiHandleIncomingByte(pSpi, receiveByte);
			}
			// reading finish - set MRDY high
			digitalWrite(MRDY_PIN, HIGH);
			spiHold = FALSE;
		}
		if ((sReadyState == 0) && (digitalRead(SRDY_PIN) == HIGH))
			sReadyState = 1;
		usleep(1000);
	}
}

void SpiOutputDataProcess(PSPI pSpi)
{
	QUEUECONTENT stOutputContent;
	BYTE nIndex;
	BYTE dataType;
	BYTE receiveByte;
	while(1)
	{
		if (QueueGetState(pSpi->pOutputQueue) == QUEUE_ACTIVE)
		{
			stOutputContent = QueueGetContent(pSpi->pOutputQueue);
			if (stOutputContent.nSize > 0)
			{
				//print data for debugging purpose
				printf("<< ");
				for (nIndex = 0; nIndex < stOutputContent.nSize; nIndex++)
					printf("0x%02X ", stOutputContent.pData[nIndex]);
				printf("\n");
				dataType = (stOutputContent.pData[2] & 0xE0) >> 5;
				// set MRDY low for starting transfer
				while(digitalRead(SRDY_PIN) == LOW);
				sReadyState = 1;
				spiHold = TRUE;
				digitalWrite(MRDY_PIN, LOW);
				while (digitalRead(SRDY_PIN) == HIGH);
				sReadyState = 0;
				// write request
				wiringPiSPIDataRW(0, (void*)(stOutputContent.pData + 1), stOutputContent.nSize - 2);
				while (digitalRead(SRDY_PIN) == LOW);
				sReadyState = 1;
				// if a SREQ then read for SRSP

				if (dataType == 1)
				{
					usleep(20);
					printf("SRSP\n");
					g_pReceivePackage[0] = 0xFE;
					g_nPackageIndex = 1;
					while(g_nPackageIndex != 0)
					{
						receiveByte = 0;
						wiringPiSPIDataRW(0, &receiveByte, 1);
						SpiHandleIncomingByte(pSpi, receiveByte);
					}
				}

				digitalWrite(MRDY_PIN, HIGH);
				spiHold = FALSE;
				QueueFinishProcBuffer(pSpi->pOutputQueue);
			}
		}
		usleep(1000);
	}
}

void SpiInOut(PSPI pSpi)
{
	while (1)
	{
		SpiProcessIncomingData(pSpi);
		SpiOutputDataProcess(pSpi);
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
