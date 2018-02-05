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

#include <devices/ahi.h>
#include <proto/ahi.h>
#include <string.h>
#include <unistd.h>    
#include "portaudio_ahidev.h"

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
static void startup() {
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
FILE *File_ahi=NULL;    

unsigned int g_AHI_Unit=0; 

UBYTE chans_ahi[] ={3,5,10,12};  

typedef struct
{
	int StreamActive;
	unsigned int AF_StreamID;   
	struct SubTask *st;
	struct SignalSemaphore sema;    
	int numInputChannels;
	int numOutputChannels;          
	PaSampleFormat sampleFormat;
	ULONG AhiSampleType;
	double sampleRate;
	unsigned long framesPerBuffer;  
	unsigned long numberOfBuffers;  
	PortAudioCallback *callback;
	void *userData;
	struct AHIRequest *AHIio;       
	struct AHIRequest *AHIio2;      
	struct AHIRequest *AIOptrStartStop;
	struct MsgPort *AHImp;
	struct MsgPort *portStartStop;
	ULONG device;                   
	BYTE *ChipMemBuf1;              
	BYTE *ChipMemBuf2;              
	SHORT *FastMemBuf1;             
	SHORT *FastMemBuf2;             
	unsigned int LeftChannel;       
	unsigned int RightChannel;      

}PortAudioStreamStruct;

static UWORD Convert16SSamples(SHORT *Source16S, BYTE *Dest8S, ULONG SampleCount);  

static ULONG getAHISampleType(PaSampleFormat format );

struct MsgPort    *AHImp     = NULL;

extern struct Device* TimerBase;           
static struct IORequest timereq;    
VOID  EspeakAudioTask_AHI(VOID)
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

			PortAudioStreamData->AHImp=CreatePort(0,0);
			if(PortAudioStreamData->AHImp)
			{

				PortAudioStreamData->portStartStop=CreatePort(0,0);
				if(PortAudioStreamData->portStartStop)
				{
					
					PortAudioStreamData->AHIio->ahir_Std.io_Message.mn_ReplyPort=PortAudioStreamData->AHImp;
					PortAudioStreamData->AHIio->ahir_Std.io_Message.mn_Length=sizeof(struct AHIRequest);
					PortAudioStreamData->AHIio->ahir_Version = 4;

					PortAudioStreamData->device=OpenDevice((CONST_STRPTR)AHINAME,g_AHI_Unit,(struct IORequest*)PortAudioStreamData->AHIio,0L);
					
					if(PortAudioStreamData->device==0)
					{
						*(PortAudioStreamData->AHIio2)=*(PortAudioStreamData->AHIio);

						PortAudioStreamData->AHIio->ahir_Std.io_Command=CMD_WRITE;

						if ((st->st_Port = CreateMsgPort()))
						{
							ULONG CallbackTime=0;       
							ULONG CallBackCount=0;
							ULONG CallBackMaxTime=0;

							ULONG BufferLength=PortAudioStreamData->framesPerBuffer * Pa_GetSampleSize_ahidev(PortAudioStreamData->sampleFormat);

							struct AHIRequest *AHIio =PortAudioStreamData->AHIio;
							struct AHIRequest *AHIio2=PortAudioStreamData->AHIio2;
							struct AHIRequest *tmpReq=NULL;
							struct AHIRequest *link = NULL;
							WORD *tmpBuf=NULL;
							WORD *p1=PortAudioStreamData->FastMemBuf1;
							WORD *p2=PortAudioStreamData->FastMemBuf2;

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
										break;
									case STC_STOP:
										
										worktodo = FALSE;
										break;
									}

									if (!running) break;

									ReplyMsg((struct Message *)stm);
								}
								if (!running) break;

								if (worktodo)
								{
									int LastBuf;
									ULONG signals;
									if( 0!=PortAudioStreamData->callback(NULL,p1,PortAudioStreamData->framesPerBuffer,NULL,NULL))  
									{
										PortAudioStreamData->StreamActive=0;
										worktodo=FALSE;
										LastBuf=TRUE;
									}
									else
									{
										LastBuf=FALSE;
									}
									AHIio->ahir_Std.io_Message.mn_Node.ln_Pri = 75;   
									AHIio->ahir_Std.io_Command  = CMD_WRITE;
									AHIio->ahir_Std.io_Data     = p1;
									AHIio->ahir_Std.io_Length   = BufferLength;
									AHIio->ahir_Std.io_Offset   = 0;
									AHIio->ahir_Frequency       = (ULONG)PortAudioStreamData->sampleRate;
									AHIio->ahir_Type            = PortAudioStreamData->AhiSampleType;
									AHIio->ahir_Volume          = 0x10000;          
									AHIio->ahir_Position        = 0x8000;           
									AHIio->ahir_Link            = link;
									SendIO((struct IORequest *) AHIio);

									if(link) {

										signals=Wait(SIGBREAKF_CTRL_C | (1L << PortAudioStreamData->AHImp->mp_SigBit));
#ifdef ALEXANDER
										if(signals & SIGBREAKF_CTRL_C) {
											SetIoErr(ERROR_BREAK);
											break;
										}
#endif

										if(WaitIO((struct IORequest *) link)) {
											SetIoErr(ERROR_WRITE_PROTECTED);
											worktodo=FALSE;   

										}
									}
									if(LastBuf) {
										WaitIO((struct IORequest *) AHIio);
										worktodo=FALSE;   
									}
									link = AHIio;

									tmpBuf = p1;
									p1     = p2;
									p2     = tmpBuf;

									tmpReq    = AHIio;
									AHIio  = AHIio2;
									AHIio2 = tmpReq;
									
								}
								else
								{
									
									WaitPort(st->st_Port);
								}
							}

							Error=paNoError;
							
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
						CloseDevice((struct IORequest*)PortAudioStreamData->AHIio);
						PortAudioStreamData->device=1;
					}
					else
					{
						printf("    Could not open " AHINAME " version 4\n");
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
				DeletePort(PortAudioStreamData->AHImp);
				PortAudioStreamData->AHImp=NULL;
			}
			else
			{
				printf("    Could not create AHImp\n");
				Error=paInsufficientMemory;
			}
			CloseDevice(&timereq);
		}
		else
		{
			printf("    Could not open " TIMERNAME"\n");
		}
		ExitSubTask(st,stm);
	}
}

PortAudioStream *GlobalPaStreamPtr_ahi=NULL;  

PaError Pa_StartStream_ahidev( PortAudioStream *stream )
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
PaError Pa_AbortStream_ahidev( PortAudioStream *stream )
{
	return paNoError;
}
PaError Pa_StreamActive_ahidev( PortAudioStream *stream )
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
PaError Pa_OpenDefaultStream_ahidev( PortAudioStream** stream,
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
		StreamStruct->AhiSampleType=getAHISampleType(sampleFormat);

		*stream=StreamStruct;

		GlobalPaStreamPtr_ahi=StreamStruct;   

		StreamStruct->ChipMemBuf1=(BYTE*)AllocMem(StreamStruct->framesPerBuffer,MEMF_CHIP);
		if(StreamStruct->ChipMemBuf1)
		{
			
			StreamStruct->ChipMemBuf2=(BYTE*)AllocMem(StreamStruct->framesPerBuffer,MEMF_CHIP);
			if(StreamStruct->ChipMemBuf2)
			{
				
				StreamStruct->FastMemBuf1=(SHORT*)AllocMem(StreamStruct->framesPerBuffer*Pa_GetSampleSize(StreamStruct->sampleFormat),MEMF_PUBLIC);
				
				if(StreamStruct->FastMemBuf1)
				{
					StreamStruct->FastMemBuf2=(SHORT*)AllocMem(StreamStruct->framesPerBuffer*Pa_GetSampleSize(StreamStruct->sampleFormat),MEMF_PUBLIC);
					if(StreamStruct->FastMemBuf2)
					{
						
						StreamStruct->AHIio=(struct AHIRequest*)AllocMem(sizeof(struct AHIRequest),MEMF_PUBLIC|MEMF_CLEAR);
						if(StreamStruct->AHIio)
						{
							
							StreamStruct->AHIio2=(struct AHIRequest*)AllocMem(sizeof(struct AHIRequest),MEMF_PUBLIC|MEMF_CLEAR);
							if(StreamStruct->AHIio2)
							{
								
								StreamStruct->AIOptrStartStop=(struct AHIRequest*)AllocMem(sizeof(struct AHIRequest),MEMF_PUBLIC|MEMF_CLEAR);
								if(StreamStruct->AIOptrStartStop)
								{
									
									StreamStruct->st = SpawnSubTask("EspeakAudioTask_AHI",EspeakAudioTask_AHI,StreamStruct);
									if (StreamStruct->st)
									{
										
										Error=paNoError;
										return Error;
									}
									Error=paInternalError;
								}
								else
								{
									printf("    not enough memory for allocating  AIOptrStartStop\n");
									Error=paInsufficientMemory;
								}
								FreeMem(StreamStruct->AHIio2,sizeof(struct AHIRequest));
								StreamStruct->AHIio2=NULL;
							}
							else
							{
								printf("    not enough memory for allocating  AHIio2\n");
								Error=paInsufficientMemory;
							}
							FreeMem(StreamStruct->AHIio,sizeof(struct AHIRequest));
							StreamStruct->AHIio=NULL;
						}
						else
						{
							printf("    not enough memory for allocating  AHIio\n");
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
		GlobalPaStreamPtr_ahi=NULL;   
	}
	else
	{
		printf("    not enough memory for allocating stream %u\n",StreamNr);
		Error=paInsufficientMemory;
	}
	return Error;
}
PaError Pa_CloseStream_ahidev( PortAudioStream *stream )
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
			CloseDevice((struct IORequest*)StreamStruct->AHIio);
			StreamStruct->device=1;
		}

		if(StreamStruct->portStartStop)
		{
			DeletePort(StreamStruct->portStartStop);
			StreamStruct->portStartStop=0;
		}

		if(StreamStruct->AHImp)
		{
			DeletePort(StreamStruct->AHImp);
			StreamStruct->AHImp=0;
		}

		if(StreamStruct->AIOptrStartStop)
		{
			FreeMem(StreamStruct->AIOptrStartStop,sizeof(struct AHIRequest));
			StreamStruct->AIOptrStartStop=NULL;
		}

		if(StreamStruct->AHIio2)
		{
			FreeMem(StreamStruct->AHIio2,sizeof(struct AHIRequest));
			StreamStruct->AHIio2=NULL;
		}

		if(StreamStruct->AHIio)
		{
			FreeMem(StreamStruct->AHIio,sizeof(struct AHIRequest));
			StreamStruct->AHIio=NULL;
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
		GlobalPaStreamPtr_ahi=NULL;   
		return paNoError;
	}
	else
	{
		return paBadStreamPtr;   

	}

	if(File_ahi)
	{
		fclose(File_ahi);
		File_ahi=NULL;
	}

}

PaError Pa_Initialize_ahidev( void )
{
	int Error;
	
	Error=paNoError;

	return Error;
}
PaError Pa_GetSampleSize_ahidev( PaSampleFormat format )
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
static ULONG getAHISampleType(PaSampleFormat format )
{
	switch (format)
	{
	case paInt16       : return AHIST_M16S;	                     
	case paInt32       : return AHIST_M32S;                      
	case paInt8        : return AHIST_M8S;             
	case paUInt8       : return AHIST_M8U;               
	default            : return AHIE_BADSAMPLETYPE;    ;
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

void Abort_Pa_CloseStream_ahidev (void)
{
	if(GlobalPaStreamPtr_ahi)
	{
		Pa_CloseStream(GlobalPaStreamPtr_ahi);
		GlobalPaStreamPtr_ahi=NULL;
	}
	else
	{
		
	}
}
