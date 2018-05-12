
#ifndef _OS_H
#define _OS_H

// error number

#define SUCCESS          1
#define PARAM_ERROR      2
#define WRONG_BLOCK_TYPE 3
#define OS_SCHED_LOCKED  4
#define NOT_WAIT         5
#define NOT_MUTEX_OWNER  6
#define MSG_EXIST        7
#define MSG_FULL         8
#define TIMER_TYPE_ERR   9
#define IN_IRQ           10
#define TIMER_NOT_RUN    11
#define SELF_KILL_FORBID 12
#define NOT_VALID_CPU    13
#define TASK_BLOCKED     14

// data type definition

#define STATUS int
#define NULL 0

#define u8 unsigned char
#define s8 char
#define u16 unsigned short
#define s16 short
#define u32 unsigned int
#define s32 int
#define u64 unsigned long long
#define s64 long long

// task state

#define READY   0x1
#define RUNNING 0x2
#define BLOCKED 0x3
#define DIE     0x4

// object type

#define SEM_TYPE    0x1
#define MUT_TYPE    0x2
#define MAIL_TYPE   0x3
#define BUF_TYPE    0x4
#define EVENT_TYPE  0x5

// about ipi cmd

#define WAKE_UP_TASK 0x1

// about max cpu

#define MAX_CPUS 2

// spin lock

typedef struct _SpinLock{

	u32 lock;
}SpinLock;


#define get_lock(p_sem) (&((Sem*)(p_sem))->lock)

// link list

typedef struct _ListNode {

	struct _ListNode* prev;
	struct _ListNode* next;
}ListNode;

#define get_list_entry(node, type, member) ((type *)((u8 *)(node) - (u32)(&((type *)0)->member)))


// task struct

typedef struct _Task {

	void* stack_base;
	u32 stack_size;
	void* entry;
	void* param;

	u32 state;

	void* msg;

	void* buf_msg;

	u32 event_opt;
	u32 event_val;
	u32 event_data;

	ListNode rdy;	
	ListNode blk;

	u32 cpuid;
	void* blk_data;
}Task;


// sem struct 

typedef struct _Sem {

	u32 blk_type;
	SpinLock lock;
	ListNode head;
	u32 count;
}Sem;


// mutex struct 

typedef struct _Mutex {

	u32 blk_type;
	SpinLock lock;
	ListNode head;
	u32 count;
	Task* owner;
}Mutex;


// mail box struct

typedef struct _Mailbox {

	u32 blk_type;
	SpinLock lock;
	ListNode head;
	void* msg;
}Mailbox;


// msg buffer struct

typedef struct _Msgbuf {

	u32 blk_type;
	SpinLock lock;
	ListNode head;
	void** pp_msg;
	u32 size;
	u32 count;
	u32 start;
	u32 end;

}Msgbuf;

// event struct

#define AND_OPTION 0x1
#define OR_OPTION  0x2

typedef struct _Event {

	u32 blk_type;
	SpinLock lock;
	ListNode head;
	u32 val;
}Event;

// timer struct

typedef struct _Timer {

	ListNode list;
	u32 val;
	u64 second;
	void (*func)(void*);
	void* param;

}Timer;

// function ready to port

#define DISABLE_IE() port_enter_critical()
#define ENABLE_IE() port_exit_critical()
#define INIT_STACK_DATA(task, base, size, entry, param) port_stack_init(task, base, size >> 2, param, entry)
#define CONTEXT_SWITCH() port_task_switch()
#define START_FIRST_TASK() raw_start_first_task()
#define is_in_irq() (g_irq[cpuid])
#define is_sched_lock() (g_lock[cpuid])

// function about secondary cpu

#define boot_core(id) CreateThread(NULL, 0, secondary_entry, (LPVOID) id, 0, 0)

#endif


