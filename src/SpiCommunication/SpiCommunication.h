/*
 * serialcommunication.h
 *
 *  Created on: Jan 14, 2016
 *      Author: ChauNM
 */

#ifndef SERIALCOMMUNICATION_H_
#define SERIALCOMMUNICATION_H_
#include <termios.h>
#include <stdio.h>
#include <fcntl.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include "typesdef.h"
#include "queue.h"

#define MAX_SERIAL_PACKAGE_SIZE		255
#define SERIAL_QUEUE_SIZE			32

/* Serial management data struct *
#pragma pack(1)
typedef struct stSerial {
	unsigned int uiBaudrate;
	int	tty_fd;
	PQUEUECONTROL pInputQueue;
	PQUEUECONTROL pOutputQueue;
} SERIAL, *PSERIAL;
*/
#pragma pack(1)
typedef struct stSpi {
	PQUEUECONTROL pInputQueue;
	PQUEUECONTROL pOutputQueue;
} SPI, *PSPI;

/* Exported function */
PSPI SpiInit();
//PSERIAL SerialOpen (char* PortName, unsigned int uiBaudrate);
//VOID SerialClose(PSERIAL pSerialPort);
VOID SpiProcessIncomingData(PSPI pSpi);
VOID SpiOutputDataProcess(PSPI pSpi);
void SpiInOut(PSPI pSpi);
VOID SpiInputDataProcess(PSPI pSpi);
BYTE SpiOutput(PSPI pSPI, PBYTE pData, BYTE nSize);
#endif /* SERIALCOMMUNICATION_H_ */
