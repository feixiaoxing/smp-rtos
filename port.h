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


/* 	2012-8  Created by jorya_txj
  *	xxxxxx   please added here
  */


#ifndef PORT_H
#define PORT_H
#include <stdio.h>
#include <assert.h> 
#include <windows.h> 


/*function prototype for task context switch during interrupt*/
void raw_int_switch(void);
void port_task_switch(void);
void raw_start_first_task(void); 


#define  RAW_ASSERT(CON)    if (!(CON)) { \
								volatile RAW_U8 dummy = 0; \
								assert(0); \
								while (dummy == 0); \
							}
										      

#endif


