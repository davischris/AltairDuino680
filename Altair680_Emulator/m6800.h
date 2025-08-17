#pragma once
#include <stdint.h>

/* Memory */
#define MAXMEMSIZE	65536		// max memory size
#define MEMSIZE		(m6800_unit.capac) // actual memory size
#define ADDRMASK	(MAXMEMSIZE - 1) // address mask
#define MEM_ADDR_OK(x)	(((uint32) (x)) < MEMSIZE)

/* Simulator stop codes */

#define STOP_RSRV	1		// must be 1
#define STOP_HALT	2		// HALT-really WAI
#define STOP_IBKPT	3		// breakpoint
#define STOP_OPCODE	4		// invalid opcode
#define STOP_MEMORY	5		// invalid memory addres

/* function prototypes */
void m6800_reset(void);
int32_t sim_instr(int32_t);
void dump_regs(void);
void dump_regs1(void);
int32_t fetch_byte(int32_t);
int32_t fetch_word(void);
unsigned char pop_byte(void);
uint16_t pop_word(void);
void push_byte(unsigned char);
void push_word(uint16_t);
void go_rel(int32_t);
int32_t get_rel_addr(void);
int32_t get_dir_val(void);
int32_t get_dir_addr(void);
int32_t get_indir_val(void);
int32_t get_indir_addr(void);
int32_t get_ext_val(void);
int32_t get_ext_addr(void);
int32_t get_flag(int32_t);
void condevalVa(int32_t, int32_t, int32_t);
void condevalVs(int32_t, int32_t, int32_t);

/* Flag values to set proper positions in CCR */
#define HF      0x20
#define IF      0x10
#define NF      0x08
#define ZF      0x04
#define VF      0x02
#define CF      0x01

/* Macros to handle the flags in the CCR */
#define CCR_ALWAYS_ON       (0xC0)        /* for 6800 */
#define CCR_MSK (HF|IF|NF|ZF|VF|CF)
#define TOGGLE_FLAG(FLAG)   (CCR ^= FLAG)
#define SET_FLAG(FLAG)      (CCR |= FLAG)
#define CLR_FLAG(FLAG)      (CCR &= ~FLAG)
#define GET_FLAG(FLAG)      (CCR & FLAG)
#define COND_SET_FLAG(COND,FLAG) \
    if (COND) SET_FLAG(FLAG); else CLR_FLAG(FLAG)
#define COND_SET_FLAG_N(VAR) \
    if (VAR & 0x80) SET_FLAG(NF); else CLR_FLAG(NF)
#define COND_SET_FLAG_Z(VAR) \
    if (VAR == 0) SET_FLAG(ZF); else CLR_FLAG(ZF)
#define COND_SET_FLAG_H(V1, V2, V3) \
    if ((V1 ^ V2 ^ V3) & 0x10) SET_FLAG(HF); else CLR_FLAG(HF)
#define COND_SET_FLAG_C(VAR) \
    if (VAR & 0x100) SET_FLAG(CF); else CLR_FLAG(CF)
#define COND_SET_FLAG_V(COND) \
    if (COND) SET_FLAG(VF); else CLR_FLAG(VF)

extern int32_t CCR;
extern int32_t saved_PC;
extern int32_t PC;
extern int32_t TRACE_PC;
extern bool basic_interpreter_running;