
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
static int instr_queue_head = 0;
static int instr_queue_tail = 0;

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
static int funct_int_slot = FU_INT_SIZE;
static int funct_fp_slot = FU_FP_SIZE;

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
static bool is_simulation_done(counter_t sim_insn)
{
	//printf("checking if simulation is done\n");
	int i;
	for(i = 0; i < INSTR_QUEUE_SIZE; i++)
	{
		if(instr_queue[i] != NULL)
		{
			//printf("insn queue not empty\n");
			return false;
		}
	}
	for(i = 0; i < FU_INT_SIZE; i++)
	{
		if(reservINT[i] != NULL)
		{
			//printf("INT reserve station not empty\n");
			return false;
		}
	}
	for(i = 0; i < RESERV_FP_SIZE; i++)
	{
		if(reservFP[i] != NULL)
		{
			//printf("float reserve station not empty\n");
			return false;
		}
	}
	for(i = 0; i < FU_INT_SIZE; i++)
	{
		if(fuINT[i] != NULL)
		{
			//printf("int functional not empty\n");
			return false;
		}
	}
	for(i = 0; i < FU_FP_SIZE; i++)
	{
		if(fuFP[i] != NULL)
		{
			//printf("float point functional not empty\n");
			return false;
		}
	}
	if(commonDataBus != NULL)
		return false;
	return true; //ECE552: you can change this as needed; we've added this so the code provided to you compiles
}

/* 
 * Description: 
 * 	Retires the instruction from writing to the Common Data Bus
 * Inputs:
 * 	current_cycle: the cycle we are at
 * Returns:
 * 	None
 */
void CDB_To_retire(int current_cycle) 
{

  /* check for:
	1. reservation stations for instructions that stall due to RAW hazards (writeback and set the corresponding Q-array to NULL)
	2. map table for matches. if there is a match then clear that map table entry, otherwise that means a subsequent instruction has overwritten that register
  */
	int i;

	// broadcasts the values produced by the instruction to all the RAW hazard stalled instructions
	if(commonDataBus == NULL)
		return;
	if(USES_INT_FU(commonDataBus->op))
	{
		for(i = 0; i < RESERV_INT_SIZE; i++)
		{
			if(reservINT[i] == NULL)
				continue;
			if(reservINT[i]->Q[0] == commonDataBus)
				reservINT[i]->Q[0] = NULL;
			if(reservINT[i]->Q[1] == commonDataBus)
				reservINT[i]->Q[1] = NULL;
			if(reservINT[i]->Q[2] == commonDataBus)
				reservINT[i]->Q[2] = NULL;
		}
		for(i = 0; i < RESERV_FP_SIZE; i++)
		{
			if(reservFP[i] == NULL)
				continue;
			if(reservFP[i]->Q[0] == commonDataBus)
				reservFP[i]->Q[0] = NULL;
			if(reservFP[i]->Q[1] == commonDataBus)
				reservFP[i]->Q[1] = NULL;
			if(reservFP[i]->Q[2] == commonDataBus)
				reservFP[i]->Q[2] = NULL;
		}
	}
	else
	{
		for(i = 0; i < RESERV_INT_SIZE; i++)
		{
			if(reservINT[i] == NULL)
				continue;
			if(reservINT[i]->Q[0] == commonDataBus)
				reservINT[i]->Q[0] = NULL;
			if(reservINT[i]->Q[1] == commonDataBus)
				reservINT[i]->Q[1] = NULL;
			if(reservINT[i]->Q[2] == commonDataBus)
				reservINT[i]->Q[2] = NULL;
		}
		for(i = 0; i < RESERV_FP_SIZE; i++)
		{
			if(reservFP[i] == NULL)
				continue;
			if(reservFP[i]->Q[0] == commonDataBus)
				reservFP[i]->Q[0] = NULL;
			if(reservFP[i]->Q[1] == commonDataBus)
				reservFP[i]->Q[1] = NULL;
			if(reservFP[i]->Q[2] == commonDataBus)
				reservFP[i]->Q[2] = NULL;
		}
	}
	
	//checks for matches in map table entries (clear the entry if there's match, otherwise false RAW hazards will be reported when checking a expired map table entry)
	if(map_table[(commonDataBus->r_out)[0]] == commonDataBus)
		map_table[(commonDataBus->r_out)[0]] = NULL;
	if(map_table[(commonDataBus->r_out)[1]] == commonDataBus)
		map_table[(commonDataBus->r_out)[1]] = NULL;
	PRINT_INST(stdout, commonDataBus, "retiring ", current_cycle);
	commonDataBus = NULL;
}


/* 
 * Description: 
 * 	Moves an instruction from the execution stage to common data bus (if possible)
 * Inputs:
 * 	current_cycle: the cycle we are at
 * Returns:
 * 	None
 */
void execute_To_CDB(int current_cycle) 
{

  /* check for:
	1. if a instruction currently in the functional unit has executed enough cycles
	2. structural hazard for CDB (do not deallocate reservation station and functional unit until insn is ready to enter CDB)
	3. store instruction as they do not need CDB
  */
	if(commonDataBus != NULL)
		return;
	int i,j,k;
	instruction_t *writeback = NULL;
	instruction_t **freeunit = NULL;
	instruction_t *stores[FU_INT_SIZE] = { NULL };
	for(i = 0; i < FU_INT_SIZE; i++)
	{
		if(fuINT[i] != NULL && (current_cycle - (fuINT[i]->tom_execute_cycle) >= FU_INT_LATENCY))
		{
			if(IS_STORE(fuINT[i]->op))
			{
				fuINT[i]->tom_cdb_cycle = 0;
				stores[i] = fuINT[i];
				funct_int_slot = funct_int_slot + 1;
				fuINT[i] = NULL;
			}
			else if(writeback == NULL || writeback->index > fuINT[i]->index)
			{
				j = i;
				freeunit = fuINT;
				writeback = fuINT[i];
			}
		}
	}
	for(i = 0; i < FU_FP_SIZE; i++)
	{
		if(fuFP[i] != NULL && (current_cycle - (fuFP[i]->tom_execute_cycle) >= FU_FP_LATENCY))
		{
			//if(IS_STORE(fuFP[i]->op))
			//	fuFP[i]->tom_cdb_cycle = 0;
			if(writeback == NULL || writeback->index > fuFP[i]->index)
			{
				j = i;
				freeunit = fuFP;
				writeback = fuFP[i];
			}
		}
	}
	
	// free the reservation stations for any completed store instructions
	instruction_t *temp;
	for(i = 0; i < FU_INT_SIZE; i++)
	{
		temp = stores[i];
		if(temp != NULL)
		{
			for(k = 0; k < RESERV_INT_SIZE; k++)
			{
				if(temp == reservINT[k])
				{
					reservINT[k] = NULL;
					break;
				}
			}
		}
		
	}
	if(writeback == NULL)
		return;
	writeback->tom_cdb_cycle = current_cycle;

	//free the functional unit
	assert(writeback == freeunit[j]);
	freeunit[j] = NULL;
	if(USES_INT_FU(writeback->op))
		funct_int_slot = funct_int_slot + 1;
	else
		funct_fp_slot = funct_fp_slot + 1;
	// free the reservation station for the instruction that will enter the CDB
	//instruction_t reserv_station = NULL;
	if(freeunit == fuINT)
	{
		for(i = 0; i < RESERV_INT_SIZE; i++)
		{
			if(writeback == reservINT[i])
			{
				reservINT[i] = NULL;
				break;
			}
		}
	}
	else
	{
		for(i = 0; i < RESERV_FP_SIZE; i++)
		{
			if(writeback == reservFP[i])
			{
				reservFP[i] = NULL;
				break;
			}
		}
	}
	PRINT_INST(stdout, writeback, "writeback ", current_cycle);
	commonDataBus = writeback;
	// check for CDB structural hazard (since the functions are called in reverse order, CDB should already be freed when this function is called)
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
void issue_To_execute(int current_cycle) 
{

  /* check for:
	1. structural hazard due to availability of functional units
	2. all of the data hazards by searching through the map table  */

	int i, j, k = 0;
	if(funct_int_slot > 0)
	{
		instruction_t *sort[FU_INT_SIZE] = {NULL};
		instruction_t *insn;
		for(i = 0; i < RESERV_INT_SIZE; i++)
		{
			insn = reservINT[i];
			if(insn == NULL)
				continue;
			assert(USES_INT_FU(insn->op));
			if(USES_INT_FU(insn->op))
			{
				if((insn->Q)[0] == NULL && (insn->Q)[1] == NULL && (insn->Q)[2] == NULL && insn->tom_execute_cycle == 0)
				{
					//no RAW hazard (data dependency) and that this instruction has NOT been sent to functional unit
					/* needs to check for structural hazard due to functional units*/

					
					// the following code segment keeps track of the oldest and second oldest instruction in the reservation station
					if(sort[0] == NULL || (sort[0]->index) > (insn->index))
					{
						// if found an oldest instruction, make first point to it (initially set first point to the first instruction in RS)
						sort[1] = sort[0];
						sort[0] = insn;
					}
					else if(sort[1] == NULL || (sort[1]->index) > (insn->index))
					{
						sort[1] = insn;
					}
				}
			}
		}
		// find the available function unit(s) and decrement the funct_int_slot counter
		for(j = 0; j < FU_INT_SIZE; j++)
		{
			if(fuINT[j] == NULL && sort[k] != NULL)
			{
				fuINT[j] = sort[k];
				fuINT[j]->tom_execute_cycle = current_cycle;
				PRINT_INST(stdout, fuINT[j], "start executing ", current_cycle);
				funct_int_slot = funct_int_slot - 1;
				k = k + 1;
			}
		}
	}
	if(funct_fp_slot > 0)
	{
		instruction_t *insn;
		instruction_t *oldest = NULL;
		for(i = 0; i < RESERV_FP_SIZE; i++)
		{
			insn = reservFP[i];
			if(insn == NULL)
				continue;
			if((insn->Q)[0] == NULL && (insn->Q)[1] == NULL && (insn->Q)[2] == NULL && insn->tom_execute_cycle == 0)
			{
				//no RAW hazard (data dependency)
				// AND this instruction has NOT been sent to functional unit
				/* needs to check for structural hazard due to functional units*/

				// the following code segment keeps track of the oldest and second oldest instruction in the reservation station
				if(oldest == NULL || insn->index > oldest->index)
				{
					//index = i;
					oldest = insn;
				}
			}
		}
		if(oldest != NULL)
		{
			assert(fuFP[0] == NULL);
			
			fuFP[0] = oldest;
			oldest->tom_execute_cycle = current_cycle;
			PRINT_INST(stdout, fuFP[0], "start executing ", current_cycle);
			funct_fp_slot = funct_fp_slot - 1;
		}
	}
}

/* 
 * Description: 
 * 	Moves instruction(s) from the dispatch stage to the issue stage
 * Inputs:
 * 	current_cycle: the cycle we are at
 * Returns:
 * 	None
 */
void dispatch_To_issue(int current_cycle) 
{
	/* ECE552: YOUR CODE GOES HERE */
	/* check:
		1. the current insn is not a branch
		2. no structural hazard due to unavailable reservation station
		3. map table for its source registers
	   update:
		1. map table for target register
	*/
	int i;
	instruction_t *head = instr_queue[instr_queue_head];
	if(head == NULL)
	{
		assert(instr_queue_size == 0);
		return;
	}


	if(!IS_UNCOND_CTRL(instr_queue[instr_queue_head] -> op) && !IS_COND_CTRL(instr_queue[instr_queue_head] -> op))
	{
		//instruction_t *head = instr_queue[instr_queue_head];
		if(USES_INT_FU(head -> op))
		{
			for(i = 0; i < RESERV_INT_SIZE; i++)
			{
				// check integer reservation station availability
				if(reservINT[i] == NULL)
				{
					reservINT[i] = head;
					head->tom_issue_cycle = current_cycle;
PRINT_INST(stdout, head, "issue to RS ", current_cycle);
					// check for source register matches in map table and if found store into Q-array to take note that it will NOT come from physical registers
					instruction_t *src;
					if(head->r_in[0] != DNA)
					{
						if((src = map_table[(head->r_in)[0]]) != NULL)
						{
							assert(src != NULL);
							(head->Q)[0] = src;
						}
					}
					if(head->r_in[1] != DNA)
					{
						if((src = map_table[(head->r_in)[1]]) != NULL)
						{
							assert(src != NULL);
							(head->Q)[1] = src;
						}
					}
					if(head->r_in[2] != DNA)
					{
						if((src = map_table[(head->r_in)[2]]) != NULL)
						{
							assert(src != NULL);
							(head->Q)[2] = src;
						}
					}

					// updates the map table with its own target registers
					if(head->r_out[0] != DNA)
						map_table[(head->r_out)[0]] = head;
					if(head->r_out[1] != DNA)
						map_table[(head->r_out)[1]] = head;
					break;
				}
			}
			if(i >= RESERV_INT_SIZE)
				return;
		}
		else
		{
			for(i = 0; i < RESERV_FP_SIZE; i++)
			{
				// check floating point reservation station availability
				// check integer reservation station availability
				if(reservFP[i] == NULL)
				{
					reservFP[i] = head;
					head->tom_issue_cycle = current_cycle;

PRINT_INST(stdout, head, "issue to RS ", current_cycle);
					// check for source register matches in map table and if found store into Q-array to take note that it will NOT come from physical registers
					instruction_t *src;
					if(head->r_in[0] != DNA)
					{
						if((src = map_table[(head->r_in)[0]]) != NULL)
							(head->Q)[0] = src;
					}
					if(head->r_in[1] != DNA)
					{
						if((src = map_table[(head->r_in)[1]]) != NULL)
							(head->Q)[1] = src;
					}
					if(head->r_in[2] != DNA)
					{
						if((src = map_table[(head->r_in)[2]]) != NULL)
							(head->Q)[2] = src;
					}

					// updates the map table with its own target registers
					if(head->r_out[0] != DNA)
						map_table[(head->r_out)[0]] = head;
					if(head->r_out[1] != DNA)
						map_table[(head->r_out)[1]] = head;
					break;
				}
			}
			if(i >= RESERV_FP_SIZE)
				return;
		}
	}
	// if reached this point that means an available reservation station has been found
	// remove the head from the instruction queue and update the head index
	instr_queue[instr_queue_head] = NULL;
	PRINT_INST(stdout, head, "removing this instruction from queue: ", current_cycle);
	if(instr_queue_head != instr_queue_tail)
	{
		assert(instr_queue_size > 1);
		
		instr_queue_head = (instr_queue_head + 1) % INSTR_QUEUE_SIZE;
	}
	instr_queue_size--;
}

/* 
 * Description: 
 * 	Grabs an instruction from the instruction trace (if possible)
 * Inputs:
 *      trace: instruction trace with all the instructions executed
 * Returns:
 * 	None
 */
void fetch(instruction_trace_t* trace) 
{
  if(fetch_index >= sim_num_insn)
	return;
  if(instr_queue_size < INSTR_QUEUE_SIZE)			//if there is an available slot in instruction queue (no structural hazard)
  {
	instruction_t* insn;
	do
	{
		fetch_index = fetch_index + 1;
		if(fetch_index >= sim_num_insn)
			return;
		insn = get_instr(trace, fetch_index);
	}
	while(IS_TRAP(insn -> op));

	if(instr_queue_size > 0)
	{
		//if the instruction queue is empty then no need to update instr_queue_tail
		instr_queue_tail = (instr_queue_tail + 1) % INSTR_QUEUE_SIZE;
	}
	instr_queue[instr_queue_tail] = insn;
	
	instr_queue_size = instr_queue_size + 1;
  }
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
void fetch_To_dispatch(instruction_trace_t* trace, int current_cycle)
{
	fetch(trace);
	/* ECE552: YOUR CODE GOES HERE */
	char buffer [50];
	sprintf(buffer, "%d", instr_queue_tail);
	//PRINT_INST(stdout, instr_queue[instr_queue_tail], buffer, current_cycle);
	if(instr_queue_size != 0 && (instr_queue[instr_queue_tail])->tom_dispatch_cycle == 0)
	{
		PRINT_INST(stdout, instr_queue[instr_queue_tail], "fetched instruction at ", current_cycle);
		(instr_queue[instr_queue_tail])->tom_dispatch_cycle = current_cycle;
	}
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
  
  int cycle = 1;	//total cycles
  while (true) 
  {

	/* ECE552: YOUR CODE GOES HERE */
	// making calls to each function that performs tasks that will be happening at each cycle
	CDB_To_retire(cycle);
	execute_To_CDB(cycle);
	issue_To_execute(cycle);
	dispatch_To_issue(cycle);
	fetch_To_dispatch(trace, cycle);
	cycle++;

	if (is_simulation_done(sim_num_insn))
	break;
  }
  
  return cycle;
}
