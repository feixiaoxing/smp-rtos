
#include "os.h"

static Task task;;

static u8 stack[1024];

static Timer timer1;
static Timer timer2;

static void timer_func1(void* param){

	param = param;
	
	vc_port_printf("hello \t");
	
	activate_timer(&timer1);
}

static void timer_func2(void* param){

	param = param;

	vc_port_printf("world \t");

	activate_timer(&timer2);
}

static void run_task(void* param){

	param = param;
	
	create_timer(&timer1, 50, timer_func1, NULL);

	activate_timer(&timer1);

	create_timer(&timer2, 70, timer_func2, NULL);
	
	activate_timer(&timer2);
	
	while(1) {
		yield();
	}
}

extern int global_test;

void test_timer() {

	if(!global_test) {

		global_test = 1;

		create_task(&task, run_task, NULL, stack, 1024);
	}

}



