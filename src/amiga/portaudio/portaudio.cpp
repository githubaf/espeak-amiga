/***************************************************************************
 *   Copyright (C) 2017 by Alexander Fritsch                               *
 *   email: selco@t-online.de                                              *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 3 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 *   This program is distributed in the hope that it will be useful,       *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 *   GNU General Public License for more details.                          *
 *                                                                         *
 *   You should have received a copy of the GNU General Public License     *
 *   along with this program; if not, write see:                           *
 *               <http://www.gnu.org/licenses/>.                           *
 ***************************************************************************/


#include "../portaudio18.h"
#include <stdio.h>
#include <stdlib.h>

/* AF, Gwd, 5.May 2017 */

#include <exec/types.h>
#include "subtask_support.h"

#include <proto/dos.h>

#include <clib/exec_protos.h>
#include <graphics/gfxbase.h>

#include <clib/alib_protos.h>  /* for CreatePort */
#include <devices/audio.h>
#include <string.h>
#include <unistd.h>    // sleep


#ifdef mc68060
#define __CPU__ "mc68060"
#elif defined  mc68040
#define __CPU__ "mc68040"
#elif  defined mc68030
#define __CPU__ "mc68030"
#elif  defined mc68020
#define __CPU__ "mc68020"
#elif  defined mc68000
#define __CPU__ "mc68000"
#else
#define __CPU__ "???????"
#endif

#ifdef __HAVE_68881__
#define __FPU__ "mc68881"
#else
#define __FPU__ ""
#endif

extern "C" LONG KPrintF(STRPTR format, ...);

#include <proto/timer.h>
static struct timeval startTime;

void startup() {
	GetSysTime(&startTime);
}


extern unsigned int global_bufsize_factor;   // AF Test einstellbare Audio Buffergroesse n mal 512 Bytes
extern unsigned int global_benchmark_flag;   // AF Einschalten der Ausgabe der durchschnittlichen Zeit fuer die portaudio-Callbackfunktion
extern char *global_ProgramName;             // AF argv[0]

ULONG getMilliseconds() {
	struct timeval endTime;

	GetSysTime(&endTime);
	SubTime(&endTime, &startTime);

	return (endTime.tv_secs * 1000 + endTime.tv_micro / 1000);
}



FILE *File=NULL;    // testweise samples in File schreiben


//UBYTE chans[] ={1,2,4,8};  /* get any of the four channels RKRM Audio-example */
UBYTE chans[] ={3,5,10,12};  /* get a pair of stereo channels, RKRM Devices */
//UBYTE chans[] ={1,8};   /* test, nur rechts */
//UBYTE chans[] ={2,4};   /* test, nur links */

typedef struct
{
	int StreamActive;
	unsigned int AF_StreamID;   // just a number to see which stream is referred to

	struct SubTask *st;
	struct SignalSemaphore sema;    /* data item protection      */

	int numInputChannels;
	int numOutputChannels;          // 1 for mono, 2 for stereo
	PaSampleFormat sampleFormat;
	double sampleRate;
	unsigned long framesPerBuffer;  // a frames is one complete sample from each channel
	unsigned long numberOfBuffers;  // suggestion for (double) bufering
	PortAudioCallback *callback;
	void *userData;
	unsigned int clock;             // Amiga Systemclock 3546895 for PAL, 3579545 for NTSC */
	unsigned long speed;            // clock / samplerate
	struct IOAudio *AIOptr1;        // four audio I/O blocks
	struct IOAudio *AIOptr1_2;      // second channel
	struct IOAudio *AIOptr2;
	struct IOAudio *AIOptr2_2;      // second channel
	struct IOAudio *AIOptrStartStop;// for CMD_START and CMD_STOP
	struct MsgPort *port1;
	struct MsgPort *port1_2;
	struct MsgPort *port2;
	struct MsgPort *port2_2;
	struct MsgPort *portStartStop;
	ULONG device;                   // Audio device handle
	BYTE *ChipMemBuf1;              // Amiga-Audiobuffer
	BYTE *ChipMemBuf2;              // Amiga-Audiobuffer
	SHORT *FastMemBuf1;             // Espeak-AudioBuffer used in Callback
	SHORT *FastMemBuf2;             // Espeak-AudioBuffer used in Callback
	unsigned int LeftChannel;       // left Audiochannel we got allocated in OpenDevice() or 0
	unsigned int RightChannel;      // right Audiochannel we got allocated in OpenDevice() or 0


}PortAudioStreamStruct;

UWORD Convert16SSamples(SHORT *Source16S, BYTE *Dest8S, ULONG SampleCount);  /* reads 16Bit Signed Samples from Source and writes 8Bit Signed Samples to Dest.  DestLen Samples will be read */


struct Device* TimerBase;           // nur zum Benchmarking
static struct IORequest timereq;    // nur zum Benchmarking

VOID /*__asm __saveds*/ EspeakAudioTask(VOID)
{

	//	KPrintF("%s() called\n",__FUNCTION__);



	struct Task *me = FindTask(NULL);
	struct SubTask *st;
	struct SubTaskMsg *stm;

	/*
	 ** Wait for our startup message from the SpawnSubTask() function.
	 */

	WaitPort(&((struct Process *)me)->pr_MsgPort);
	stm  = (struct SubTaskMsg *)GetMsg(&((struct Process *)me)->pr_MsgPort);
	st   = (struct SubTask *)stm->stm_Parameter;




	{
		//		struct Data *data = (struct Data *)st->st_Data;
		BOOL running = TRUE;
		BOOL worktodo = FALSE;

		PortAudioStreamStruct *PortAudioStreamData=(PortAudioStreamStruct*)(st->st_Data);
		int Error=paInternalError;

		if(0==OpenDevice((const char *)TIMERNAME, UNIT_MICROHZ, &timereq, 0))   // nur zum test
		{
			TimerBase = timereq.io_Device;	            // nur zum test

			/* Make four Reply-Ports */
			PortAudioStreamData->port1=CreatePort(0,0);
			if(PortAudioStreamData->port1)
			{
				PortAudioStreamData->port1_2=CreatePort(0,0);
				if(PortAudioStreamData->port1_2)
				{
					PortAudioStreamData->port2=CreatePort(0,0);
					if(PortAudioStreamData->port2)
					{
						PortAudioStreamData->port2_2=CreatePort(0,0);
						if(PortAudioStreamData->port2_2)
						{
							PortAudioStreamData->portStartStop=CreatePort(0,0);
							if(PortAudioStreamData->portStartStop)
							{

								/* Now Open Audio Device */
								//				KPrintF("Attempting to open Audio.device\n");
								/* set up audio I/O block for channel   */
								/* allocation and Open the audio.device */
								PortAudioStreamData->AIOptr1->ioa_Request.io_Message.mn_ReplyPort=PortAudioStreamData->port1;
								PortAudioStreamData->AIOptr1->ioa_Request.io_Message.mn_Node.ln_Pri=127; /* no stealing */
								PortAudioStreamData->AIOptr1->ioa_AllocKey=0;
								PortAudioStreamData->AIOptr1->ioa_Data=chans;
								PortAudioStreamData->AIOptr1->ioa_Length=sizeof(chans);


								PortAudioStreamData->device=OpenDevice((const char *)AUDIONAME,0L,(struct IORequest*)PortAudioStreamData->AIOptr1,0L);

								if(PortAudioStreamData->device==0)
								{
									UBYTE GotChannels;

									/* Set Up Audio IO Blocks for Sample Playing */
									//printf("AllocKey is 0x%02u\n",PortAudioStreamData->AIOptr1->ioa_AllocKey);
									//printf("ioa_Request.io_Unit=0x%02u\n",(UBYTE)PortAudioStreamData->AIOptr1->ioa_Request.io_Unit);  /* the channels I got are in ioUnit, lower 4 bits. */


									GotChannels=((UBYTE)PortAudioStreamData->AIOptr1->ioa_Request.io_Unit) & 0x0f; /* the channels I got are in ioUnit, lower 4 bits. */
									if(GotChannels & 0x02) PortAudioStreamData->LeftChannel=0x02;
									else if(GotChannels & 0x04) PortAudioStreamData->LeftChannel=0x04;
									else PortAudioStreamData->LeftChannel=0;

									if(GotChannels & 0x01) PortAudioStreamData->RightChannel=0x01;
									else if(GotChannels & 0x08) PortAudioStreamData->RightChannel=0x08;
									else PortAudioStreamData->RightChannel=0;

									//printf("Left  Channel: %u\n",PortAudioStreamData->LeftChannel);
									//printf("Right Channel: %u\n",PortAudioStreamData->RightChannel);

									PortAudioStreamData->AIOptr1->ioa_Request.io_Command=CMD_WRITE;
									PortAudioStreamData->AIOptr1->ioa_Request.io_Flags=ADIOF_PERVOL;

									/* Volume */
									PortAudioStreamData->AIOptr1->ioa_Volume=64; /* (0 thru 64, linear) */

									/* Period/Cycles */
									PortAudioStreamData->AIOptr1->ioa_Period=(UWORD)PortAudioStreamData->speed;
									PortAudioStreamData->AIOptr1->ioa_Cycles=1;

									*(PortAudioStreamData->AIOptr1_2)=*(PortAudioStreamData->AIOptr1);       /* Make sure we have the same allocation keys */
									*(PortAudioStreamData->AIOptr2)  =*(PortAudioStreamData->AIOptr1);       /* Make sure we have the same allocation keys */
									*(PortAudioStreamData->AIOptr2_2)=*(PortAudioStreamData->AIOptr1);       /* Make sure we have the same allocation keys */
									*(PortAudioStreamData->AIOptrStartStop)=*(PortAudioStreamData->AIOptr1); /* Make sure we have the same allocation keys */
									/* same channel selected (allocKeys) and same flags       */
									/* but different ports...                     */
									PortAudioStreamData->AIOptr1  ->ioa_Request.io_Message.mn_ReplyPort=PortAudioStreamData->port1;  /* Left and right Channels are running exactly synchronous. They can therefore use the same port */
									PortAudioStreamData->AIOptr1_2->ioa_Request.io_Message.mn_ReplyPort=PortAudioStreamData->port1_2;
									PortAudioStreamData->AIOptr2  ->ioa_Request.io_Message.mn_ReplyPort=PortAudioStreamData->port2;
									PortAudioStreamData->AIOptr2_2->ioa_Request.io_Message.mn_ReplyPort=PortAudioStreamData->port2_2;
									PortAudioStreamData->AIOptrStartStop->ioa_Request.io_Message.mn_ReplyPort=PortAudioStreamData->portStartStop;
									/* and different Channels in ioUnit -> CMD_WRITE is a single channel command */
									(ULONG)PortAudioStreamData->AIOptr1  ->ioa_Request.io_Unit &=0xf0; (ULONG)PortAudioStreamData->AIOptr1  ->ioa_Request.io_Unit |=  PortAudioStreamData->LeftChannel;     /* lower 4 Bits contain ONE channel bit */
									(ULONG)PortAudioStreamData->AIOptr1_2->ioa_Request.io_Unit &=0xf0; (ULONG)PortAudioStreamData->AIOptr1_2->ioa_Request.io_Unit |=  PortAudioStreamData->RightChannel;
									(ULONG)PortAudioStreamData->AIOptr2  ->ioa_Request.io_Unit &=0xf0; (ULONG)PortAudioStreamData->AIOptr2  ->ioa_Request.io_Unit |=  PortAudioStreamData->LeftChannel;
									(ULONG)PortAudioStreamData->AIOptr2_2->ioa_Request.io_Unit &=0xf0; (ULONG)PortAudioStreamData->AIOptr2_2->ioa_Request.io_Unit |=  PortAudioStreamData->RightChannel;


									// Data...

									if ((st->st_Port = CreateMsgPort()))
									{
										/*
										 ** Reply startup message, everything ok.
										 ** Note that if the initialization fails, the code falls
										 ** through and replies the startup message with a stm_Result
										 ** of 0 after a Forbid(). This tells SpawnSubTask() that the
										 ** sub task failed to run.
										 */

										/* variables are declared here because they should be initialized to Zero */
										/* in the for loop they would become initialized at the beginning of every turn - which is wrong */
										struct IOAudio *Aptr=NULL;  /* for double buffer switching */
										struct MsgPort *port=NULL;  /* for double buffer switching */
										struct MsgPort *port_2=NULL; /* for double buffer switching for the second audio channel (we do 2-channel(MONO) playback)*/
										BYTE *ChipMemBuf=NULL;      /* Amiga-Audiobuffer for double buffer switching */
										SHORT *FastMemBuf=NULL;     /* Espeak-AudioBuffer used in Callback for double buffer switching */

										ULONG CallbackTime=0;       /* For Callback benchmarking */
										ULONG CallBackCount=0;
										ULONG CallBackMaxTime=0;

										stm->stm_Result = TRUE;
										ReplyMsg((struct Message *)stm);
										// Ok, all opened sucessfully
										//					KPrintF("Opened Audio.device successfully\n");

										/*
										 ** after the sub task is up and running, we go into
										 ** a loop and process the messages from the main task.
										 */

										for (;;)
										{
											while ((stm = (struct SubTaskMsg *)GetMsg(st->st_Port)))
											{
												switch (stm->stm_Command)
												{
												case STC_SHUTDOWN:
													/*
													 ** This is the shutdown message from KillSubTask().
													 */
													//								printf("Task STC_SHUTDOWN\n");
													running = FALSE;
													break;

												case STC_START:
													/*
													 ** we received a start message with a fractal description.
													 ** clear the rastport and the line update array and start
													 ** rendering.
													 */
													//								printf("Task STC_START\n");

													worktodo = TRUE;

													/* read two buffers. cannot fail. (just end of data but that does not matter here) */
													PortAudioStreamData->callback(NULL,PortAudioStreamData->FastMemBuf1,PortAudioStreamData->framesPerBuffer,NULL,NULL);

													Convert16SSamples(PortAudioStreamData->FastMemBuf1, PortAudioStreamData->ChipMemBuf1 ,PortAudioStreamData->framesPerBuffer);
													PortAudioStreamData->AIOptr1->ioa_Length=PortAudioStreamData->framesPerBuffer;
													PortAudioStreamData->AIOptr1->ioa_Data=(UBYTE*)PortAudioStreamData->ChipMemBuf1;

													PortAudioStreamData->AIOptr1_2->ioa_Length=PortAudioStreamData->framesPerBuffer;      /* second channel */
													PortAudioStreamData->AIOptr1_2->ioa_Data=(UBYTE*)PortAudioStreamData->ChipMemBuf1;    /* second channel */



													PortAudioStreamData->callback(NULL,PortAudioStreamData->FastMemBuf2,PortAudioStreamData->framesPerBuffer,NULL,NULL);
													Convert16SSamples(PortAudioStreamData->FastMemBuf2, PortAudioStreamData->ChipMemBuf2 ,PortAudioStreamData->framesPerBuffer);
													PortAudioStreamData->AIOptr2->ioa_Length=PortAudioStreamData->framesPerBuffer;
													PortAudioStreamData->AIOptr2->ioa_Data=(UBYTE*)PortAudioStreamData->ChipMemBuf2;

													PortAudioStreamData->AIOptr2_2->ioa_Length=PortAudioStreamData->framesPerBuffer;      /* second channel */
													PortAudioStreamData->AIOptr2_2->ioa_Data=(UBYTE*)PortAudioStreamData->ChipMemBuf2;    /* second channel */

													/* Synchonized Start of both stereo channels -> CMD_STOP, CMD_WRITE, CMD_START */
													(struct IORequest*)PortAudioStreamData->AIOptrStartStop->ioa_Request.io_Command=CMD_STOP;
													DoIO((struct IORequest*)PortAudioStreamData->AIOptrStartStop);                                /* synchronous call, cares about complete-message */

													/* Start playback */
													BeginIO((struct IORequest*)PortAudioStreamData->AIOptr1);
													BeginIO((struct IORequest*)PortAudioStreamData->AIOptr1_2);
													BeginIO((struct IORequest*)PortAudioStreamData->AIOptr2);
													BeginIO((struct IORequest*)PortAudioStreamData->AIOptr2_2);

//KPrintF("AIOptr1=0x%08lx\n",PortAudioStreamData->AIOptr1);
//KPrintF("AIOptr1_2=0x%08lx\n",PortAudioStreamData->AIOptr1_2);
//KPrintF("AIOptr2=0x%08lx\n",PortAudioStreamData->AIOptr2);
//KPrintF("AIOptr2_2=0x%08lx\n",PortAudioStreamData->AIOptr2_2);

													(struct IORequest*)PortAudioStreamData->AIOptrStartStop->ioa_Request.io_Command=CMD_START;
													DoIO((struct IORequest*)PortAudioStreamData->AIOptrStartStop);                                /* synchronous call, cares about complete-message */

													/* set the double buffer switching */
													Aptr=PortAudioStreamData->AIOptr1;
													port=PortAudioStreamData->port1;
													port_2=PortAudioStreamData->port1_2;
													FastMemBuf=PortAudioStreamData->FastMemBuf1;
													ChipMemBuf=PortAudioStreamData->ChipMemBuf1;
													break;

												case STC_STOP:
													/* this message is not used in this example */
													//								printf("Task STC_STOP\n");
													worktodo = FALSE;
													break;
												}

												/*
												 ** If we received a shutdown message, we do not reply it
												 ** immediately. First, we need to free our resources.
												 */
												if (!running) break;

												ReplyMsg((struct Message *)stm);
											}

											if (!running) break;

											if (worktodo)
											{
												/* if there is work to do,...
												 */
												/*
								        void *inputBuffer, void *outputBuffer, unsigned long framesPerBuffer, PaTimestamp outTime, void *userData );
												 */

												struct Message *msg;
												ULONG wakebit;

												wakebit=Wait(1 << port->mp_SigBit);
												while((msg=GetMsg(port))==0)
												{
												};
//KPrintF("GetMsg() returned 0x%08lx\n",msg);
												wakebit=Wait(1 << port_2->mp_SigBit);
												while((msg=GetMsg(port_2))==0)  // the 2. channel has also finished. Clear that message queue, too
												{
												};


												if(global_benchmark_flag)
												{
													startup();     /* set CallbackTime startvalue */
												}
												//							printf("Callback() called %u\n",count);
												if( 0!=PortAudioStreamData->callback(NULL,FastMemBuf,PortAudioStreamData->framesPerBuffer,NULL,NULL))  /* Last Buffer */
												{
													/* we queue this last buffer for playback and wait for the previous one and this one  to finish */

													//								printf("Callback() called %u, return != paContinue. End of stream.\n",count);
													PortAudioStreamData->StreamActive=0;
													worktodo=FALSE;
													Convert16SSamples(FastMemBuf, ChipMemBuf ,PortAudioStreamData->framesPerBuffer);
													BeginIO((struct IORequest*)Aptr);
													BeginIO((Aptr==PortAudioStreamData->AIOptr1)?(struct IORequest*)PortAudioStreamData->AIOptr1_2:(struct IORequest*)PortAudioStreamData->AIOptr2_2); /* also corresponding 2. stereo-channel */
													/* Wait for the last two buffers to finish */
													if(Aptr==(struct IOAudio*)PortAudioStreamData->AIOptr1)
													{
														Aptr=PortAudioStreamData->AIOptr2;
														port=PortAudioStreamData->port2;
														port_2=PortAudioStreamData->port2_2;
													}
													else
													{
														Aptr=PortAudioStreamData->AIOptr1;
														port=PortAudioStreamData->port1;
														port_2=PortAudioStreamData->port1_2;
													}
													wakebit=Wait(1 << port->mp_SigBit);  /* wait until last buffer has finished */
													while((msg=GetMsg(port))==0) {};
													wakebit=Wait(1 << port_2->mp_SigBit);  /* wait until last buffer has finished */
													while((msg=GetMsg(port_2))==0) {};

													if(Aptr==(struct IOAudio*)PortAudioStreamData->AIOptr1)
													{
														port=PortAudioStreamData->port2;
														port_2=PortAudioStreamData->port2_2;
													}
													else
													{
														port=PortAudioStreamData->port1;
														port_2=PortAudioStreamData->port1_2;
													}
													wakebit=Wait(1 << port->mp_SigBit);  /* wait until last bufer has finished */
													while((msg=GetMsg(port))==0) {};
													wakebit=Wait(1 << port_2->mp_SigBit);  /* wait until last bufer has finished */
													while((msg=GetMsg(port_2))==0) {};
												}
												else /* There are more buffers to come */
												{
													if(global_benchmark_flag)
													{
														unsigned long Time=getMilliseconds();  /* measure time for callbacks for benchmarking */
														if(Time>CallBackMaxTime)
														{
															CallBackMaxTime=Time;
														}
														CallbackTime+=Time;
														CallBackCount+=1;
													}
													//KPrintF("Convert16SSamples($%08lx,$%08lx)\n",(ULONG)PortAudioStreamData->FastMemBuf1, (ULONG)PortAudioStreamData->ChipMemBuf1);

													//fwrite(PortAudioStreamData->FastMemBuf1,PortAudioStreamData->framesPerBuffer,1,File);
													Convert16SSamples(FastMemBuf, ChipMemBuf ,PortAudioStreamData->framesPerBuffer);
													//	fwrite(PortAudioStreamData->ChipMemBuf1,PortAudioStreamData->framesPerBuffer,1,File);
													BeginIO((struct IORequest*)Aptr);
													BeginIO((Aptr==PortAudioStreamData->AIOptr1)?(struct IORequest*)PortAudioStreamData->AIOptr1_2:(struct IORequest*)PortAudioStreamData->AIOptr2_2); /* also corresponding 2. stereo-channel */

													/* Double buffer switching */
													if(Aptr==(struct IOAudio*)PortAudioStreamData->AIOptr1)
													{
														Aptr=PortAudioStreamData->AIOptr2;
														port=PortAudioStreamData->port2;
														port_2=PortAudioStreamData->port2_2;
														FastMemBuf=PortAudioStreamData->FastMemBuf2;
														ChipMemBuf=PortAudioStreamData->ChipMemBuf2;
													}
													else
													{
														Aptr=PortAudioStreamData->AIOptr1;
														port=PortAudioStreamData->port1;
														port_2=PortAudioStreamData->port1_2;
														FastMemBuf=PortAudioStreamData->FastMemBuf1;
														ChipMemBuf=PortAudioStreamData->ChipMemBuf1;
													}

												}

												//startup(); // Zeit Anfang
												//KPrintF("PortAudioStreamData->port1->mp_SigBit = 0x%04lx\n",(ULONG)PortAudioStreamData->port1->mp_SigBit);
												//KPrintF("wakebit                               = 0x%04lx\n",(ULONG)wakebit);
												//KPrintF("st->st_Port->mp_SigBit                = 0x%04lx\n\n",(ULONG)st->st_Port->mp_SigBit);

												//ULONG Zeit=getMilliseconds();
												//KPrintF("Dauer: %lums\n",Zeit);

												//	fwrite(PortAudioStreamData->ChipMemBuf1,PortAudioStreamData->framesPerBuffer,1,File);
												/* Since we are very busy working, we do not Wait() for signals. */
											}
											else
											{
												/* We have nothing to do, just sit quietly and wait for something to happen */
												WaitPort(st->st_Port);
											}
										}

										Error=paNoError;
										//					return;// Error;

										/* Wait for sound to finish ALEXANDER */
										PortAudioStreamData->AIOptr1->ioa_Request.io_Command=ADCMD_FINISH;
										BeginIO((struct IORequest*)PortAudioStreamData->AIOptr1);
										PortAudioStreamData->AIOptr2->ioa_Request.io_Command=ADCMD_FINISH;
										BeginIO((struct IORequest*)PortAudioStreamData->AIOptr2);

										if(global_benchmark_flag)   /* if Benchmarking was done */
										{
											printf("%s, Average BufferTime %lums, Max Buffertime %lums, (Buffer is %lums), Compiled for "__CPU__" "__FPU__"\n",global_ProgramName,CallbackTime/CallBackCount,CallBackMaxTime,(ULONG)(global_bufsize_factor*512*1000/PortAudioStreamData->sampleRate));
										}
									}
									else  // st->st_Port = CreateMsgPort() failed
									{
										printf("    Subtask CreateMsgPort() failed\n");
										Error=paInsufficientMemory;
									}
									//KPrintF("Calling CloseDevice\n");
									CloseDevice((struct IORequest*)PortAudioStreamData->AIOptr1);
									PortAudioStreamData->device=1;
								}
								else
								{
									printf("    Could not open "AUDIONAME"\n");
									Error=paDeviceUnavailable;
								}
								/* --------------------- */
								//KPrintF("Calling DeletePort2_2\n");
								DeletePort(PortAudioStreamData->port2_2);
								PortAudioStreamData->port2_2=NULL;
							}
							else
							{
								printf("    Could not create portStartStop\n");
								Error=paInsufficientMemory;
							}
							//KPrintF("Calling DeletePortStartStop\n");
							DeletePort(PortAudioStreamData->portStartStop);
							PortAudioStreamData->portStartStop=NULL;
						}
						else
						{
							printf("    Could not create port2_2\n");
							Error=paInsufficientMemory;
						}
						//KPrintF("Calling DeletePort2\n");
						DeletePort(PortAudioStreamData->port2);
						PortAudioStreamData->port2=NULL;
					}
					else
					{
						printf("    Could not create port1_2\n");
						Error=paInsufficientMemory;
					}
					//KPrintF("Calling DeletePort1_2\n");
					DeletePort(PortAudioStreamData->port1_2);
					PortAudioStreamData->port1_2=NULL;
				}
				else
				{
					printf("    Could not create port1\n");
					Error=paInsufficientMemory;
				}
				//KPrintF("Calling DeletePort1\n");
				DeletePort(PortAudioStreamData->port1);
				PortAudioStreamData->port1=NULL;
			}
			else
			{
				printf("    Could not create port1\n");
				Error=paInsufficientMemory;
			}

			CloseDevice(&timereq);
		}
		else
		{
			printf("    Could not open "TIMERNAME"\n");
		}

		// Error uebergeben?
		//KPrintF("Calling ExitSubtask\n");
		ExitSubTask(st,stm);
	}
}


/* ##################################################################################################### */

PortAudioStream *GlobalPaStreamPtr=NULL;  /* used for atexit(). Functions there have void parameter -> global Pointer needed */


/*
	Pa_StartStream() and Pa_StopStream() begin and terminate audio processing.
	Pa_StopStream() waits until all pending audio buffers have been played.
	Pa_AbortStream() stops playing immediately without waiting for pending
	buffers to complete.
 */

PaError Pa_StartStream( PortAudioStream *stream )
{
	int Error;
	//	KPrintF("%s() called\n",__FUNCTION__);
	if(stream)
	{
		//		printf(" %s called for Stream %u\n",__FUNCTION__,((PortAudioStreamStruct*)stream)->AF_StreamID);
		((PortAudioStreamStruct*)stream)->StreamActive=1;
		SendSubTaskMsg(((PortAudioStreamStruct*)stream)->st,STC_START,NULL);
		Error=paNoError;
	}
	else
	{
		//		printf(" %s called with NULL-Ptr\n",__FUNCTION__);
		Error=paBadStreamPtr;

	}

	return Error;
}

/* ########################################################################## */


//PaError Pa_StopStream( PortAudioStream *stream );

PaError Pa_AbortStream( PortAudioStream *stream )
{
	//	KPrintF("%s() called\n",__FUNCTION__);
	//	printf(" %s called for Stream %u\n",__FUNCTION__,((PortAudioStreamStruct*)stream)->AF_StreamID);
	((PortAudioStreamStruct*)stream)->StreamActive=0;
	return paNoError;
}

/* ########################################################################## */
/*
	Pa_StreamActive() returns one when the stream is playing audio,
	zero when not playing, or a negative error number if the
	stream is invalid.
	The stream is active between calls to Pa_StartStream() and Pa_StopStream(),
	but may also become inactive if the callback returns a non-zero value.
	In the latter case, the stream is considered inactive after the last
	buffer has finished playing.
 */


PaError Pa_StreamActive( PortAudioStream *stream )
{
	//	KPrintF("%s() called\n",__FUNCTION__);

	if(stream)
	{
		//		printf(" %s called for Stream %u\n returning %u\n",__FUNCTION__,((PortAudioStreamStruct*)stream)->AF_StreamID,((PortAudioStreamStruct*)stream)->StreamActive);

		return ((PortAudioStreamStruct*)stream)->StreamActive;   //paNoError;
	}
	else
	{
		//		printf(" %s called with NULL-Ptr\n",__FUNCTION__);
		//		printf("    returning paBadStreamPtr\n");
		return paBadStreamPtr;   // <0 is error

	}
}

/* ########################################################################## */
/*
	Pa_OpenDefaultStream() is a simplified version of Pa_OpenStream() that
	opens the default input and/or ouput devices. Most parameters have
	identical meaning to their Pa_OpenStream() counterparts, with the following
	exceptions:

	If either numInputChannels or numOutputChannels is 0 the respective device
	is not opened (same as passing paNoDevice in the device arguments to Pa_OpenStream() )

	sampleFormat applies to both the input and output buffers.
 */



PaError Pa_OpenDefaultStream( PortAudioStream** stream,
		int numInputChannels,
		int numOutputChannels,
		PaSampleFormat sampleFormat,
		double sampleRate,
		unsigned long framesPerBuffer,
		unsigned long numberOfBuffers,
		PortAudioCallback *callback,
		void *userData )
{

	//	KPrintF("%s() called\n",__FUNCTION__);
	/*
	File=fopen("Samples","w");
	if(!File)
	{
		printf("Kann File nicht oeffnen\n");
		return paInternalError;
	}
	 */

	static unsigned int StreamNr=1;
	PortAudioStreamStruct* StreamStruct;
	int Error=paInternalError;
	struct GfxBase *GfxBase;
	unsigned int clock;

	//	printf(" %s called\n",__FUNCTION__);
	//	printf("    numInputChannels=%d\n",numInputChannels);
	//	printf("    numOutputChannels=%d\n",numOutputChannels);
	//	printf("    sampleFormat=%lu (%u bytes)\n",sampleFormat,Pa_GetSampleSize(sampleFormat));
	//	printf("    sampleRate=%f\n",sampleRate);
	//	printf("    framesPerBuffer=%lu\n",framesPerBuffer);        // a frame is numOutputChannels * BytesPerSample
	//	printf("    numberOfBuffers=%lu\n",numberOfBuffers);        // this is only a suggestion how many buffers to use, e.g double buffering
	//	printf("    userData=$%p\n",userData);


	GfxBase = (struct GfxBase *)OpenLibrary((const char *)"graphics.library",0L);
	if (GfxBase)
	{
		if (GfxBase->DisplayFlags & PAL)
		{
			clock = 3546895;        /* PAL clock */
		}
		else
		{
			clock = 3579545;        /* NTSC clock */
		}
		CloseLibrary((struct Library *) GfxBase);

		StreamStruct=(PortAudioStreamStruct*)malloc(sizeof(PortAudioStreamStruct));
		if(StreamStruct)
		{
			memset(StreamStruct,0,sizeof(PortAudioStreamStruct));  /* initilize all entries */
			StreamStruct->device=1;                                /* device must be initialized to !=0 (for 0 is success) */

			//			printf("    Setting Stream to %u\n",StreamNr);
			StreamStruct->AF_StreamID=StreamNr;
			StreamNr++;

			StreamStruct->callback=callback;  // store ptr to callback function
			StreamStruct->framesPerBuffer=framesPerBuffer*global_bufsize_factor;   // AF Test mit n mal 512 Byte Puffern
			StreamStruct->numInputChannels=numInputChannels;
			StreamStruct->numOutputChannels=numOutputChannels;
			StreamStruct->sampleFormat=sampleFormat;
			StreamStruct->sampleRate=sampleRate;

			StreamStruct->clock=clock;

			/*----------------------------------*/
			/* Calculate playback sampling rate */
			/*----------------------------------*/
			StreamStruct->speed =  (ULONG) (clock / sampleRate);


			// ########################################

			*stream=StreamStruct;

			GlobalPaStreamPtr=StreamStruct;   /* in case of CTRL-C atexit()-Functions are void so a global pointer is needed */

			/* Allocate two Audio Buffers in ChipMem for Amiga-8Bit-Samples */
			StreamStruct->ChipMemBuf1=(BYTE*)AllocMem(StreamStruct->framesPerBuffer,MEMF_CHIP);
			if(StreamStruct->ChipMemBuf1)
			{
				StreamStruct->ChipMemBuf2=(BYTE*)AllocMem(StreamStruct->framesPerBuffer,MEMF_CHIP);
				if(StreamStruct->ChipMemBuf2)
				{
					/* Allocate two Audio Buffers in FastMem for Espeak-16Bit-Samples */
					StreamStruct->FastMemBuf1=(SHORT*)AllocMem(StreamStruct->framesPerBuffer*Pa_GetSampleSize(StreamStruct->sampleFormat),MEMF_PUBLIC);
					//KPrintF("Allocating %lu Bytes for FastMemBuf1, Ptr=$%08lx\n",(ULONG)StreamStruct->framesPerBuffer*Pa_GetSampleSize(StreamStruct->sampleFormat),(ULONG)StreamStruct->FastMemBuf1);
					if(StreamStruct->FastMemBuf1)
					{
						StreamStruct->FastMemBuf2=(SHORT*)AllocMem(StreamStruct->framesPerBuffer*Pa_GetSampleSize(StreamStruct->sampleFormat),MEMF_PUBLIC);
						if(StreamStruct->FastMemBuf2)
						{
							/* Allocate two audio IO Blocks */
							StreamStruct->AIOptr1=(struct IOAudio*)AllocMem(sizeof(struct IOAudio),MEMF_PUBLIC|MEMF_CLEAR);
							if(StreamStruct->AIOptr1)
							{
								StreamStruct->AIOptr1_2=(struct IOAudio*)AllocMem(sizeof(struct IOAudio),MEMF_PUBLIC|MEMF_CLEAR);
								if(StreamStruct->AIOptr1_2)
								{
									StreamStruct->AIOptr2=(struct IOAudio*)AllocMem(sizeof(struct IOAudio),MEMF_PUBLIC|MEMF_CLEAR);
									if(StreamStruct->AIOptr2)
									{
										StreamStruct->AIOptr2_2=(struct IOAudio*)AllocMem(sizeof(struct IOAudio),MEMF_PUBLIC|MEMF_CLEAR);
										if(StreamStruct->AIOptr2_2)
										{
											StreamStruct->AIOptrStartStop=(struct IOAudio*)AllocMem(sizeof(struct IOAudio),MEMF_PUBLIC|MEMF_CLEAR);
											if(StreamStruct->AIOptrStartStop)
											{

												StreamStruct->st = SpawnSubTask("EspeakAudioTask",EspeakAudioTask,StreamStruct);
												if (StreamStruct->st)
												{
													Error=paNoError;
													return Error;
												}
												Error=paInternalError;
												FreeMem(StreamStruct->AIOptr2,sizeof(struct IOAudio));
												StreamStruct->AIOptr2=NULL;
											}
											else
											{
												printf("    not enough memory for allocating  AIOptrStartStop\n");
												Error=paInsufficientMemory;
											}
											FreeMem(StreamStruct->AIOptr2_2,sizeof(struct IOAudio));
										}
										else
										{
											printf("    not enough memory for allocating  AIOptr2_2\n");
											Error=paInsufficientMemory;
										}
										FreeMem(StreamStruct->AIOptr2,sizeof(struct IOAudio));
										StreamStruct->AIOptr2=NULL;
									}
									else
									{
										printf("    not enough memory for allocating  AIOptr2\n");
										Error=paInsufficientMemory;
									}
									FreeMem(StreamStruct->AIOptr1_2,sizeof(struct IOAudio));
									StreamStruct->AIOptr1_2=NULL;

								}
								else
								{
									printf("    not enough memory for allocating  AIOptr1_2\n");
									Error=paInsufficientMemory;
								}
								FreeMem(StreamStruct->AIOptr1,sizeof(struct IOAudio));
								StreamStruct->AIOptr1=NULL;
							}
							else
							{
								printf("    not enough memory for allocating  AIOptr1\n");
								Error=paInsufficientMemory;
							}
							FreeMem(StreamStruct->FastMemBuf2,StreamStruct->framesPerBuffer*Pa_GetSampleSize(StreamStruct->sampleFormat));
							StreamStruct->FastMemBuf2=NULL;
						}
						else
						{
							printf("    Could not allocate FastMemBuf2 (%lu Bytes)\n",StreamStruct->framesPerBuffer*Pa_GetSampleSize(StreamStruct->sampleFormat));
							Error=paInsufficientMemory;
						}
						FreeMem(StreamStruct->FastMemBuf1,StreamStruct->framesPerBuffer*Pa_GetSampleSize(StreamStruct->sampleFormat));
						StreamStruct->FastMemBuf1=NULL;
					}
					else
					{
						printf("    Could not allocate FastMemBuf1 (%lu Bytes)\n",StreamStruct->framesPerBuffer*Pa_GetSampleSize(StreamStruct->sampleFormat));
						Error=paInsufficientMemory;
					}
					FreeMem(StreamStruct->ChipMemBuf2,StreamStruct->framesPerBuffer);
					StreamStruct->ChipMemBuf2=NULL;
				}
				else
				{
					printf("    Could not allocate ChipMemBuf2 (%lu Bytes)\n",StreamStruct->framesPerBuffer);
					Error=paInsufficientMemory;
				}
				FreeMem(StreamStruct->ChipMemBuf1,StreamStruct->framesPerBuffer);
				StreamStruct->ChipMemBuf1=NULL;
			}
			else
			{
				printf("    Could not allocate ChipMemBuf1 (%lu Bytes)\n",StreamStruct->framesPerBuffer);
				Error=paInsufficientMemory;
			}

			free(StreamStruct);
			StreamStruct=NULL;
			*stream=NULL;
			GlobalPaStreamPtr=NULL;   /* in case of CTRL-C atexit()-Functions are void so a global pointer is needed */

		}
		else
		{
			printf("    not enough memory for allocating stream %u\n",StreamNr);
			Error=paInsufficientMemory;
		}

	}
	else
	{
		printf("Unable to open graphics.library\n");
		Error=paInternalError;

	}
	//	KPrintF("Finished %s\n",__FUNCTION__);
	return Error;
}


/* ########################################################################## */
/*
	Pa_CloseStream() closes an audio stream, flushing any pending buffers.
 */

PaError Pa_CloseStream( PortAudioStream *stream )
{
	//	KPrintF("%s() called\n",__FUNCTION__);

	PortAudioStreamStruct *StreamStruct=(PortAudioStreamStruct*)stream;

	if(stream)
	{
		//		printf(" %s called for Stream %u\n",__FUNCTION__,StreamStruct->AF_StreamID);

		if(StreamStruct->st)
		{
			KillSubTask(StreamStruct->st);
			StreamStruct->st=NULL;
		}

		if(StreamStruct->device==0)
		{
			CloseDevice((struct IORequest*)StreamStruct->AIOptr1);
			StreamStruct->device=1;
		}

		if(StreamStruct->portStartStop)
		{
			DeletePort(StreamStruct->portStartStop);
			StreamStruct->portStartStop=0;
		}

		if(StreamStruct->port2_2)
		{
			DeletePort(StreamStruct->port2_2);
			StreamStruct->port2_2=0;
		}


		if(StreamStruct->port2)
		{
			DeletePort(StreamStruct->port2);
			StreamStruct->port2=0;
		}

		if(StreamStruct->port1_2)
		{
			DeletePort(StreamStruct->port1_2);
			StreamStruct->port1_2=0;
		}

		if(StreamStruct->port1)
		{
			DeletePort(StreamStruct->port1);
			StreamStruct->port1=0;
		}

		if(StreamStruct->AIOptrStartStop)
		{
			FreeMem(StreamStruct->AIOptrStartStop,sizeof(struct IOAudio));
			StreamStruct->AIOptrStartStop=NULL;
		}

		if(StreamStruct->AIOptr2_2)
		{
			FreeMem(StreamStruct->AIOptr2_2,sizeof(struct IOAudio));
			StreamStruct->AIOptr2_2=NULL;
		}

		if(StreamStruct->AIOptr2)
		{
			FreeMem(StreamStruct->AIOptr2,sizeof(struct IOAudio));
			StreamStruct->AIOptr2=NULL;
		}

		if(StreamStruct->AIOptr1_2)
		{
			FreeMem(StreamStruct->AIOptr1_2,sizeof(struct IOAudio));
			StreamStruct->AIOptr1_2=NULL;
		}

		if(StreamStruct->AIOptr1)
		{
			FreeMem(StreamStruct->AIOptr1,sizeof(struct IOAudio));
			StreamStruct->AIOptr1=NULL;
		}

		if(StreamStruct->FastMemBuf2)
		{
			FreeMem(StreamStruct->FastMemBuf2,StreamStruct->framesPerBuffer*Pa_GetSampleSize(StreamStruct->sampleFormat));
			StreamStruct->FastMemBuf2=NULL;
		}

		if(StreamStruct->FastMemBuf1)
		{
			FreeMem(StreamStruct->FastMemBuf1,StreamStruct->framesPerBuffer*Pa_GetSampleSize(StreamStruct->sampleFormat));
			StreamStruct->FastMemBuf1=NULL;
		}


		if(StreamStruct->ChipMemBuf2)
		{
			FreeMem(StreamStruct->ChipMemBuf2,StreamStruct->framesPerBuffer);
			StreamStruct->ChipMemBuf2=NULL;
		}

		if(StreamStruct->ChipMemBuf1)
		{
			FreeMem(StreamStruct->ChipMemBuf1,StreamStruct->framesPerBuffer);
			StreamStruct->ChipMemBuf1=NULL;
		}

		free(StreamStruct);
		GlobalPaStreamPtr=NULL;   /* in case of CTRL-C atexit()-Functions are void so a global pointer is needed */


		//		printf("    returning paNoError\n");
		return paNoError;
	}
	else
	{
		//		printf(" %s called with NULL-Ptr\n",__FUNCTION__);
		//		printf("    returning paBadStreamPtr\n");
		return paBadStreamPtr;   // <0 is error

	}

	if(File)
	{
		fclose(File);
		File=NULL;
	}

}


/* ########################################################################## */
/*
	Pa_Initialize() is the library initialisation function - call this before
	using the library.
 */


PaError Pa_Initialize( void )
{
	int Error;
	//	printf(" %s called\n",__FUNCTION__);
	//	KPrintF("%s() called\n",__FUNCTION__);

	Error=paNoError;

	return Error;
}



// ##############################################################################
// not needed by espeak but used by myself, AF 18.May2017

/*
	Return size in bytes of a single sample in a given PaSampleFormat
	or paSampleFormatNotSupported.
 */
PaError Pa_GetSampleSize( PaSampleFormat format )
{
	//	KPrintF("%s() called\n",__FUNCTION__);
	switch (format)
	{
	case paFloat32     : return 4;	/*always available*/
	case paInt16       : return 2;	/*always available*/
	case paInt32       : return 4;	/*always available*/
	case paInt24       : return 6;
	case paPackedInt24 : return 6;
	case paInt8        : return 1;
	case paUInt8       : return 1;  /* unsigned 8 bit, 128 is "ground" */
	default            : return paSampleFormatNotSupported;
	}
}





// ##############################################################################
/* reads 16Bit Signed Samples from Source and writes 8Bit Signed Smaples to Dest.  */
/* 16 Bit Samples must be BIG_ENDIAN */
/* SampleCount Samples will be read  */
/* SampleCount must be multiple of 2 */
/* Returns number of copied samples  */
/* AF, 15.76.2017                    */

UWORD Convert16SSamples(SHORT *Source16S, BYTE *Dest8S, ULONG SampleCount)
{
	UWORD i=0;
	ULONG *SourcePtr=(ULONG*)Source16S;   /* we read 2 16Bit-Samples at once */
	USHORT *DestPtr=(USHORT*)Dest8S;      /* we write 2 8Bit-Samples at once */
	ULONG SourceSamples;
	USHORT DestSamples;

	if(SampleCount%2)
	{
		printf("%s() SampleCount %lu is not even!\n",__FUNCTION__,SampleCount);
		/* we read probably one bad sample but continue anyway */
	}

	for(;i<SampleCount/2;i++)          /* two Samples at once */
	{
		SourceSamples=*SourcePtr++;
		DestSamples=((SourceSamples & 0xff000000) >>16) | ((SourceSamples &0xff00) >> 8);  /* extract the two High-Bytes */
		*DestPtr++=DestSamples;

		/*printf("SourceSamples=0x%0lx, DestSamples=0x%0hx\n",SourceSamples,DestSamples);*/
	}
	return i*2;
}



/* used for atexit(). Therefore void parameter -> global Pointer needed */
void Abort_Pa_CloseStream (void)
{
	if(GlobalPaStreamPtr)
	{
		//		KPrintF("%s() called with Stream=$%08lx\n",__FUNCTION__,GlobalPaStreamPtr);
		Pa_CloseStream(GlobalPaStreamPtr);
		GlobalPaStreamPtr=NULL;
	}
	else
	{
		//		KPrintF("%s() called but nothing to do\n",__FUNCTION__);
	}
}
