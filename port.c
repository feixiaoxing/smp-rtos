/*
     raw os - Copyright (C)  Lingjun Chen(jorya_txj).

    This file is part of raw os.

    raw os is free software; you can redistribute it it under the terms of the 
    GNU General Public License as published by the Free Software Foundation; 
    either version 3 of the License, or  (at your option) any later version.

    raw os is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; 
    without even the implied warranty of  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  
    See the GNU General Public License for more details.

    You should have received a copy of the GNU Lesser General Public License
    along with this program. if not, write email to jorya.txj@gmail.com
                                      ---

    A special exception to the LGPL can be applied should you wish to distribute
    a combined work that includes raw os, without being obliged to provide
    the source code for any proprietary components. See the file exception.txt
    for full details of how and when the exception can be applied.
*/


/* 	2012-4  Created by jorya_txj
  *	xxxxxx   please added here
  */

#include "os.h"

#include    <stdio.h>
#include    <string.h>
#include    <ctype.h>
#include    <stdlib.h>

#include  	<stdio.h>
#include  	<string.h>
#include  	<ctype.h>
#include  	<stdlib.h>
#include	<conio.h>
#include 	<stdarg.h>
#include	<windows.h>
#include	<mmsystem.h>
#include  	<assert.h> 




#define  WINDOWS_ASSERT(CON)    if (!(CON)) { \
			printf("If you see this error, please contact author txj, thanks\n");\
			assert(0);\
	}


static void master_core_interrupt_process( void );
static void sub_cores_interrupt_process( void );

void port_enter_critical();
void port_exit_critical();

/*-----------------------------------------------------------*/

/* The WIN32 simulator runs each task in a thread.  The context switching is
managed by the threads, so the task stack does not have to be managed directly,
although the task stack is still used to hold an xThreadState structure this is
the only thing it will ever hold.  The structure indirectly maps the task handle 
to a thread handle. */
typedef struct
{
	/* Handle of the thread that executes the task. */
	void *pvThread;
	HANDLE hInitEvent;
	HANDLE hSigEvent;
	void (*func)(void*);
	void* param;
	u32 state;

} xThreadState;

#define CREATED 0x1
#define NOT_CREATED 0x2

/* An event used to inform the simulated interrupt processing thread (a high 
priority thread that simulated interrupt processing) that an interrupt is
pending. */
static void* timer_ipi_event = NULL;
static void* ipi_event[MAX_CPUS] = {0};

/* Mutex used to protect all the simulated interrupt variables that are accessed 
by multiple threads. */
static void *cpu_global_interrupt_mask[MAX_CPUS] = {0};


int ipi_switch_flag[MAX_CPUS] = {0};

static SpinLock g_print_lock = {0};

void vc_port_printf(char*   f,   ...)
{
	va_list   args;
	
	DISABLE_IE();
	spin_lock(&g_print_lock);
	
	va_start(args, f);
	vprintf(f,args);  
	va_end(args);
	
	spin_unlock(&g_print_lock);
	ENABLE_IE();

}


u8 get_local_cpu();

static void normal_entry(void* param) {

	Task* p_task = (Task*) param;

	xThreadState* pxThreadState = (xThreadState*) p_task-> stack_base;

	WaitForSingleObject(pxThreadState-> hSigEvent, INFINITE);

	SetEvent(pxThreadState-> hInitEvent);

	pxThreadState->func(pxThreadState-> param);

}

void  *port_stack_init(Task* p_task, u32  *p_stk_base, u32 stk_size,  void   *p_arg, void* p_func)
{
    
	u8 cpuid = get_local_cpu();

	xThreadState *pxThreadState = NULL;

	/* In this simulated case a stack is not initialised, but instead a thread
	is created that will execute the task being created.  The thread handles
	the context switching itself.  The xThreadState object is placed onto
	the stack that was created for the task - so the stack buffer is still
	used, just not in the conventional way.  It will not be used for anything
	other than holding this structure. */
	pxThreadState = ( xThreadState * ) (p_stk_base + stk_size - 2 - sizeof(xThreadState)/4 );

	/* Create the thread itself. */
	pxThreadState->pvThread = CreateThread( NULL, 0, ( LPTHREAD_START_ROUTINE ) normal_entry, p_task, CREATE_SUSPENDED, NULL );

	pxThreadState-> func = p_func;
	pxThreadState-> param = p_arg;
	
	pxThreadState-> hInitEvent = CreateEvent(NULL, FALSE, FALSE, NULL);
	pxThreadState-> hSigEvent = CreateEvent(NULL, FALSE, FALSE, NULL);
	pxThreadState-> state = CREATED;

	SetThreadAffinityMask( pxThreadState->pvThread, 0x1 << cpuid );
	SetThreadPriority( pxThreadState->pvThread, THREAD_PRIORITY_IDLE );
	SetThreadPriorityBoost( pxThreadState->pvThread, TRUE );

	return pxThreadState;
	
}



static unsigned int vc_timer_value = 10;

void start_vc_timer(int tick_ms)
{

	vc_timer_value = tick_ms;
}



static volatile u8 done_timer_init;


static void CALLBACK os_timer_job(unsigned int a,unsigned int b,unsigned long c,unsigned long d,unsigned long e)
{	
	
	ReleaseSemaphore(timer_ipi_event, 1, 0);

}


static void start_internal_timer(int tick_ms) 
{	
	done_timer_init = 0;
	timeSetEvent(tick_ms, 0, os_timer_job, 0, TIME_PERIODIC);
	done_timer_init = 1;	
}


typedef  void  (*SIMULTED_INTERRUPT_TYPE)();


SIMULTED_INTERRUPT_TYPE simulated_zero_fun;
SIMULTED_INTERRUPT_TYPE simulated_interrupt_fun;

extern Task* current_task[];
extern Task* sched_task[];

void raw_start_first_task(void)
{
	void *pvHandle;
	xThreadState *pxThreadState;
	u8 cpuid = get_local_cpu();

	if(0 == cpuid) {

		/*Max is assumed to 3*/
		timer_ipi_event = CreateSemaphore( NULL, 0, 2, NULL );
	
		cpu_global_interrupt_mask[0] = CreateMutex(NULL, FALSE, NULL);
	
		if( ( cpu_global_interrupt_mask[0] == NULL ) || ( timer_ipi_event == NULL ) ) {
	
			WINDOWS_ASSERT(0);
		}

		start_internal_timer(vc_timer_value);

	} else {

		ipi_event[cpuid] = CreateSemaphore( NULL, 0, 1, NULL );

		cpu_global_interrupt_mask[cpuid] = CreateMutex(NULL, FALSE, NULL);

		if( ( cpu_global_interrupt_mask[cpuid] == NULL ) || ( ipi_event[cpuid] == NULL ) ) {

			WINDOWS_ASSERT(0);
		}
	}

	
	pxThreadState = ( xThreadState * ) *( ( unsigned long * ) current_task[cpuid] );

	/* Bump up the priority of the thread that is going to run, in the
	hope that this will asist in getting the Windows thread scheduler to
	behave as an embedded engineer might expect. */

	pxThreadState-> state = NOT_CREATED;
	ResumeThread(pxThreadState-> pvThread);
	SignalObjectAndWait(pxThreadState-> hSigEvent, pxThreadState-> hInitEvent, INFINITE, FALSE);

	/* Handle all simulated interrupts - including yield requests and 
	simulated ticks. */

	if(0 == cpuid) {

		master_core_interrupt_process();
	} else {

		sub_cores_interrupt_process();
	}
		
}



extern u32 idle_tick_start;
extern u32 g_irq[];

static void master_core_interrupt_process( void )
{
	DWORD ret = 0xffffffff;
	BOOL end_ret = 0;
	
	xThreadState *pxThreadState;

	void* pvObjectList[2];

	pvObjectList[0] = timer_ipi_event;
	pvObjectList[1] = cpu_global_interrupt_mask[0];

	for(;;)
	{

		ret = WaitForMultipleObjects(sizeof(pvObjectList)/ sizeof(void*), pvObjectList, TRUE, INFINITE);
		
		if (ret == 0xffffffff) {
			
			WINDOWS_ASSERT(0);

		}

		/* must be task switch first */

		g_irq[0] ++;

		// ipi interrupt processed here

		if(ipi_switch_flag[0]) {

			ipi_switch_flag[0] = 0;
			ipi_isr_func();

		}

		timer_isr_func();
		g_irq[0] --;


		ReleaseMutex(cpu_global_interrupt_mask[0]);
	}
}


static void sub_cores_interrupt_process( void )
{

	DWORD ret = 0xffffffff;
	BOOL end_ret = 0;
	u8 cpuid = get_local_cpu();

	xThreadState *pxThreadState;

	void* pvObjectList[2];

	pvObjectList[0] = ipi_event[cpuid];
	pvObjectList[1] = cpu_global_interrupt_mask[cpuid];

	for(;;)
	{

		ret = WaitForMultipleObjects(sizeof(pvObjectList)/ sizeof(void*), pvObjectList, TRUE, INFINITE);

		if (ret == 0xffffffff) {


			WINDOWS_ASSERT(0);

		}

		g_irq[cpuid] ++;

		if(ipi_switch_flag[cpuid]) {

			ipi_switch_flag[cpuid] = 0;
			ipi_isr_func();
		}

		g_irq[cpuid] --;


		ReleaseMutex(cpu_global_interrupt_mask[cpuid]);

	}
}


void port_task_switch(void)
{
	/*global interrupt is disabled here so it is safe to change value here*/

	u8 cpuid = get_local_cpu();

	xThreadState* pxThreadState_cur = ( xThreadState * ) ( *( unsigned long *) current_task[cpuid] );

	xThreadState* pxThreadState_sched = ( xThreadState * ) ( *( unsigned long *) sched_task[cpuid] );

	current_task[cpuid] = sched_task[cpuid];

	if(pxThreadState_sched-> state == CREATED) {

		pxThreadState_sched-> state = NOT_CREATED;

		ResumeThread(pxThreadState_sched-> pvThread);

		SignalObjectAndWait(pxThreadState_sched-> hSigEvent, pxThreadState_sched-> hInitEvent, INFINITE, FALSE);

	}else {

		SetEvent(pxThreadState_sched-> hSigEvent);
	}

	port_exit_critical();

	WaitForSingleObject(pxThreadState_cur-> hSigEvent, INFINITE);

	port_enter_critical();
}


void ipi_trigger(u8 cpuid) {

	ipi_switch_flag[cpuid] = 1;

	if(0 == cpuid) {

		ReleaseSemaphore(timer_ipi_event, 1, 0);

	}else{

		ReleaseSemaphore(ipi_event[cpuid], 1, 0);

	}
}

extern u32 g_run[];

void port_enter_critical()
{
	u8 cpuid;

	cpuid = get_local_cpu();

	if (g_run[cpuid]) {
	
		/* The interrupt event mutex is held for the entire critical section,
		effectively disabling (simulated) interrupts. */
		WaitForSingleObject(cpu_global_interrupt_mask[cpuid], INFINITE);
	}
	
}


void port_exit_critical()
{
	
	/* The interrupt event mutex should already be held by this thread as it was
	obtained on entry to the critical section. */

	u8 cpuid = get_local_cpu();

	if(!g_run[cpuid]) {

		return; 
	}
	
	ReleaseMutex(cpu_global_interrupt_mask[cpuid]);

}

void cpu_init(u8 cpuid) {

	void* pvHandle;

	pvHandle = GetCurrentThread();

	if (SetThreadPriority( pvHandle, THREAD_PRIORITY_TIME_CRITICAL) == 0) {

		WINDOWS_ASSERT(0);
	}

	SetThreadPriorityBoost(pvHandle, TRUE);
	SetThreadAffinityMask( pvHandle, 0x01 << cpuid);

}

u8 get_local_cpu() {

	s32 CPUInfo[4];
	__cpuid(CPUInfo, 1);

	if ((CPUInfo[3] & (1 << 9)) == 0)
		return -1;

	return (u8)(CPUInfo[1] >> 24);
}

