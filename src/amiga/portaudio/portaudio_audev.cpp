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

#include <exec/types.h>
#include "subtask_support.h"
#include <proto/dos.h>

#include <clib/exec_protos.h>
#include <graphics/gfxbase.h>
#include <clib/alib_protos.h>  
#include <devices/audio.h>
#include <string.h>
#include <unistd.h>    
#include "portaudio_audev.h"

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

extern unsigned int global_bufsize_factor;   
extern unsigned int global_benchmark_flag;   
extern char *global_ProgramName;             

static ULONG getMilliseconds() {
	struct timeval endTime;
	GetSysTime(&endTime);
	SubTime(&endTime, &startTime);
	return (endTime.tv_secs * 1000 + endTime.tv_micro / 1000);
}
FILE *File=NULL;    

UBYTE chans[] ={3,5,10,12};  

typedef struct
{
	int StreamActive;
	unsigned int AF_StreamID;   
	struct SubTask *st;
	struct SignalSemaphore sema;    
	int numInputChannels;
	int numOutputChannels;          
	PaSampleFormat sampleFormat;
	double sampleRate;
	unsigned long framesPerBuffer;  
	unsigned long numberOfBuffers;  
	PortAudioCallback *callback;
	void *userData;
	unsigned int clock;             
	unsigned long speed;            
	struct IOAudio *AIOptr1;        
	struct IOAudio *AIOptr1_2;      
	struct IOAudio *AIOptr2;
	struct IOAudio *AIOptr2_2;      
	struct IOAudio *AIOptrStartStop;
	struct MsgPort *port1;
	struct MsgPort *port1_2;
	struct MsgPort *port2;
	struct MsgPort *port2_2;
	struct MsgPort *portStartStop;
	ULONG device;                   
	BYTE *ChipMemBuf1;              
	BYTE *ChipMemBuf2;              
	SHORT *FastMemBuf1;             
	SHORT *FastMemBuf2;             
	unsigned int LeftChannel;       
	unsigned int RightChannel;      

}PortAudioStreamStruct;

void SetAudioUnit(struct IOAudio *AIOptr,unsigned int Channel)
{
	ULONG Temp;
	Temp= (ULONG) AIOptr->ioa_Request.io_Unit;
	Temp&=0xf0;
	Temp|=Channel;
	AIOptr->ioa_Request.io_Unit =(struct Unit*)Temp;

}

static UWORD Convert16SSamples(SHORT *Source16S, BYTE *Dest8S, ULONG SampleCount);  

struct Device* TimerBase;           
static struct IORequest timereq;    
VOID  EspeakAudioTask(VOID)
{
	struct Task *me = FindTask(NULL);
	struct SubTask *st;
	struct SubTaskMsg *stm;

	WaitPort(&((struct Process *)me)->pr_MsgPort);
	stm  = (struct SubTaskMsg *)GetMsg(&((struct Process *)me)->pr_MsgPort);
	st   = (struct SubTask *)stm->stm_Parameter;

	{
		
		BOOL running = TRUE;
		BOOL worktodo = FALSE;
		PortAudioStreamStruct *PortAudioStreamData=(PortAudioStreamStruct*)(st->st_Data);
		int Error=paInternalError;
		if(0==OpenDevice((CONST_STRPTR)TIMERNAME, UNIT_MICROHZ, &timereq, 0))   
		{
			TimerBase = timereq.io_Device;	            

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

								PortAudioStreamData->AIOptr1->ioa_Request.io_Message.mn_ReplyPort=PortAudioStreamData->port1;
								PortAudioStreamData->AIOptr1->ioa_Request.io_Message.mn_Node.ln_Pri=127; 
								PortAudioStreamData->AIOptr1->ioa_Request.io_Message.mn_Length=sizeof(struct IOAudio);

								PortAudioStreamData->AIOptr1->ioa_AllocKey=0;
								PortAudioStreamData->AIOptr1->ioa_Data=chans;
								PortAudioStreamData->AIOptr1->ioa_Length=sizeof(chans);

								PortAudioStreamData->device=OpenDevice((CONST_STRPTR)AUDIONAME,0L,(struct IORequest*)PortAudioStreamData->AIOptr1,0L);

								if(PortAudioStreamData->device==0)
								{
									UBYTE GotChannels;

									GotChannels=((ULONG)PortAudioStreamData->AIOptr1->ioa_Request.io_Unit) & 0x0f; 
									if(GotChannels & 0x02) PortAudioStreamData->LeftChannel=0x02;
									else if(GotChannels & 0x04) PortAudioStreamData->LeftChannel=0x04;
									else PortAudioStreamData->LeftChannel=0;
									if(GotChannels & 0x01) PortAudioStreamData->RightChannel=0x01;
									else if(GotChannels & 0x08) PortAudioStreamData->RightChannel=0x08;
									else PortAudioStreamData->RightChannel=0;

									PortAudioStreamData->AIOptr1->ioa_Request.io_Command=CMD_WRITE;
									PortAudioStreamData->AIOptr1->ioa_Request.io_Flags=ADIOF_PERVOL;
									PortAudioStreamData->AIOptr1->ioa_Volume=64; 

									PortAudioStreamData->AIOptr1->ioa_Period=(UWORD)PortAudioStreamData->speed;
									PortAudioStreamData->AIOptr1->ioa_Cycles=1;
									*(PortAudioStreamData->AIOptr1_2)=*(PortAudioStreamData->AIOptr1);       
									*(PortAudioStreamData->AIOptr2)  =*(PortAudioStreamData->AIOptr1);       
									*(PortAudioStreamData->AIOptr2_2)=*(PortAudioStreamData->AIOptr1);       
									*(PortAudioStreamData->AIOptrStartStop)=*(PortAudioStreamData->AIOptr1); 
									PortAudioStreamData->AIOptr1  ->ioa_Request.io_Message.mn_ReplyPort=PortAudioStreamData->port1;  
									PortAudioStreamData->AIOptr1_2->ioa_Request.io_Message.mn_ReplyPort=PortAudioStreamData->port1_2;
									PortAudioStreamData->AIOptr2  ->ioa_Request.io_Message.mn_ReplyPort=PortAudioStreamData->port2;
									PortAudioStreamData->AIOptr2_2->ioa_Request.io_Message.mn_ReplyPort=PortAudioStreamData->port2_2;
									PortAudioStreamData->AIOptrStartStop->ioa_Request.io_Message.mn_ReplyPort=PortAudioStreamData->portStartStop;
									
									SetAudioUnit(PortAudioStreamData->AIOptr1,   PortAudioStreamData->LeftChannel);  
									SetAudioUnit(PortAudioStreamData->AIOptr1_2, PortAudioStreamData->RightChannel); 
									SetAudioUnit(PortAudioStreamData->AIOptr2,   PortAudioStreamData->LeftChannel);  
									SetAudioUnit(PortAudioStreamData->AIOptr2_2, PortAudioStreamData->RightChannel); 
									if ((st->st_Port = CreateMsgPort()))
									{
										struct IOAudio *Aptr=NULL;  
										struct MsgPort *port=NULL;  
										struct MsgPort *port_2=NULL; 
										BYTE *ChipMemBuf=NULL;      
										SHORT *FastMemBuf=NULL;     

										ULONG CallbackTime=0;       
										ULONG CallBackCount=0;
										ULONG CallBackMaxTime=0;

										stm->stm_Result = TRUE;
										ReplyMsg((struct Message *)stm);
										for (;;)
										{
											while ((stm = (struct SubTaskMsg *)GetMsg(st->st_Port)))
											{
												switch (stm->stm_Command)
												{
												case STC_SHUTDOWN:
													
													running = FALSE;
													break;
												case STC_START:
													
													worktodo = TRUE;

													PortAudioStreamData->callback(NULL,PortAudioStreamData->FastMemBuf1,PortAudioStreamData->framesPerBuffer,NULL,NULL);

													Convert16SSamples(PortAudioStreamData->FastMemBuf1, PortAudioStreamData->ChipMemBuf1 ,PortAudioStreamData->framesPerBuffer);
													PortAudioStreamData->AIOptr1->ioa_Length=PortAudioStreamData->framesPerBuffer;
													PortAudioStreamData->AIOptr1->ioa_Data=(UBYTE*)PortAudioStreamData->ChipMemBuf1;

													PortAudioStreamData->AIOptr1_2->ioa_Length=PortAudioStreamData->framesPerBuffer;      
													PortAudioStreamData->AIOptr1_2->ioa_Data=(UBYTE*)PortAudioStreamData->ChipMemBuf1;    
													PortAudioStreamData->callback(NULL,PortAudioStreamData->FastMemBuf2,PortAudioStreamData->framesPerBuffer,NULL,NULL);
													Convert16SSamples(PortAudioStreamData->FastMemBuf2, PortAudioStreamData->ChipMemBuf2 ,PortAudioStreamData->framesPerBuffer);
													PortAudioStreamData->AIOptr2->ioa_Length=PortAudioStreamData->framesPerBuffer;
													PortAudioStreamData->AIOptr2->ioa_Data=(UBYTE*)PortAudioStreamData->ChipMemBuf2;
													PortAudioStreamData->AIOptr2_2->ioa_Length=PortAudioStreamData->framesPerBuffer;      
													PortAudioStreamData->AIOptr2_2->ioa_Data=(UBYTE*)PortAudioStreamData->ChipMemBuf2;    
													PortAudioStreamData->AIOptrStartStop->ioa_Request.io_Command=CMD_STOP;
													DoIO((struct IORequest*)PortAudioStreamData->AIOptrStartStop);                                
													BeginIO((struct IORequest*)PortAudioStreamData->AIOptr1);
													BeginIO((struct IORequest*)PortAudioStreamData->AIOptr1_2);
													BeginIO((struct IORequest*)PortAudioStreamData->AIOptr2);
													BeginIO((struct IORequest*)PortAudioStreamData->AIOptr2_2);
													PortAudioStreamData->AIOptrStartStop->ioa_Request.io_Command=CMD_START;
													DoIO((struct IORequest*)PortAudioStreamData->AIOptrStartStop);                                
													Aptr=PortAudioStreamData->AIOptr1;
													port=PortAudioStreamData->port1;
													port_2=PortAudioStreamData->port1_2;
													FastMemBuf=PortAudioStreamData->FastMemBuf1;
													ChipMemBuf=PortAudioStreamData->ChipMemBuf1;
													break;
												case STC_STOP:
													
													worktodo = FALSE;
													break;
												}

												if (!running) break;

												ReplyMsg((struct Message *)stm);
											}
											if (!running)
											{
												break; 
											}
											if (worktodo)
											{
												struct Message *msg;
												ULONG wakebit;
												wakebit=Wait(1 << port->mp_SigBit);
												while((msg=GetMsg(port))==0)
												{
												};
												wakebit=Wait(1 << port_2->mp_SigBit);
												while((msg=GetMsg(port_2))==0)  
												{
												};
												if(global_benchmark_flag)
												{
													startup();     
												}
												if( 0!=PortAudioStreamData->callback(NULL,FastMemBuf,PortAudioStreamData->framesPerBuffer,NULL,NULL))  
												{
													PortAudioStreamData->StreamActive=0;
													worktodo=FALSE;
													Convert16SSamples(FastMemBuf, ChipMemBuf ,PortAudioStreamData->framesPerBuffer);
													BeginIO((struct IORequest*)Aptr);
													BeginIO((Aptr==PortAudioStreamData->AIOptr1)?(struct IORequest*)PortAudioStreamData->AIOptr1_2:(struct IORequest*)PortAudioStreamData->AIOptr2_2); 
													
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
													wakebit=Wait(1 << port->mp_SigBit);  
													while((msg=GetMsg(port))==0) {};
													wakebit=Wait(1 << port_2->mp_SigBit);  
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
													wakebit=Wait(1 << port->mp_SigBit);  
													while((msg=GetMsg(port))==0) {};
													wakebit=Wait(1 << port_2->mp_SigBit);  
													while((msg=GetMsg(port_2))==0) {};
												}
												else 
												{
													if(global_benchmark_flag)
													{
														unsigned long Time=getMilliseconds();  
														if(Time>CallBackMaxTime)
														{
															CallBackMaxTime=Time;
														}
														CallbackTime+=Time;
														CallBackCount+=1;
													}
													
													Convert16SSamples(FastMemBuf, ChipMemBuf ,PortAudioStreamData->framesPerBuffer);
													
													BeginIO((struct IORequest*)Aptr);
													BeginIO((Aptr==PortAudioStreamData->AIOptr1)?(struct IORequest*)PortAudioStreamData->AIOptr1_2:(struct IORequest*)PortAudioStreamData->AIOptr2_2); 
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

											}
											else
											{
												
												WaitPort(st->st_Port);
											}
										}

										Error=paNoError;
										
										PortAudioStreamData->AIOptr1->ioa_Request.io_Command=ADCMD_FINISH;
										BeginIO((struct IORequest*)PortAudioStreamData->AIOptr1);
										PortAudioStreamData->AIOptr2->ioa_Request.io_Command=ADCMD_FINISH;
										BeginIO((struct IORequest*)PortAudioStreamData->AIOptr2);
										if(global_benchmark_flag)   
										{
											printf("%s, Average BufferTime %lums, Max Buffertime %lums, (Buffer is %lums), Compiled for " __CPU__ " " __FPU__ "\n",global_ProgramName,CallbackTime/CallBackCount,CallBackMaxTime,(ULONG)(global_bufsize_factor*512*1000/PortAudioStreamData->sampleRate));
										}
										DeletePort(st->st_Port);
										st->st_Port=NULL;
									}
									else  
									{
										printf("    Subtask CreateMsgPort() failed\n");
										Error=paInsufficientMemory;
									}
									CloseDevice((struct IORequest*)PortAudioStreamData->AIOptr1);
									PortAudioStreamData->device=1;
								}
								else
								{
									printf("    Could not open " AUDIONAME "\n");
									Error=paDeviceUnavailable;
								}
								DeletePort(PortAudioStreamData->portStartStop);
								PortAudioStreamData->portStartStop=NULL;
							}
							else
							{
								printf("    Could not create portStartStop\n");
								Error=paInsufficientMemory;
							}
							DeletePort(PortAudioStreamData->port2_2);
							PortAudioStreamData->port2_2=NULL;
						}
						else
						{
							printf("    Could not create port2_2\n");
							Error=paInsufficientMemory;
						}
						DeletePort(PortAudioStreamData->port2);
						PortAudioStreamData->port2=NULL;
					}
					else
					{
						printf("    Could not create port2\n");
						Error=paInsufficientMemory;
					}
					DeletePort(PortAudioStreamData->port1_2);
					PortAudioStreamData->port1_2=NULL;
				}
				else
				{
					printf("    Could not create port1_2\n");
					Error=paInsufficientMemory;
				}
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
			printf("    Could not open " TIMERNAME "\n");
		}
		ExitSubTask(st,stm);
	}
}

PortAudioStream *GlobalPaStreamPtr=NULL;  

PaError Pa_StartStream_audev( PortAudioStream *stream )
{
	int Error;
	
	if(stream)
	{
		((PortAudioStreamStruct*)stream)->StreamActive=1;
		SendSubTaskMsg(((PortAudioStreamStruct*)stream)->st,STC_START,NULL);
		Error=paNoError;
	}
	else
	{
		Error=paBadStreamPtr;

	}

	return Error;
}
PaError Pa_AbortStream_audev( PortAudioStream *stream )
{
	((PortAudioStreamStruct*)stream)->StreamActive=0;
	return paNoError;
}

PaError Pa_StreamActive_audev( PortAudioStream *stream )
{
	if(stream)
	{
		return ((PortAudioStreamStruct*)stream)->StreamActive;   
	}
	else
	{
		return paBadStreamPtr;   

	}
}
PaError Pa_OpenDefaultStream_audev( PortAudioStream** stream,
		int numInputChannels,
		int numOutputChannels,
		PaSampleFormat sampleFormat,
		double sampleRate,
		unsigned long framesPerBuffer,
		unsigned long numberOfBuffers,
		PortAudioCallback *callback,
		void *userData )
{
	static unsigned int StreamNr=1;
	PortAudioStreamStruct* StreamStruct;
	int Error=paInternalError;
	struct GfxBase *GfxBase;
	unsigned int clock;

	GfxBase = (struct GfxBase *)OpenLibrary((CONST_STRPTR)"graphics.library",0L);
	if (GfxBase)
	{
		if (GfxBase->DisplayFlags & PAL)
		{
			clock = 3546895;        
		}
		else
		{
			clock = 3579545;        
		}
		CloseLibrary((struct Library *) GfxBase);
		StreamStruct=(PortAudioStreamStruct*)malloc(sizeof(PortAudioStreamStruct));
		if(StreamStruct)
		{
			memset(StreamStruct,0,sizeof(PortAudioStreamStruct));  
			StreamStruct->device=1;                                

			StreamStruct->AF_StreamID=StreamNr;
			StreamNr++;
			StreamStruct->callback=callback;  
			StreamStruct->framesPerBuffer=framesPerBuffer*global_bufsize_factor;   
			StreamStruct->numInputChannels=numInputChannels;
			StreamStruct->numOutputChannels=numOutputChannels;
			StreamStruct->sampleFormat=sampleFormat;
			StreamStruct->sampleRate=sampleRate;
			StreamStruct->clock=clock;

			StreamStruct->speed =  (ULONG) (clock / sampleRate);

			*stream=StreamStruct;

			GlobalPaStreamPtr=StreamStruct;   

			StreamStruct->ChipMemBuf1=(BYTE*)AllocMem(StreamStruct->framesPerBuffer,MEMF_CHIP);
			if(StreamStruct->ChipMemBuf1)
			{
				StreamStruct->ChipMemBuf2=(BYTE*)AllocMem(StreamStruct->framesPerBuffer,MEMF_CHIP);
				if(StreamStruct->ChipMemBuf2)
				{
					StreamStruct->FastMemBuf1=(SHORT*)AllocMem(StreamStruct->framesPerBuffer*Pa_GetSampleSize_audev(StreamStruct->sampleFormat),MEMF_PUBLIC);
					
					if(StreamStruct->FastMemBuf1)
					{
						StreamStruct->FastMemBuf2=(SHORT*)AllocMem(StreamStruct->framesPerBuffer*Pa_GetSampleSize_audev(StreamStruct->sampleFormat),MEMF_PUBLIC);
						if(StreamStruct->FastMemBuf2)
						{
							
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
							FreeMem(StreamStruct->FastMemBuf2,StreamStruct->framesPerBuffer*Pa_GetSampleSize_audev(StreamStruct->sampleFormat));
							StreamStruct->FastMemBuf2=NULL;
						}
						else
						{
							printf("    Could not allocate FastMemBuf2 (%lu Bytes)\n",StreamStruct->framesPerBuffer*Pa_GetSampleSize_audev(StreamStruct->sampleFormat));
							Error=paInsufficientMemory;
						}
						FreeMem(StreamStruct->FastMemBuf1,StreamStruct->framesPerBuffer*Pa_GetSampleSize_audev(StreamStruct->sampleFormat));
						StreamStruct->FastMemBuf1=NULL;
					}
					else
					{
						printf("    Could not allocate FastMemBuf1 (%lu Bytes)\n",StreamStruct->framesPerBuffer*Pa_GetSampleSize_audev(StreamStruct->sampleFormat));
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
			GlobalPaStreamPtr=NULL;   
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
	
	return Error;
}
PaError Pa_CloseStream_audev( PortAudioStream *stream )
{
	PortAudioStreamStruct *StreamStruct=(PortAudioStreamStruct*)stream;

	if(stream)
	{
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
			FreeMem(StreamStruct->FastMemBuf2,StreamStruct->framesPerBuffer*Pa_GetSampleSize_audev(StreamStruct->sampleFormat));
			StreamStruct->FastMemBuf2=NULL;
		}

		if(StreamStruct->FastMemBuf1)
		{
			FreeMem(StreamStruct->FastMemBuf1,StreamStruct->framesPerBuffer*Pa_GetSampleSize_audev(StreamStruct->sampleFormat));
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
		GlobalPaStreamPtr=NULL;   
		return paNoError;
	}
	else
	{
		return paBadStreamPtr;   

	}

	if(File)
	{
		fclose(File);
		File=NULL;
	}

}

PaError Pa_Initialize_audev( void )
{
	int Error;
	
	Error=paNoError;

	return Error;
}
PaError Pa_GetSampleSize_audev( PaSampleFormat format )
{
	switch (format)
	{
	case paFloat32     : return 4;	
	case paInt16       : return 2;	
	case paInt32       : return 4;	
	case paInt24       : return 6;
	case paPackedInt24 : return 6;
	case paInt8        : return 1;
	case paUInt8       : return 1;  
	default            : return paSampleFormatNotSupported;
	}
}
static UWORD Convert16SSamples(SHORT *Source16S, BYTE *Dest8S, ULONG SampleCount)
{
	UWORD i=0;
	ULONG *SourcePtr=(ULONG*)Source16S;   
	USHORT *DestPtr=(USHORT*)Dest8S;      
	ULONG SourceSamples;
	USHORT DestSamples;

	if(SampleCount%2)
	{
		printf("%s() SampleCount %lu is not even!\n",__FUNCTION__,SampleCount);
		
	}

	for(;i<SampleCount/2;i++)          
	{
		SourceSamples=*SourcePtr++;
		DestSamples=((SourceSamples & 0xff000000) >>16) | ((SourceSamples &0xff00) >> 8);  
		*DestPtr++=DestSamples;

	}
	return i*2;
}

void Abort_Pa_CloseStream_audev (void)
{
	if(GlobalPaStreamPtr)
	{
		Pa_CloseStream_audev(GlobalPaStreamPtr);
		GlobalPaStreamPtr=NULL;
	}
	else
	{
		
	}
}
