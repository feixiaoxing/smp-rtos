
#include "port.h"
#include "os.h"

static void timer_entry(void* param);
static void idle_entry(void* param);
STATUS create_task(Task* p_task, void(* p_entry)(void*), void* param, void* p_stack, u32 stack_size);
void send_ipi(u32 cmd, void* p_data, u8 cpuid) ;
DWORD WINAPI secondary_entry(LPVOID param);

// list function

static void list_init(ListNode* node) {

	node->prev = node;
	node->next = node;
}

static STATUS is_list_empty(ListNode* node) {

	return node->next == node;
}

static void list_insert(ListNode* head, ListNode* node) {

	// save data ahead of head point

	node->prev = head->prev;
	node->next = head;

	head->prev->next = node;
	head->prev = node;
}

static void list_delete(ListNode* node) {

	node->prev->next = node->next;
	node->next->prev = node->prev;
}

// about spin lock function

void spin_lock_init(SpinLock* lock) {

	lock-> lock = 0;
}

void spin_lock(SpinLock* lock) {

	// atomic write

	while(InterlockedExchange(&lock->lock, 1) == 1);

	// memory barrier operation

	do {__asm {mfence}} while(0);
}

void spin_unlock(SpinLock* lock) {

	// memory barrier operation

	do {__asm {mfence}} while(0);

	// restore data to 0

	lock-> lock = 0;
}


// global defined here

u32 g_run[MAX_CPUS];
u32 g_irq[MAX_CPUS];
static ListNode g_ready[MAX_CPUS];

static u8 g_lock[MAX_CPUS];

static u64 g_idle[MAX_CPUS];
static u8 idle_stack[MAX_CPUS][1024];
static Task idle_task[MAX_CPUS];

static u64 g_tick;
static ListNode g_timer_head;
static SpinLock g_timer_lock;
static Sem g_timer_sem;
static Task timer_task;
static u8 timer_stack[1024];

Task* sched_task[MAX_CPUS];
Task* current_task[MAX_CPUS];

static SpinLock g_ipi_lock[MAX_CPUS];
static u32 g_ipi_cmd[MAX_CPUS];
static void* g_ipi_data[MAX_CPUS];


// sched lock

void sched_lock() {

	u8 cpuid;
	cpuid = get_local_cpu();

	DISABLE_IE();
	g_lock[cpuid] ++;
	ENABLE_IE();
}


void sched_unlock() {

	u8 cpuid;
	cpuid = get_local_cpu();

	DISABLE_IE();
	g_lock[cpuid] --;
	ENABLE_IE();

}


// os init

void os_init() {

	u8 cpuid;

	// about g_run

	for(cpuid = 0; cpuid < MAX_CPUS; cpuid ++)
		g_run[cpuid] = 0;

	// about g_lock, g_irq

	for(cpuid = 0; cpuid < MAX_CPUS; cpuid ++) {

		g_lock[cpuid] = 0;
		g_irq[cpuid] = 0;
	}

	// about g_ready

	for(cpuid = 0; cpuid < MAX_CPUS; cpuid ++)
		list_init(&g_ready[cpuid]);

	// about g_idle

	for(cpuid = 0; cpuid < MAX_CPUS; cpuid ++)
		g_idle[cpuid] = 0;

	// about sched_task and current_task

	for(cpuid = 0; cpuid < MAX_CPUS; cpuid ++) {

		sched_task[cpuid] = NULL;
		current_task[cpuid] = NULL;
	}

	// create idle task

	create_task(&idle_task[0], idle_entry, NULL, &idle_stack[0], 1024);

	// create timer task

	list_init(&g_timer_head);
	spin_lock_init(&g_timer_lock);

	g_tick = 0;

	create_sem(&g_timer_sem, 0);

	create_task(&timer_task, timer_entry, NULL, timer_stack, 1024);

	// about ipi

	for(cpuid = 0; cpuid < MAX_CPUS; cpuid ++) {

		spin_lock_init(&g_ipi_lock[cpuid]);
		g_ipi_cmd[cpuid] = 0;
		g_ipi_data[cpuid] = NULL;
	}
}


// boot sub cores

void boot_other_cores() {

	u32 cpuid;
	u32 pid;

	for(cpuid = 1; cpuid < MAX_CPUS; cpuid ++)
		boot_core(cpuid);
}


// os start

void os_start() {

	Task* p_task;

	// ready to boot sub cores, maybe core1, core2, core3 ...

	boot_other_cores();

	if(!g_run[0]){

		g_run[0] = 1;

		p_task = get_list_entry(g_ready[0].next, Task, rdy);
		p_task-> state = READY;
		current_task[0] = p_task;

		START_FIRST_TASK();
	}
}

// entry for sub cores

DWORD WINAPI secondary_entry(LPVOID param) {

	u8 cpuid;
	Task* p_task;

	cpuid = (u8) param;
	cpu_init(cpuid);

	// create task here or add task just by specified cpuid

	create_task(&idle_task[cpuid], idle_entry, NULL, &idle_stack[cpuid], 1024);

	// test_task();

	// test_sem();

	// test_mutex();

	// test_mail();

	// test_buf();

	// test_event();

	// test_timer();

	// test_ipi();

	test_ipi_mutex();

	if(!g_run[cpuid]){

		g_run[cpuid] = 1;

		p_task = get_list_entry(g_ready[cpuid].next, Task, rdy);
		p_task-> state = READY;
		current_task[cpuid] = p_task;

		START_FIRST_TASK();
	}

	return 0;
}

// add to ready queue

static void add_to_rdy_queue(ListNode* head, Task* p_task){

	list_insert(head, &p_task-> rdy);
}

// remove from ready queue

static void remove_from_rdy_queue(Task* p_task){

	list_delete(&p_task-> rdy);
}

// add to block queue, used by mutex, sem, mail, buf and event

static void add_to_blk_queue(ListNode* head, Task* p_task) {

	list_insert(head, &p_task-> blk);
}

// remove from block queue

static void remove_from_blk_queue(Task* p_task) {

	list_delete(&p_task-> blk);
}

// create task

STATUS create_task(Task* p_task, void(* p_entry)(void*), void* param, void* p_stack, u32 stack_size){

	u8 cpuid;

	if(NULL == p_task) {

		return PARAM_ERROR;
	}

	if(NULL == p_entry) {

		return PARAM_ERROR;
	}

	if(NULL == p_stack) {

		return PARAM_ERROR;
	}

	if(0 == stack_size){

		return PARAM_ERROR;
	}

	// get cpuid, then choose which ready queue it is added to

	cpuid = get_local_cpu();

	p_task-> entry = p_entry;
	p_task-> param = param;
	p_task-> stack_size = stack_size;

	p_task-> stack_base = (void*) INIT_STACK_DATA(p_task, p_stack, stack_size, p_entry, param);

	p_task-> msg = NULL;
	p_task-> buf_msg = NULL;

	p_task-> event_opt = 0;
	p_task-> event_val = 0;
	p_task-> event_data = 0;

	list_init(&p_task-> rdy);
	list_init(&p_task-> blk);

	p_task-> cpuid = (u32) cpuid;

	DISABLE_IE();

	add_to_rdy_queue(&g_ready[cpuid], p_task);
	p_task-> state = READY;

	ENABLE_IE();

	return SUCCESS;
}

// shutdown task

STATUS  shutdown_task(Task* p_task) {

	u8 cpuid;

	if(NULL == p_task) {

		return PARAM_ERROR;
	}

	if(DIE == p_task-> state) {

		return SUCCESS;
	}

	// task can only be processed by local cpu, this is very important

	cpuid = get_local_cpu();

	if(cpuid != p_task-> cpuid){

		return NOT_VALID_CPU;
	}

	DISABLE_IE();

	if(p_task == current_task[cpuid]) {

		ENABLE_IE();

		return SELF_KILL_FORBID;
	}

	if(READY == p_task-> state) {

		remove_from_rdy_queue(p_task);
	}else {

		// already send ipi, but not processed immediately

		if(!is_list_empty(&p_task-> blk)) {

			ENABLE_IE();

			return TASK_BLOCKED;
		}

		remove_from_blk_queue(p_task);
		p_task-> blk_data = NULL;

	}

	p_task-> state = DIE;

	ENABLE_IE();

	return SUCCESS;
}

// resume task

STATUS resume_task(Task* p_task) {

	u8 cpuid;

	if(NULL == p_task) {

		return PARAM_ERROR;
	}

	if(DIE != p_task-> state) {

		return SUCCESS;
	}

	cpuid = get_local_cpu();

	DISABLE_IE();

	if(cpuid != p_task-> cpuid) {

		ENABLE_IE();

		return NOT_VALID_CPU;
	}

	add_to_rdy_queue(&g_ready[cpuid], p_task);
	p_task-> state = READY;

	ENABLE_IE();

	return SUCCESS;
}

// dispatch function

STATUS dispatch() {

	u8 cpuid;
	Task* p_task;

	cpuid = get_local_cpu();

	if(is_in_irq()) {

		return IN_IRQ;
	}

	if(is_sched_lock()) {

		return OS_SCHED_LOCKED;
	}

	p_task = get_list_entry(g_ready[cpuid].next, Task, rdy);
	sched_task[cpuid] = p_task;

	if(current_task[cpuid] != p_task){

		p_task-> state = RUNNING;
		CONTEXT_SWITCH();
	}

	return SUCCESS;
}


// yield function

STATUS yield() {

	u8 cpuid;
	Task* p_task;

	cpuid = get_local_cpu();

	if(is_in_irq()) {

		return IN_IRQ;
	}

	if(is_sched_lock()) {

		return OS_SCHED_LOCKED;
	}

	DISABLE_IE();

	// move current task to tail position

	remove_from_rdy_queue(current_task[cpuid]);
	add_to_rdy_queue(&g_ready[cpuid], current_task[cpuid]);

	current_task[cpuid]-> state = READY;

	// try to get new task from head position

	p_task = get_list_entry(g_ready[cpuid].next, Task, rdy);
	sched_task[cpuid] = p_task;

	if(current_task[cpuid] != p_task){

		p_task-> state = RUNNING;
		CONTEXT_SWITCH();
	}

	ENABLE_IE();

	return SUCCESS;
}

// create sem

STATUS create_sem(Sem* p_sem, u32 count){

	if(NULL == p_sem) {

		return PARAM_ERROR;
	}

	p_sem-> blk_type = SEM_TYPE;

	spin_lock_init(&p_sem-> lock);
	list_init(&p_sem-> head);
	p_sem-> count = count;

	return SUCCESS;
}

// get sem

STATUS get_sem(Sem* p_sem, u8 wait){

	u8 cpuid;
	STATUS result;

	cpuid = get_local_cpu();

	if(is_in_irq()) {

		return IN_IRQ;
	}

	if(NULL == p_sem) {

		return PARAM_ERROR;
	}

	if(SEM_TYPE != p_sem-> blk_type) {

		return WRONG_BLOCK_TYPE;
	}

	DISABLE_IE();
	spin_lock(&p_sem-> lock);

	if(p_sem-> count) {

		p_sem-> count --;

		spin_unlock(&p_sem-> lock);
		ENABLE_IE();

		return SUCCESS;
	}

	if(is_sched_lock()) {

		spin_unlock(&p_sem-> lock);
		ENABLE_IE();

		return OS_SCHED_LOCKED;
	}

	if(!wait) {

		spin_unlock(&p_sem-> lock);
		ENABLE_IE();

		return NOT_WAIT;
	}

	remove_from_rdy_queue(current_task[cpuid]);
	add_to_blk_queue(&p_sem-> head, current_task[cpuid]);

	current_task[cpuid]-> state =  BLOCKED;
	current_task[cpuid]-> blk_data = p_sem;

	spin_unlock(&p_sem-> lock);
	result = dispatch();

	ENABLE_IE();

	return result;
}

// put sem

STATUS put_sem(Sem* p_sem) {

	u8 cpuid;
	Task* p_task;

	if(NULL == p_sem) {

		return PARAM_ERROR;
	}

	if(SEM_TYPE != p_sem-> blk_type){

		return WRONG_BLOCK_TYPE;
	}

	DISABLE_IE();
	spin_lock(&p_sem-> lock);

	if(is_list_empty(&p_sem-> head)){

		p_sem-> count ++;

		spin_unlock(&p_sem-> lock);
		ENABLE_IE();

		return SUCCESS;
	}

	p_task = get_list_entry(p_sem->head.next, Task, blk);
	remove_from_blk_queue(p_task);
	list_init(&p_task-> blk);

	cpuid = get_local_cpu();

	if(cpuid != p_task-> cpuid) {

		// send ipi to target cpu

		send_ipi(WAKE_UP_TASK, p_task, p_task-> cpuid);

		spin_unlock(&p_sem-> lock);
		ENABLE_IE();

		return SUCCESS;
	}


	add_to_rdy_queue(&g_ready[cpuid], p_task);
	p_task-> state = READY;
	p_task-> blk_data = NULL;

	spin_unlock(&p_sem-> lock);
	ENABLE_IE();

	return SUCCESS;
}

// create mutex

STATUS create_mutex(Mutex* p_mut){

	if(NULL == p_mut){

		return PARAM_ERROR;
	}

	p_mut-> blk_type = MUT_TYPE;
	spin_lock_init(&p_mut-> lock);
	list_init(&p_mut-> head);

	p_mut-> count = 1;
	p_mut-> owner = NULL;

	return SUCCESS;
}


// get mutex

STATUS get_mutex(Mutex* p_mut, u8 wait){

	u8 cpuid;
	STATUS result;

	cpuid = get_local_cpu();

	if(is_in_irq()) {

		return IN_IRQ;
	}

	if(NULL == p_mut){

		return PARAM_ERROR;
	}

	if(MUT_TYPE != p_mut-> blk_type){

		return WRONG_BLOCK_TYPE;
	}

	DISABLE_IE();
	spin_lock(&p_mut-> lock);

	if(p_mut-> count){

		// owner is main difference between sem and mutex

		p_mut-> count = 0;
		p_mut-> owner = current_task[cpuid];

		spin_unlock(&p_mut-> lock);
		ENABLE_IE();

		return SUCCESS;
	}

	if(is_sched_lock()){

		spin_unlock(&p_mut-> lock);
		ENABLE_IE();

		return OS_SCHED_LOCKED;
	}

	if(!wait) {

		spin_unlock(&p_mut-> lock);
		ENABLE_IE();

		return NOT_WAIT;
	}

	remove_from_rdy_queue(current_task[cpuid]);
	add_to_blk_queue(&p_mut-> head, current_task[cpuid]);

	current_task[cpuid]-> state = BLOCKED;
	current_task[cpuid]-> blk_data = p_mut;

	spin_unlock(&p_mut-> lock);
	result = dispatch();

	ENABLE_IE();

	return result;
}

// put mutex

STATUS put_mutex(Mutex* p_mut){

	u8 cpuid;
	Task* p_task;

	if(NULL == p_mut){

		return PARAM_ERROR;
	}

	if(MUT_TYPE != p_mut-> blk_type){

		return WRONG_BLOCK_TYPE;
	}

	DISABLE_IE();
	spin_lock(&p_mut-> lock);

	cpuid = get_local_cpu();

	if(current_task[cpuid] != p_mut-> owner){

		spin_unlock(&p_mut-> lock);
		ENABLE_IE();

		return NOT_MUTEX_OWNER;
	}

	if(is_list_empty(&p_mut-> head)){

		p_mut-> count = 1;
		p_mut-> owner = NULL;

		spin_unlock(&p_mut-> lock);
		ENABLE_IE();

		return SUCCESS;
	}

	p_task = get_list_entry(p_mut-> head.next, Task, blk);
	remove_from_blk_queue(p_task);

	// set owner immediately once get mutex

	p_mut-> owner = p_task;

	list_init(&p_task-> blk);

	if(cpuid != p_task-> cpuid) {

		send_ipi(WAKE_UP_TASK, p_task, p_task-> cpuid);

		spin_unlock(&p_mut-> lock);
		ENABLE_IE();

		return SUCCESS;
	}

	add_to_rdy_queue(&g_ready[cpuid], p_task);
	p_task-> state = READY;
	p_task-> blk_data = NULL;

	spin_unlock(&p_mut-> lock);
	ENABLE_IE();

	return SUCCESS;
}

// create mail

STATUS create_mail(Mailbox* p_mail, void* msg){

	if(NULL == p_mail){

		return PARAM_ERROR;
	}

	p_mail-> blk_type = MAIL_TYPE;
	spin_lock_init(&p_mail-> lock);

	list_init(&p_mail-> head);
	p_mail-> msg = msg;

	return SUCCESS;
}

// get mail

STATUS get_mail(Mailbox* p_mail, void** pp_msg, u8 wait){

	u8 cpuid;
	STATUS result;

	cpuid = get_local_cpu();

	if(is_in_irq()) {

		return IN_IRQ;
	}

	if(NULL == p_mail) {

		return PARAM_ERROR;
	}

	if(NULL == pp_msg) {

		return PARAM_ERROR;
	}

	DISABLE_IE();
	spin_lock(&p_mail-> lock);

	if(p_mail-> msg){

		*pp_msg = p_mail-> msg;
		p_mail-> msg = NULL;

		spin_unlock(&p_mail-> lock);
		ENABLE_IE();

		return SUCCESS;
	}

	if(is_sched_lock()){

		spin_unlock(&p_mail-> lock);
		ENABLE_IE();

		return OS_SCHED_LOCKED;
	}

	if(!wait) {

		spin_unlock(&p_mail-> lock);
		ENABLE_IE();

		return NOT_WAIT;
	}

	remove_from_rdy_queue(current_task[cpuid]);
	add_to_blk_queue(&p_mail-> head, current_task[cpuid]);

	current_task[cpuid]-> state = BLOCKED;
	current_task[cpuid]-> blk_data = p_mail;

	spin_unlock(&p_mail-> lock);
	result = dispatch();

	ENABLE_IE();

	if(SUCCESS != result) {

		return result;
	}

	DISABLE_IE();
	spin_lock(&p_mail-> lock);

	// get message form task object

	*pp_msg = current_task[cpuid]-> msg;

	spin_unlock(&p_mail-> lock);
	ENABLE_IE();

	return SUCCESS;
}

// put mail

STATUS put_mail(Mailbox* p_mail, void* p_msg){

	u8 cpuid;
	Task* p_task;

	if(NULL == p_mail) {

		return PARAM_ERROR;
	}

	if(NULL == p_msg){

		return PARAM_ERROR;
	}

	if(MAIL_TYPE != p_mail-> blk_type){

		return WRONG_BLOCK_TYPE;
	}

	DISABLE_IE();
	spin_lock(&p_mail-> lock);

	if(p_mail-> msg) {

		spin_unlock(&p_mail-> lock);
		ENABLE_IE();

		return MSG_EXIST;
	}

	if(is_list_empty(&p_mail-> head)){

		p_mail-> msg =  p_msg;

		spin_unlock(&p_mail-> lock);
		ENABLE_IE();

		return SUCCESS;
	}

	cpuid = get_local_cpu();

	p_task = get_list_entry(p_mail->head.next, Task, blk);
	remove_from_blk_queue(p_task);
	list_init(&p_task-> blk);

	p_task-> msg = p_msg;

	if(cpuid != p_task-> cpuid) {

		send_ipi(WAKE_UP_TASK, p_task, p_task-> cpuid);

		spin_unlock(&p_mail-> lock);
		ENABLE_IE();

		return SUCCESS;
	}

	add_to_rdy_queue(&g_ready[cpuid], p_task);
	p_task-> state =  READY;
	p_task-> blk_data = NULL;

	spin_unlock(&p_mail-> lock);
	ENABLE_IE();

	return SUCCESS;
}

// create buf

STATUS create_msg_buf(Msgbuf* p_buf, void** pp_msg, u32 size) {

	if(NULL == p_buf) {

		return PARAM_ERROR;
	}

	if(NULL == pp_msg) {

		return PARAM_ERROR;
	}

	if(!size) {

		return PARAM_ERROR;
	}

	p_buf-> blk_type = BUF_TYPE;
	spin_lock_init(&p_buf-> lock);
	list_init(&p_buf-> head);

	p_buf-> pp_msg = pp_msg;
	p_buf-> size = size;
	p_buf-> count = 0;
	p_buf-> start = 0;
	p_buf-> end = 0;

	return SUCCESS;
}

// get buffer

STATUS get_msg_buf(Msgbuf* p_buf, void** pp_msg, u8 wait) {

	u8 cpuid;
	STATUS result;

	cpuid = get_local_cpu();

	if(is_in_irq()) {

		return IN_IRQ;
	}

	if(NULL == p_buf) {

		return PARAM_ERROR;
	}

	if(BUF_TYPE != p_buf-> blk_type) {

		return WRONG_BLOCK_TYPE;
	}

	if(NULL == pp_msg) {

		return PARAM_ERROR;
	}

	DISABLE_IE();
	spin_lock(&p_buf-> lock);

	if(p_buf-> count) {

		p_buf-> count --;
		*pp_msg = p_buf-> pp_msg[p_buf-> end];

		p_buf-> end ++;
		if(p_buf-> end == p_buf-> size) {

			p_buf-> end = 0;
		}

		spin_unlock(&p_buf-> lock);
		ENABLE_IE();

		return SUCCESS;
	}

	if(is_sched_lock()){

		spin_unlock(&p_buf-> lock);
		ENABLE_IE();

		return OS_SCHED_LOCKED;
	}

	if(!wait) {

		spin_unlock(&p_buf-> lock);
		ENABLE_IE();

		return NOT_WAIT;
	}

	remove_from_rdy_queue(current_task[cpuid]);
	add_to_blk_queue(&p_buf-> head, current_task[cpuid]);

	current_task[cpuid]-> state = BLOCKED;
	current_task[cpuid]-> blk_data = p_buf;

	spin_unlock(&p_buf-> lock);
	result = dispatch();

	ENABLE_IE();

	if(SUCCESS != result) {

		return result;
	}

	DISABLE_IE();
	spin_lock(&p_buf-> lock);

	// get msg form task object, but a little bit difficult

	*pp_msg = current_task[cpuid]-> buf_msg;

	spin_unlock(&p_buf-> lock);
	ENABLE_IE();

	return SUCCESS;
}

// put buffer

STATUS put_msg_buf(Msgbuf* p_buf, void* p_msg){

	u8 cpuid;
	Task* p_task;

	if(NULL == p_buf) {

		return PARAM_ERROR;
	}

	if(BUF_TYPE != p_buf-> blk_type) {

		return WRONG_BLOCK_TYPE;
	}

	if(NULL == p_msg) {

		return PARAM_ERROR;
	}

	DISABLE_IE();
	spin_lock(&p_buf-> lock);

	if(p_buf-> count == p_buf-> size) {

		spin_unlock(&p_buf-> lock);
		ENABLE_IE();

		return MSG_FULL;
	}

	if(is_list_empty(&p_buf-> head)) {

		p_buf-> pp_msg[p_buf-> start] = p_msg;
		p_buf-> start ++;

		if(p_buf-> start == p_buf-> size) {

			p_buf-> start = 0;
		}

		p_buf-> count ++;

		spin_unlock(&p_buf-> lock);
		ENABLE_IE();

		return SUCCESS;
	}

	cpuid = get_local_cpu();

	p_task = get_list_entry(p_buf->head.next, Task, blk);
	remove_from_blk_queue(p_task);
	list_init(&p_task-> blk);

	p_task-> buf_msg = p_msg;

	if(p_task-> cpuid != cpuid) {

		send_ipi(WAKE_UP_TASK, p_task, p_task-> cpuid);

		spin_unlock(&p_buf-> lock);
		ENABLE_IE();

		return SUCCESS;
	}

	add_to_rdy_queue(&g_ready[cpuid], p_task);
	p_task-> state = READY;
	p_task-> blk_data = NULL;

	spin_unlock(&p_buf-> lock);
	ENABLE_IE();

	return SUCCESS;
}

// create event

STATUS create_event(Event* p_event, u32 val) {

	if(NULL == p_event) {

		return PARAM_ERROR;
	}

	p_event-> blk_type = EVENT_TYPE;
	spin_lock_init(&p_event-> lock);
	list_init(&p_event-> head);
	p_event-> val = val;

	return SUCCESS;

}

// get event

STATUS get_event(Event* p_event, u32 opt, u32 val, u32* p_data, u8 wait){

	u8 cpuid;
	STATUS result;

	cpuid = get_local_cpu();

	if(is_in_irq()) {

		return IN_IRQ;
	}

	if(NULL == p_event) {

		return PARAM_ERROR;
	}

	if(EVENT_TYPE != p_event-> blk_type) {

		return WRONG_BLOCK_TYPE;
	}

	if(AND_OPTION != opt && OR_OPTION != opt) {

		return PARAM_ERROR;
	}

	if(NULL == p_data) {

		return PARAM_ERROR;
	}

	DISABLE_IE();
	spin_lock(&p_event-> lock);

	if(AND_OPTION == opt) {

		if(val == (p_event-> val & val)) {

			*p_data = val;
			p_event-> val &= ~val;

			spin_unlock(&p_event-> lock);
			ENABLE_IE();

			return SUCCESS;
		}
	}else {

		if(p_event-> val & val) {

			*p_data = p_event-> val & val;
			p_event-> val &= ~*p_data;

			spin_unlock(&p_event-> lock);
			ENABLE_IE();

			return SUCCESS;
		}
	}

	if(is_sched_lock()) {

		spin_unlock(&p_event-> lock);
		ENABLE_IE();

		return OS_SCHED_LOCKED;
	}

	if(!wait) {

		spin_unlock(&p_event-> lock);
		ENABLE_IE();

		return NOT_WAIT;
	}

	remove_from_rdy_queue(current_task[cpuid]);
	add_to_blk_queue(&p_event-> head, current_task[cpuid]);

	// save all dato to task object, which will be useful in put_event

	current_task[cpuid]-> event_opt = opt;
	current_task[cpuid]-> event_val = val;

	current_task[cpuid]-> state = BLOCKED;
	current_task[cpuid]-> blk_data = p_event;

	spin_unlock(&p_event-> lock);
	result = dispatch();
	ENABLE_IE();

	if(SUCCESS != result) {

		return result;
	}

	DISABLE_IE();
	spin_lock(&p_event-> lock);

	*p_data = current_task[cpuid]-> event_data;

	spin_unlock(&p_event-> lock);
	ENABLE_IE();

	return SUCCESS;
}

// put event

STATUS put_event(Event* p_event, u32 val) {

	u8 cpuid;
	ListNode* p_node;
	Task* p_task;

	if(NULL == p_event) {

		return PARAM_ERROR;
	}

	if(EVENT_TYPE != p_event-> blk_type) {

		return WRONG_BLOCK_TYPE;
	}

	DISABLE_IE();
	spin_lock(&p_event-> lock);

	p_event-> val |= val;

	if(is_list_empty(&p_event-> head)) {

		spin_unlock(&p_event-> lock);
		ENABLE_IE();

		return SUCCESS;
	}

	p_node = p_event-> head.next;
	cpuid = get_local_cpu();

	// all pending task should be checked, as every task is possible to be ready again

	while(p_node != &p_event->head) {

		p_task = get_list_entry(p_node, Task, blk);
		if(AND_OPTION == p_task-> event_opt ) {

			if(p_task-> event_val & p_event-> val == p_task-> event_val) {

				p_task-> event_data = p_task-> event_val;
				p_event-> val &= ~p_task-> event_val;

				p_node = p_node-> next;
				remove_from_blk_queue(p_task);
				list_init(&p_task-> blk);

				if(cpuid != p_task-> cpuid) {

					send_ipi(WAKE_UP_TASK, p_task, p_task-> cpuid);
				}else {

					add_to_rdy_queue(&g_ready[cpuid], p_task);
					p_task-> state = READY;
					p_task-> blk_data = NULL;
				}

				continue;
			}

		}else {

			if(p_task-> event_val & p_event-> val) {

				p_task-> event_data = p_task-> event_val & p_event-> val;
				p_event-> val &= ~p_task-> event_data;

				p_node = p_node-> next;
				remove_from_blk_queue(p_task);
				list_init(&p_task-> blk);

				if(cpuid != p_task-> cpuid) {

					send_ipi(WAKE_UP_TASK, p_task, p_task-> cpuid);
				}else {

					add_to_rdy_queue(&g_ready[cpuid], p_task);
					p_task-> state = READY;
					p_task-> blk_data = NULL;
				}

				continue;
			}
		}

		p_node = p_node-> next;
	}

	spin_unlock(&p_event-> lock);
	ENABLE_IE();

	return SUCCESS;

}

// idle entry for all cores

static void idle_entry(void* param) {

	u8 cpuid;

	param = param;
	cpuid = get_local_cpu();

	while(1) {

		DISABLE_IE();
		g_idle[cpuid] ++;
		ENABLE_IE();

		yield();
	}

}

// create timer

STATUS create_timer(Timer* p_timer, u32 val, void(*func)(void*), void* param){

	if(NULL == p_timer) {

		return PARAM_ERROR;
	}

	list_init(&p_timer-> list);
	p_timer-> val = val;
	p_timer-> second = 0;
	p_timer-> func = func;
	p_timer-> param = param;

}

// activate timer

STATUS activate_timer(Timer* p_timer) {

	ListNode* p_node;

	if(NULL == p_timer) {

		return PARAM_ERROR;
	}

	DISABLE_IE();
	spin_lock(&g_timer_lock);

	p_timer-> second = g_tick + p_timer-> val;
	p_node = g_timer_head.next;

	while(p_node != &g_timer_head){

		if(get_list_entry(p_node, Timer, list)-> second > p_timer-> second){

			break;
		}

		p_node = p_node-> next;
	}

	list_insert(p_node, &p_timer->list);

	spin_unlock(&g_timer_lock);
	ENABLE_IE();
}

// deactivate timer

STATUS deactivate_timer(Timer* p_timer) {

	ListNode* p_node;

	DISABLE_IE();
	spin_lock(&g_timer_lock);

	if(!is_list_empty(&p_timer-> list)) {

		list_delete(&p_timer-> list);
	}

	spin_unlock(&g_timer_lock);
	ENABLE_IE();

	return SUCCESS;
}


// timer entry, only fore core 0

static void timer_entry(void* param) {

	Timer* p_timer;
	ListNode* p_node;

	while(1) {

		get_sem(&g_timer_sem, 1);

	start:

		DISABLE_IE();
		spin_lock(&g_timer_lock);

		p_node = g_timer_head.next;
		while(p_node != &g_timer_head) {

			p_timer = get_list_entry(p_node, Timer, list);
			if(p_timer-> second > g_tick) {

				break;
			}

			p_node = p_node-> next;
			list_delete(&p_timer-> list);

			spin_unlock(&g_timer_lock);
			ENABLE_IE();

			p_timer-> func(p_timer-> param);

			goto start;
		}

		spin_unlock(&g_timer_lock);
		ENABLE_IE();

	}

}


// timer isr function

void timer_isr_func() {

	g_tick ++;

	put_sem(&g_timer_sem);
}

// send ipi

void send_ipi(u32 cmd, void* p_data, u8 cpuid) {

	spin_lock(&g_ipi_lock[cpuid]);

	g_ipi_cmd[cpuid] = cmd;
	g_ipi_data[cpuid] = p_data;

	ipi_trigger(cpuid);
}


// ipi isr func, for all cores

void ipi_isr_func() {

	u8 cpuid;
	u32 cmd;
	void* p_data;
	Task* p_task;

	cpuid = get_local_cpu();

	cmd = g_ipi_cmd[cpuid];
	p_data = g_ipi_data[cpuid];

	// ready to get other ipi cmd

	spin_unlock(&g_ipi_lock[cpuid]);

	DISABLE_IE();

	p_task = (Task*) p_data;

	if(BLOCKED == p_task-> state) {

		add_to_rdy_queue(&g_ready[cpuid], p_task);
		p_task-> blk_data = NULL;
		p_task-> state = READY;
	}

	ENABLE_IE();
}

// test function entry

int global_test;

int main(int argc, char* argv[]){

	cpu_init(0);

	os_init();

	global_test = 0;

	// test_task();

	// test_sem();

	// test_mutex();

	// test_mail();

	// test_buf();

	// test_event();

	// test_timer();

	// test_ipi();

	test_ipi_mutex();

	os_start();

	return 0;
}

