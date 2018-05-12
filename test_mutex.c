
#include "os.h"

static Task task1;
static Task task2;

static u8 task1_stack[1024];
static u8 task2_stack[1024];

static Mutex mut;

static void run_task1(void* param){

	param = param;
	
	while(1) {
	
		get_mutex(&mut, 1);
		
		vc_port_printf("mut_for_task1 ");
		
		put_mutex(&mut);

		yield();
	}
}

static void run_task2(void* param){

	param = param;
	
	while(1) {
	
		get_mutex(&mut, 1);
	
		vc_port_printf("mut_for_task2 ");
		
		put_mutex(&mut);

		yield();
	}
}

extern int global_test;

void test_mutex() {

	if(!global_test) {

		global_test = 1;
		
		create_mutex(&mut);

		create_task(&task1, run_task1, NULL, task1_stack, 1024);
	
		create_task(&task2, run_task2, NULL, task2_stack, 1024);

	}

}



