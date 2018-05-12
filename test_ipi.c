
#include "os.h"

static Task task1;
static Task task2;

static u8 task1_stack[1024];
static u8 task2_stack[1024];

static Sem sem;

static void run_task1(void* param){

	param = param;
	
	while(1) {
	
		get_sem(&sem, 1);
		
		vc_port_printf("get_sem ");
	}
}

static void run_task2(void* param){

	param = param;
	
	while(1) {
	
		put_sem(&sem);
	
		vc_port_printf("put_sem ");
		
		yield();
	}
}


void test_ipi() {	

	u8 cpuid = get_local_cpu();

	if(0 == cpuid) {

		create_sem(&sem, 1);

		create_task(&task1, run_task1, NULL, task1_stack, 1024);

	} else {

		create_task(&task2, run_task2, NULL, task2_stack, 1024);

	}
}


