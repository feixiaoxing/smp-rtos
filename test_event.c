
#include "os.h"

static Task task1;
static Task task2;

static u8 task1_stack[1024];
static u8 task2_stack[1024];

static Event evt;

static void run_task1(void* param){

	u32 data;

	param = param;
	
	while(1) {
	
		data = 0;
	
		get_event(&evt, AND_OPTION, 1, &data, 1);
		
		vc_port_printf("get_event ");
		
	}
}

static void run_task2(void* param){

	param = param;
	
	while(1) {
	
		put_event(&evt, 0x1);
	
		vc_port_printf("put_event ");
		
		yield();
	}
}

extern int global_test;

void test_event() {

	if(!global_test) {

		global_test = 1;

		create_event(&evt, 0);
		
		create_task(&task1, run_task1, NULL, task1_stack, 1024);
	
		create_task(&task2, run_task2, NULL, task2_stack, 1024);

	}

}



