
#include <limits.h>
#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>

#include "host.h"
#include "misc.h"
#include "machine.h"
#include "regs.h"
#include "memory.h"
#include "loader.h"
#include "syscall.h"
#include "dlite.h"
#include "options.h"
#include "stats.h"
#include "sim.h"
#include "decode.def"

#include "instr.h"

/* PARAMETERS OF THE TOMASULO'S ALGORITHM */

#define INSTR_QUEUE_SIZE         10

#define RESERV_INT_SIZE    4
#define RESERV_FP_SIZE     2
#define FU_INT_SIZE        2
#define FU_FP_SIZE         1

#define FU_INT_LATENCY     4
#define FU_FP_LATENCY      9

/* IDENTIFYING INSTRUCTIONS */

//unconditional branch, jump or call
#define IS_UNCOND_CTRL(op) (MD_OP_FLAGS(op) & F_CALL || \
                         MD_OP_FLAGS(op) & F_UNCOND)

//conditional branch instruction
#define IS_COND_CTRL(op) (MD_OP_FLAGS(op) & F_COND)

//floating-point computation
#define IS_FCOMP(op) (MD_OP_FLAGS(op) & F_FCOMP)

//integer computation
#define IS_ICOMP(op) (MD_OP_FLAGS(op) & F_ICOMP)

//load instruction
#define IS_LOAD(op)  (MD_OP_FLAGS(op) & F_LOAD)

//store instruction
#define IS_STORE(op) (MD_OP_FLAGS(op) & F_STORE)

//trap instruction
#define IS_TRAP(op) (MD_OP_FLAGS(op) & F_TRAP) 

#define USES_INT_FU(op) (IS_ICOMP(op) || IS_LOAD(op) || IS_STORE(op))
#define USES_FP_FU(op) (IS_FCOMP(op))

#define WRITES_CDB(op) (IS_ICOMP(op) || IS_LOAD(op) || IS_FCOMP(op))

/* FOR DEBUGGING */

//prints info about an instruction
#define PRINT_INST(out,instr,str,cycle)	\
  myfprintf(out, "%d: %s", cycle, str);		\
  md_print_insn(instr->inst, instr->pc, out); \
  myfprintf(stdout, "(%d)\n",instr->index);

#define PRINT_REG(out,reg,str,instr) \
  myfprintf(out, "reg#%d %s ", reg, str);	\
  md_print_insn(instr->inst, instr->pc, out); \
  myfprintf(stdout, "(%d)\n",instr->index);

/* VARIABLES */

//instruction queue for tomasulo
static instruction_t* instr_queue[INSTR_QUEUE_SIZE];
//number of instructions in the instruction queue
static int instr_queue_size = 0;
/* ECE552 Assignment 3 -BEGIN CODE*/
static int instr_queue_head = 0;
static int instr_queue_tail = 0;
/* ECE552 Assignment 3 -END CODE*/


//reservation stations (each reservation station entry contains a pointer to an instruction)
static instruction_t* reservINT[RESERV_INT_SIZE];
static instruction_t* reservFP[RESERV_FP_SIZE];

//functional units
static instruction_t* fuINT[FU_INT_SIZE];
static instruction_t* fuFP[FU_FP_SIZE];

//common data bus
static instruction_t* commonDataBus = NULL;

//The map table keeps track of which instruction produces the value for each register
static instruction_t* map_table[MD_TOTAL_REGS];

//the index of the last instruction fetched
static int fetch_index = 0;

/* FUNCTIONAL UNITS */


/* RESERVATION STATIONS */


/* 
 * Description: 
 * 	Checks if simulation is done by finishing the very last instruction
 *      Remember that simulation is done only if the entire pipeline is empty
 * Inputs:
 * 	sim_insn: the total number of instructions simulated
 * Returns:
 * 	True: if simulation is finished
 */
static bool is_simulation_done(counter_t sim_insn) {

  /* ECE552: YOUR CODE GOES HERE */

    /* ECE552 Assignment 3 -BEGIN CODE*/

    //if (fetch_index == sim_insn)
    //    return true;
    bool isFinished = true;
    // check IFQ
    if (instr_queue_size != 0) {
        //printf("instr_queue_size: %d\n", instr_queue_size);
        isFinished = false;
    }
    // check RS
    int i;
    for (i = 0; i < RESERV_INT_SIZE; i++)
    {
        if (reservINT[i] != NULL) {
            //PRINT_INST(stdout, reservINT[i], "reservINT is not empty: ", 0);
            isFinished = false;
        }
    }
    for (i = 0; i < RESERV_FP_SIZE; i++)
    {
        if (reservFP[i] != NULL) {
            //PRINT_INST(stdout, reservFP[i], "reservFP is not empty: ", 0);
            isFinished = false;
        }
    }
    // check CDB
    if (commonDataBus != NULL) {
        //PRINT_INST(stdout, commonDataBus, "CDB is not empty: ", 0);
        isFinished = false;
    }

  return isFinished; //ECE552: you can change this as needed; we've added this so the code provided to you compiles
    /* ECE552 Assignment 3 -END CODE*/

}

/* 
 * Description: 
 * 	Retires the instruction from writing to the Common Data Bus
 * Inputs:
 * 	current_cycle: the cycle we are at
 * Returns:
 * 	None
 */
void CDB_To_retire(int current_cycle) {

  /* ECE552: YOUR CODE GOES HERE */
    /* ECE552 Assignment 3 -BEGIN CODE*/

    // check CDB and wait for one cycle to boardcast value
    if (commonDataBus != NULL) {
            int i;
            for (i = 0; i < RESERV_INT_SIZE; i++) {
                if (reservINT[i] == NULL) continue;
                //PRINT_INST(stdout, reservINT[i], "reservINT: ", current_cycle);
                //if (reservINT[i]->Q[0] != NULL)
                //    printf("cdb index: %d, reservINT index: %d\n", commonDataBus->index, reservINT[i]->Q[0]->index);
                //PRINT_INST(stdout, reservINT[i]->Q, "relex: ", current_cycle);
                int j;
                for (j = 0; j < 3; j++) {
                    if (reservINT[i]->Q[j] != NULL &&
                            reservINT[i]->Q[j]->index == commonDataBus->index) {
                        reservINT[i]->Q[j] = NULL;
                        //PRINT_INST(stdout, reservINT[i], "relex: ", current_cycle);
                    }
                }
            }
            for (i = 0; i < RESERV_FP_SIZE; i++) {
                if (reservFP[i] == NULL) continue;
                int j;
                for (j = 0; j < 3; j++) {
                    if (reservFP[i]->Q[j] != NULL &&
                            reservFP[i]->Q[j]->index == commonDataBus->index) {
                        reservFP[i]->Q[j] = NULL;
                    }
                }
            }
        //PRINT_INST(stdout, commonDataBus, "retire CDB: ", current_cycle);
        // retire CDB
        commonDataBus = NULL;
    }
    /* ECE552 Assignment 3 -END CODE*/

}



/* 
 * Description: 
 * 	Moves an instruction from the execution stage to common data bus (if possible)
 * Inputs:
 * 	current_cycle: the cycle we are at
 * Returns:
 * 	None
 */
void execute_To_CDB(int current_cycle) {

    /* ECE552 Assignment 3 -BEGIN CODE*/

  /* ECE552: YOUR CODE GOES HERE */
    // check for CDB
    if (commonDataBus != NULL)
        return;
    
    // check fuINT
    int i;
    instruction_t* oldest_inst = NULL;

    for (i = 0; i < FU_INT_SIZE; i++) {
        if (fuINT[i] != NULL && (current_cycle - fuINT[i]->tom_execute_cycle >= FU_INT_LATENCY)) {
            // special case for store
            // no need to wait for CDB, release RS and FU
            if (IS_STORE(fuINT[i]->op)) {
                int j;
                for (j = 0; j < RESERV_INT_SIZE; j++) {
                    if (reservINT[j] != NULL && reservINT[j]->index == fuINT[i]->index) {
                        reservINT[j] = NULL;
                        break;
                    }
                }
                // release fuINT[i]
                fuINT[i] = NULL;
            } else {
                //PRINT_INST(stdout, fuINT[i], "fuINT is ready for CDB: ", current_cycle);
                // compete for CDB
                if (oldest_inst == NULL || fuINT[i]->index < oldest_inst->index) {
                    oldest_inst = fuINT[i];
                }
            }
        }
    }
    
    // check fuFP
    for (i = 0; i < FU_FP_SIZE; i++) {
        if (fuFP[i] != NULL && (current_cycle - fuFP[i]->tom_execute_cycle >= FU_FP_LATENCY)) {
            //PRINT_INST(stdout, fuFP[i], "fuFP is ready for CDB: ", current_cycle);
            // compete for CDB
            if (oldest_inst == NULL || fuFP[i]->index < oldest_inst->index) {
                oldest_inst = fuFP[i];
            }
        }
    }
    if (oldest_inst != NULL && USES_FP_FU(oldest_inst->op)) {
        oldest_inst->tom_cdb_cycle = current_cycle;
        commonDataBus = oldest_inst;
        //PRINT_INST(stdout, commonDataBus, "CDB_FP: ", current_cycle);
        // check if map_table tag is the same
        // if it is the same, release it
        int j;
        for (j = 0; j < 2; j++) {
            if (commonDataBus->r_out[j] != DNA) {
                if (map_table[commonDataBus->r_out[j]]->index == commonDataBus->index) {
                    map_table[commonDataBus->r_out[j]] = NULL;
                }
            }
        }

        for (j = 0; j < RESERV_FP_SIZE; j++) {
            if (reservFP[j] != NULL && reservFP[j]->index == commonDataBus->index) {
                reservFP[j] = NULL;
                break;
            }
        }
        // deallocate rs and fu
        for (j = 0; j < FU_FP_SIZE; j++) {
            if (fuFP[j] != NULL && fuFP[j]->index == commonDataBus->index) {
                fuFP[j] = NULL;
                break;
            }
        }

    } else if (oldest_inst != NULL && USES_INT_FU(oldest_inst->op)) {
        oldest_inst->tom_cdb_cycle = current_cycle;
        commonDataBus = oldest_inst;
        //PRINT_INST(stdout, commonDataBus, "CDB_INT: ", current_cycle);
        int j;
        for (j = 0; j < 2; j++) {
            if (commonDataBus->r_out[j] != DNA) {
                if (map_table[commonDataBus->r_out[j]] != NULL && 
                        map_table[commonDataBus->r_out[j]]->index == commonDataBus->index) {
                    map_table[commonDataBus->r_out[j]] = NULL;
                }
            }
        }
        for (j = 0; j < RESERV_INT_SIZE; j++) {
            if (reservINT[j] != NULL && reservINT[j]->index == commonDataBus->index) {
                reservINT[j] = NULL;
                break;
            }
        }
        // deallocate rs and fu
        for (j = 0; j < FU_INT_SIZE; j++) {
            if (fuINT[j] != NULL && fuINT[j]->index == commonDataBus->index) {
                fuINT[j] = NULL;
                break;
            }
        }
    }
    /* ECE552 Assignment 3 -END CODE*/

}

/* 
 * Description: 
 * 	Moves instruction(s) from the issue to the execute stage (if possible). We prioritize old instructions
 *      (in program order) over new ones, if they both contend for the same functional unit.
 *      All RAW dependences need to have been resolved with stalls before an instruction enters execute.
 * Inputs:
 * 	current_cycle: the cycle we are at
 * Returns:
 * 	None
 */
void issue_To_execute(int current_cycle) {
    /* ECE552 Assignment 3 -BEGIN CODE*/

  /* ECE552: YOUR CODE GOES HERE */
    // check reservINT
    instruction_t* instr_ready_queue[RESERV_INT_SIZE] = {NULL};
    int ready_queue_head = 0;
    int ready_queue_size = 0;
    int i;
    for (i = 0; i < RESERV_INT_SIZE; i++) {
        if (reservINT[i] != NULL &&
            reservINT[i]->Q[0] == NULL &&
            reservINT[i]->Q[1] == NULL &&
            reservINT[i]->Q[2] == NULL &&
            reservINT[i]->tom_execute_cycle == 0) {
            instr_ready_queue[ready_queue_size++] = reservINT[i];
            //PRINT_INST(stdout, reservINT[i], "execute_INT_ready: ", current_cycle);
        }
    }
    // sort ready_queue based on index
    for (i = 0; i < ready_queue_size; i++) {
        int j;
        for (j = 0; j < ready_queue_size - 1; j++) {
            // bubble sort
            if (instr_ready_queue[j]->index > instr_ready_queue[j+1]->index) {
                instruction_t* temp = instr_ready_queue[j+1];
                instr_ready_queue[j+1] = instr_ready_queue[j];
                instr_ready_queue[j] = temp;
            }
        }
    }
    // find int FU
    if (ready_queue_size > 0) {
        for (i = 0; i < FU_INT_SIZE; i++) {
            if (fuINT[i] == NULL && ready_queue_head < ready_queue_size) {
                fuINT[i] = instr_ready_queue[ready_queue_head];
                ready_queue_head++;
                fuINT[i]->tom_execute_cycle = current_cycle;
                //PRINT_INST(stdout, fuINT[i], "execute_INT: ", current_cycle);
            }
        }
    }
    // FP unit case
    for (i = 0; i < RESERV_INT_SIZE; i++) {
        instr_ready_queue[i] = NULL;
    }
    ready_queue_head = 0;
    ready_queue_size = 0;
    for (i = 0; i < RESERV_FP_SIZE; i++) {
        if (reservFP[i] != NULL &&
            reservFP[i]->Q[0] == NULL &&
            reservFP[i]->Q[1] == NULL &&
            reservFP[i]->Q[2] == NULL &&
            reservFP[i]->tom_execute_cycle == 0) {
            instr_ready_queue[ready_queue_size++] = reservFP[i];
            //PRINT_INST(stdout, reservFP[i], "execute_FP_ready: ", current_cycle);
        }
    }
    // sort ready_queue based on index
    for (i = 0; i < ready_queue_size; i++) {
        int j;
        for (j = 0; j < ready_queue_size - 1; j++) {
            // bubble sort
            if (instr_ready_queue[j]->index > instr_ready_queue[j+1]->index) {
                instruction_t* temp = instr_ready_queue[j+1];
                instr_ready_queue[j+1] = instr_ready_queue[j];
                instr_ready_queue[j] = temp;
            }
        }
    }
    // find fp FU
    if (ready_queue_size > 0) {
        for (i = 0; i < FU_FP_SIZE; i++) {
            if (fuFP[i] == NULL && ready_queue_head < ready_queue_size) {
                fuFP[i] = instr_ready_queue[ready_queue_head];
                ready_queue_head++;
                fuFP[i]->tom_execute_cycle = current_cycle;
                //PRINT_INST(stdout, fuFP[i], "execute_FP: ", current_cycle);
            }
        }
    }
/* ECE552 Assignment 3 -END CODE*/

} 
    
    
/* 
 * Description: 
 * 	Moves instruction(s) from the dispatch stage to the issue stage
 * Inputs:
 * 	current_cycle: the cycle we are at
 * Returns:
 * 	None
 */
void dispatch_To_issue(int current_cycle) {

  /* ECE552: YOUR CODE GOES HERE */
    /* ECE552 Assignment 3 -BEGIN CODE*/

    instruction_t* instr_head = instr_queue[instr_queue_head];

    if (instr_queue_size == 0)
        return;
    // check if the head is branch op
    if (IS_COND_CTRL(instr_head->op) || IS_UNCOND_CTRL(instr_head->op)) {
        // remove it from IFQ but do not issue
        instr_queue[instr_queue_head] = NULL;
        if (instr_queue_head != instr_queue_tail)
            instr_queue_head = (instr_queue_head + 1) % INSTR_QUEUE_SIZE;
        instr_queue_size--;
    } else if (USES_INT_FU(instr_head->op)) {
        // check if reservINT is available
        int i;
        for (i = 0; i < RESERV_INT_SIZE; i++) {
            if (reservINT[i] == NULL) {
                break;
            }
        }

        if (i < RESERV_INT_SIZE) {
            // update issue cycle
            instr_head->tom_issue_cycle = current_cycle; // modify
            // issue it to reservINT
            reservINT[i] = instr_head;
            instr_queue[instr_queue_head] = NULL;
            if (instr_queue_head != instr_queue_tail)
                instr_queue_head = (instr_queue_head + 1) % INSTR_QUEUE_SIZE;
            instr_queue_size--;

            int j;
            // set dependency
            for (j = 0; j < 3; j++) {
                if (reservINT[i]->r_in[j] != DNA) {
                    if (map_table[reservINT[i]->r_in[j]] != NULL) {
                        reservINT[i]->Q[j] = map_table[reservINT[i]->r_in[j]];
                    }
                }
            }
            // set tag and update map_table
            // write to map_table
            for (j = 0; j < 2; j++) {
                if (reservINT[i]->r_out[j] != DNA) {
                    map_table[reservINT[i]->r_out[j]] = reservINT[i];
                }
            }

            
        //PRINT_INST(stdout, instr_head, "issue INT: ", current_cycle);
        }
    } else if (USES_FP_FU(instr_head->op)) {
        // check if reserv FP is available
        int i;
        for (i = 0; i < RESERV_FP_SIZE; i++) {
            if (reservFP[i] == NULL) {
                break;
            }
        }

        if (i < RESERV_FP_SIZE) {
            // update issue cycle
            instr_head->tom_issue_cycle = current_cycle;
            // issue to reservFP
            reservFP[i] = instr_head;
            instr_queue[instr_queue_head] = NULL;
            if (instr_queue_head != instr_queue_tail)
                instr_queue_head = (instr_queue_head + 1) % INSTR_QUEUE_SIZE;
            instr_queue_size--;
            
            int j; 
            // set dependency
            for (j = 0; j < 3; j++) {
                if (reservFP[i]->r_in[j] != DNA) {
                    if (map_table[reservFP[i]->r_in[j]] != NULL) {
                        reservFP[i]->Q[j] = map_table[reservFP[i]->r_in[j]];
                        //PRINT_INST(stdout, reservFP[i]->Q[j], "FP depends on: ", current_cycle);
                    }
                }
            }
            // set tag and update map_table
            // write to map_table
            for (j = 0; j < 2; j++) {
                if (reservFP[i]->r_out[j] != DNA) {
                    map_table[reservFP[i]->r_out[j]] = reservFP[i];
                }
            }

            

           // PRINT_INST(stdout, instr_head, "issue FP: ", current_cycle);
        }
    }
    /* ECE552 Assignment 3 -END CODE*/

}

/* 
 * Description: 
 * 	Grabs an instruction from the instruction trace (if possible)
 * Inputs:
 *      trace: instruction trace with all the instructions executed
 * Returns:
 * 	None
 */
void fetch(instruction_trace_t* trace) {
/* ECE552 Assignment 3 -BEGIN CODE*/

  /* ECE552: YOUR CODE GOES HERE */
    // check if fetch_index bound
    if (fetch_index + 1 > sim_num_insn)
        return;

    //first skip any TRAP instr
    while (IS_TRAP(get_instr(trace, fetch_index + 1)->op)) {
        fetch_index++;
    }
    //check if IFQ is full
    if (instr_queue_size < INSTR_QUEUE_SIZE) {
        instruction_t* instr = get_instr(trace, ++fetch_index);
        // if size is not 0, we need to increment to next slot
        if (instr_queue_size != 0) 
            instr_queue_tail = (instr_queue_tail + 1) % INSTR_QUEUE_SIZE;
        // set Q to NULL
        int i;
        for (i = 0; i < 3; i++)
            instr->Q[i] = NULL;
        instr_queue[instr_queue_tail] = instr;
        instr_queue_size++;
    }
    /* ECE552 Assignment 3 -END CODE*/

}

/* 
 * Description: 
 * 	Calls fetch and dispatches an instruction at the same cycle (if possible)
 * Inputs:
 *      trace: instruction trace with all the instructions executed
 * 	current_cycle: the cycle we are at
 * Returns:
 * 	None
 */
void fetch_To_dispatch(instruction_trace_t* trace, int current_cycle) {

  fetch(trace);

  /* ECE552: YOUR CODE GOES HERE */
    /* ECE552 Assignment 3 -BEGIN CODE*/

  // update dispatch cycle
  // check if we fetch a new instr
    //printf("instr queue size: %d, header %d, tail %d\n", instr_queue_size, instr_queue_head, instr_queue_tail); 
    instruction_t* instr_tail = instr_queue[instr_queue_tail];
    //md_print_insn(instr_tail->inst, instr_tail->pc, stdout);
    if (instr_tail != NULL && instr_tail->tom_dispatch_cycle == 0) {
        //PRINT_INST(stdout, instr_tail, "fetch: ", current_cycle);
        instr_tail->tom_dispatch_cycle = current_cycle;
    }
     /* ECE552 Assignment 3 -END CODE*/

}

/* 
 * Description: 
 * 	Performs a cycle-by-cycle simulation of the 4-stage pipeline
 * Inputs:
 *      trace: instruction trace with all the instructions executed
 * Returns:
 * 	The total number of cycles it takes to execute the instructions.
 * Extra Notes:
 * 	sim_num_insn: the number of instructions in the trace
 */
counter_t runTomasulo(instruction_trace_t* trace)
{
/* ECE552 Assignment 3 -BEGIN CODE*/

  //initialize instruction queue
  int i;
  for (i = 0; i < INSTR_QUEUE_SIZE; i++) {
    instr_queue[i] = NULL;
  }

  //initialize reservation stations
  for (i = 0; i < RESERV_INT_SIZE; i++) {
      reservINT[i] = NULL;
  }

  for(i = 0; i < RESERV_FP_SIZE; i++) {
      reservFP[i] = NULL;
  }

  //initialize functional units
  for (i = 0; i < FU_INT_SIZE; i++) {
    fuINT[i] = NULL;
  }

  for (i = 0; i < FU_FP_SIZE; i++) {
    fuFP[i] = NULL;
  }

  //initialize map_table to no producers
  int reg;
  for (reg = 0; reg < MD_TOTAL_REGS; reg++) {
    map_table[reg] = NULL;
  }
  
  int cycle = 1;
  while (true) {

     /* ECE552: YOUR CODE GOES HERE */
     // in reverse order 
     CDB_To_retire(cycle);
     execute_To_CDB(cycle);
     issue_To_execute(cycle);
     dispatch_To_issue(cycle);
     fetch_To_dispatch(trace, cycle);
     
     cycle++;
     //if (cycle == 30000) break;
     //printf("current_cycle: %d\n", cycle);
     if (is_simulation_done(sim_num_insn))
        break;
  }
  /* ECE552 Assignment 3 -END CODE*/

  return cycle;
}
