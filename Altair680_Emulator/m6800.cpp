/* 
 *  191231 - jbn - bug fixes:
 *    1. DAA implementation corrected, based on IA-32 architecture spec.
 *    2. H flag now set/clear correctly.
 *    3. condevalVa() now correctly sets the V flag. 
 *    4. condevalVs() now correctly sets the V flag. 
 *    5. correct SWI (0x3f) vector fetch address; was 0xfffb, is actually 0xfffa.
 *    
 *    J Nichols (vesinet78110@gmail.com)
*/

// JBN_DAA is correct, original DAA code otherwise
#define JBN_DAA

#include <stdio.h>

#include "SWTPC.h"
#include "m6800.h"
#include <Arduino.h>


/* m6800.c: SWTP 6800 CPU simulator

   Copyright (c) 2005-2011, William Beech

   Permission is hereby granted, free of charge, to any person obtaining a
   copy of this software and associated documentation files (the "Software"),
   to deal in the Software without restriction, including without limitation
   the rights to use, copy, modify, merge, publish, distribute, sublicense,
   and/or sell copies of the Software, and to permit persons to whom the
   Software is furnished to do so, subject to the following conditions:

   The above copyright notice and this permission notice shall be included in
   all copies or substantial portions of the Software.

   THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
   IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
   FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
   WILLIAM A. BEECH BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
   IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
   CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

   Except as contained in this notice, the name of William A. Beech shall not
   be used in advertising or otherwise to promote the sale, use or other dealings
   in this Software without prior written authorization from William A. Beech.

   cpu                  Motorola M6800 CPU

   The register state for the M6800 CPU is:

   A<0:7>               Accumulator A
   B<0:7>               Accumulator B
   IX<0:15>             Index Register
   CCR<0:7>             Condition Code Register
       HF                   half-carry flag
       IF                   interrupt flag
       NF                   negative flag
       ZF                   zero flag
       VF                   overflow flag
       CF                   carry flag
   PC<0:15>             program counter
   SP<0:15>             Stack Pointer

   The M6800 is an 8-bit CPU, which uses 16-bit registers to address
   up to 64KB of memory.

   The 72 basic instructions come in 1, 2, and 3-byte flavors.

   This routine is the instruction decode routine for the M6800.
   It is called from the CPU board simulator to execute
   instructions in simulated memory, starting at the simulated PC.
   It runs until 'reason' is set non-zero.

   General notes:

   1. Reasons to stop.  The simulator can be stopped by:

        WAI instruction
        I/O error in I/O simulator
        Invalid OP code (if ITRAP is set on CPU)
        Invalid mamory address (if MTRAP is set on CPU)

   2. Interrupts.
      There are 4 types of interrupt, and in effect they do a 
      hardware CALL instruction to one of 4 possible high memory addresses.

   3. Non-existent memory.  
        On the SWTP 6800, reads to non-existent memory
        return 0FFH, and writes are ignored.
*/

/* local global variables */

int32_t A = 0;                            /* Accumulator A */
int32_t B = 0;                            /* Accumulator B */
int32_t IX = 0;                           /* Index register */
int32_t SP = 0;                           /* Stack pointer */
int32_t CCR = CCR_ALWAYS_ON | IF;         /* Condition Code Register */
int32_t saved_PC = 0;                     /* Program counter */
int32_t PC;                               /* global for the helper routines */
int32_t INTE = 0;                         /* Interrupt Enable */
int32_t int_req = 0;                      /* Interrupt request */

int32_t mem_fault = 0;                    /* memory fault flag */

extern uint8_t RAM_0000_BFFF[];

extern int32_t TRACE_PC;
extern void trace(int32_t PC, char *opcode, int32_t CCR, int32_t B, int32_t A, int32_t IX, int32_t SP);
// extern int32_t CPU_BD_get_mword(int32_t addr);
// extern void CPU_BD_put_mword(int32_t addr, int32_t val);
// extern void WriteToConsole(char* Text);

const char *opcode[] = {
//static const char *opcode[] = {
"???", "NOP", "???", "???",             //0x00
"???", "???", "TAP", "TPA",
"INX", "DEX", "CLV", "SEV",
"CLC", "SEC", "CLI", "SEI",
"SBA", "CBA", "???", "???",             //0x10
"???", "???", "TAB", "TBA",
"???", "DAA", "???", "ABA",
"???", "???", "???", "???",
"BRA", "???", "BHI", "BLS",             //0x20
"BCC", "BCS", "BNE", "BEQ",
"BVC", "BVS", "BPL", "BMI",
"BGE", "BLT", "BGT", "BLE",
"TSX", "INS", "PULA", "PULB",           //0x30
"DES", "TXS", "PSHA", "PSHB",
"???", "RTS", "???", "RTI",
"???", "???", "WAI", "SWI",
"NEGA", "???", "???", "COMA",           //0x40
"LSRA", "???", "RORA", "ASRA",
"ASLA", "ROLA", "DECA", "???",
"INCA", "TSTA", "???", "CLRA",
"NEGB", "???", "???", "COMB",           //0x50
"LSRB", "???", "RORB", "ASRB",
"ASLB", "ROLB", "DECB", "???",
"INCB", "TSTB", "???", "CLRB",
"NEG", "???", "???", "COM",             //0x60
"LSR", "???", "ROR", "ASR",
"ASL", "ROL", "DEC", "???",
"INC", "TST", "JMP", "CLR",
"NEG", "???", "???", "COM",             //0x70
"LSR", "???", "ROR", "ASR",
"ASL", "ROL", "DEC", "???",
"INC", "TST", "JMP", "CLR",
"SUBA", "CMPA", "SBCA", "???",          //0x80
"ANDA", "BITA", "LDAA", "???",
"EORA", "ADCA", "ORAA", "ADDA",
"CPX", "BSR", "LDS", "???",
"SUBA", "CMPA", "SBCA", "???",          //0x90
"ANDA", "BITA", "LDAA", "STAA",
"EORA", "ADCA", "ORAA", "ADDA",
"CPX", "???", "LDS", "STS",
"SUBA", "CMPA", "SBCA", "???",          //0xA0
"ANDA", "BITA", "LDAA", "STAA",
"EORA", "ADCA", "ORAA", "ADDA",
"CPX X", "JSR X", "LDS X", "STS X",
"SUBA", "CMPA", "SBCA", "???",          //0xB0
"ANDA", "BITA", "LDAA", "STAA",
"EORA", "ADCA", "ORAA", "ADDA",
"CPX", "JSR", "LDS", "STS",
"SUBB", "CMPB", "SBCB", "???",          //0xC0
"ANDB", "BITB", "LDAB", "???",
"EORB", "ADCB", "ORAB", "ADDB",
"???", "???", "LDX", "???",
"SUBB", "CMPB", "SBCB", "???",          //0xD0
"ANDB", "BITB", "LDAB", "STAB",
"EORB", "ADCB", "ORAB", "ADDB",
"???", "???", "LDX", "STX",
"SUBB", "CMPB", "SBCB", "???",          //0xE0
"ANDB", "BITB", "LDAB", "STAB",
"EORB", "ADCB", "ORAB", "ADDB",
"???", "???", "LDX", "STX",
"SUBB", "CMPB", "SBCB", "???",          //0xF0
"ANDB", "BITB", "LDAB", "STAB",
"EORB", "ADCB", "ORAB", "ADDB",
"???", "???", "LDX", "STX",
};

/*
int32_t oplen[256] = {
0,1,0,0,0,0,1,1,1,1,1,1,1,1,1,1,        //0x00
1,1,0,0,0,0,1,1,0,1,0,1,0,0,0,0,
2,0,2,2,2,2,2,2,2,2,2,2,2,2,2,2,
1,1,1,1,1,1,1,1,0,1,0,1,0,0,1,1,
1,0,0,1,1,0,1,1,1,1,1,0,1,1,0,1,        //0x40
1,0,0,1,1,0,1,1,1,1,1,0,1,1,0,1,
2,0,0,2,2,0,2,2,2,2,2,0,2,2,2,2,
3,0,0,3,3,0,3,3,3,3,3,0,3,3,3,3,
2,2,2,0,2,2,2,0,2,2,2,2,3,2,3,0,        //0x80
2,2,2,0,2,2,2,2,2,2,2,2,2,0,2,2,
2,2,2,0,2,2,2,2,2,2,2,2,2,2,2,2,
3,3,3,0,3,3,3,3,3,3,3,3,3,3,3,3,
2,2,2,0,2,2,2,0,2,2,2,2,0,0,3,0,        //0xC0
2,2,2,0,2,2,2,2,2,2,2,2,0,0,2,2,
2,2,2,0,2,2,2,2,2,2,2,2,0,0,2,2,
3,3,3,0,3,3,3,3,3,3,3,3,0,0,3,3
};
*/

int32_t sim_instr(int32_t sim_interval)
{
    int32_t IR, OP, DAR, reason, hi, lo, op1, tmp1, tmp2;
    
    PC = saved_PC & ADDRMASK;           /* load local PC */
    reason = 0;

    /* Main instruction fetch/decode loop */
    delay(1);
    while (reason == 0) {               /* loop until halted */
//    dump_regs1();
        if (sim_interval <= 0)          /* check clock queue */
/*            if (reason = sim_process_event ()) 
                break; */                
            if (mem_fault) {            /* memory fault? */
                mem_fault = 0;          /* reset fault flag */
                reason = STOP_MEMORY;
                break;
            }
        if (int_req > 0) {              /* interrupt? */
        /* 6800 interrupts not implemented yet.  None were used,
           on a standard SWTP 6800. All I/O is programmed. */
        }                               /* end interrupt */
/*        if (sim_brk_summ &&
            sim_brk_test (PC, SWMASK ('E'))) { // breakpoint?
            reason = STOP_IBKPT;               // stop simulation
            break;
        }  */

        if (PC < 0x0000 || PC > 0xFFFF) {
            Serial.print("Trap: PC out of bounds (");
            Serial.print(PC, HEX);
            Serial.println(") — halting.");

            for (int i = 0x01FF; i > 0x01E0; i -= 2) {
                uint8_t lo = RAM_0000_BFFF[i];
                uint8_t hi = RAM_0000_BFFF[i - 1];
                uint16_t addr = (hi << 8) | lo;
                Serial.print("Stack @ 0x");
                Serial.print(i - 1, HEX);
                Serial.print(": ");
                Serial.print(hi, HEX); Serial.print(" ");
                Serial.print(lo, HEX); Serial.print(" = ");
                Serial.println(addr, HEX);
            }

            return STOP_OPCODE;
        }

        IR = OP = fetch_byte(0);        /* fetch instruction */
        sim_interval--;

        /* The Big Instruction Decode Switch */

        switch (IR) {

            case 0x01:                  /* NOP */
                break;
            case 0x06:                  /* TAP */
                CCR = A;
                break;
            case 0x07:                  /* TPA */
                A = CCR;
                break;
            case 0x08:                  /* INX */
                IX = (IX + 1) & ADDRMASK;
                COND_SET_FLAG_Z(IX);
                break;
            case 0x09:                  /* DEX */
                IX = (IX - 1) & ADDRMASK;
                COND_SET_FLAG_Z(IX);
                break;
            case 0x0A:                  /* CLV */
                CLR_FLAG(VF);
                break;
            case 0x0B:                  /* SEV */
                SET_FLAG(VF);
                break;
            case 0x0C:                  /* CLC */
                CLR_FLAG(CF);
                break;
            case 0x0D:                  /* SEC */
                SET_FLAG(CF);
                break;
            case 0x0E:                  /* CLI */
                CLR_FLAG(IF);
                break;
            case 0x0F:                  /* SEI */
                SET_FLAG(IF);
                break;
            case 0x10:                  /* SBA */
                op1 = A;
                A = A - B;
                COND_SET_FLAG_N(A);
                COND_SET_FLAG_Z(A);
                COND_SET_FLAG_C(A);
                condevalVs(A, op1, B);
                A &= 0xFF;
                break;
            case 0x11:                  /* CBA */
                lo = A - B;
                COND_SET_FLAG_N(lo);
                COND_SET_FLAG_Z(lo);
                COND_SET_FLAG_C(lo);
                condevalVs(lo, A, B);
                break;
            case 0x16:                  /* TAB */
                B = A;
                COND_SET_FLAG_N(B);
                COND_SET_FLAG_Z(B);
                CLR_FLAG(VF);
                break;
            case 0x17:                  /* TBA */
                A = B;
                COND_SET_FLAG_N(B);
                COND_SET_FLAG_Z(B);
                CLR_FLAG(VF);
                break;
           case 0x19:                  /* DAA */
#ifdef JBN_DAA
                {
                  int32_t old_A, old_cf;
                  int32_t cf = 0;

                  old_A = A, old_cf = get_flag(CF);
                  cf = 0;

                  if ((A & 0x0f) > 9  ||  get_flag(HF)) {
                    A += 6;
                    COND_SET_FLAG_C(old_cf || A & 0x100);
                    SET_FLAG(HF);
                  } else {
                    CLR_FLAG(HF);
                  }

                  if (old_A > 0x99 || old_cf) {
                    A += 0x60;
                    SET_FLAG(CF);
                  } else {
                    CLR_FLAG(CF);
                  }

                  A &= 0xff;
                }
#else
                DAR = A & 0x0F;
                op1 = get_flag(CF);
                if (DAR > 9 || get_flag(CF)) {
                    DAR += 6;
                    A &= 0xF0;
                    A |= (DAR & 0x0F);
                        COND_SET_FLAG(DAR & 0x10,CF);
                }
                DAR = (A >> 4) & 0x0F;
                if (DAR > 9 || get_flag(CF)) {
                    DAR += 6;
                    if (get_flag(CF)) 
                        DAR++;
                    A &= 0x0F;
                    A |= (DAR << 4);
                }
                COND_SET_FLAG(op1,CF);
                if ((DAR << 4) & 0x100)
                    SET_FLAG(CF);
                COND_SET_FLAG_N(A);
                COND_SET_FLAG_Z(A);
                A &= 0xFF;
#endif
                break;
            case 0x1B:                  /* ABA */
                tmp1 = A;
                A += B;
                COND_SET_FLAG_H(A, tmp1, B);
                COND_SET_FLAG_N(A);
                COND_SET_FLAG_Z(A);
                COND_SET_FLAG_C(A);
                condevalVa(A, tmp1, B);
                A &= 0xFF;
                break;
            case 0x20:                  /* BRA rel */
                go_rel(1);
                break;
            case 0x22:                  /* BHI rel */
                go_rel(!(get_flag(CF) | get_flag(ZF)));
                break;
            case 0x23:                  /* BLS rel */
                go_rel(get_flag(CF) | get_flag(ZF));
                break;
            case 0x24:                  /* BCC rel */
                go_rel(!get_flag(CF));
                break;
            case 0x25:                  /* BCS rel */
                go_rel(get_flag(CF));
                break;
            case 0x26:                  /* BNE rel */
                go_rel(!get_flag(ZF));
                break;
            case 0x27:                  /* BEQ rel */
                go_rel(get_flag(ZF));
                break;
            case 0x28:                  /* BVC rel */
                go_rel(!get_flag(VF));
                break;
            case 0x29:                  /* BVS rel */
                go_rel(get_flag(VF));
                break;
            case 0x2A:                  /* BPL rel */
                go_rel(!get_flag(NF));
                break;
            case 0x2B:                  /* BMI rel */
                go_rel(get_flag(NF));
                break;
            case 0x2C:                  /* BGE rel */
                go_rel(!(get_flag(NF) ^ get_flag(VF)));
                break;
            case 0x2D:                  /* BLT rel */
                go_rel(get_flag(NF) ^ get_flag(VF));
                break;
            case 0x2E:                  /* BGT rel */
                go_rel(!(get_flag(ZF) | (get_flag(NF) ^ get_flag(VF))));
                break;
            case 0x2F:                  /* BLE rel */
                go_rel(get_flag(ZF) | (get_flag(NF) ^ get_flag(VF)));
                break;
            case 0x30:                  /* TSX */
                IX = (SP + 1) & ADDRMASK;
                break;
            case 0x31:                  /* INS */
                SP = (SP + 1) & ADDRMASK;
                break;
            case 0x32:                  /* PUL A */
                A = pop_byte();
                break;
            case 0x33:                  /* PUL B */
                B = pop_byte();
                break;
            case 0x34:                  /* DES */
                SP = (SP - 1) & ADDRMASK;
                break;
            case 0x35:                  /* TXS */
                SP = (IX - 1) & ADDRMASK;
                break;
            case 0x36:                  /* PSH A */
                push_byte(A);
                break;
            case 0x37:                  /* PSH B */
                push_byte(B);
                break;
            case 0x39:                  /* RTS */
                PC = pop_word();
                break;
            case 0x3B:                  /* RTI */
                CCR = pop_byte();
                B = pop_byte();
                A = pop_byte();
                IX = pop_word();
                PC = pop_word();
                break;
            case 0x3E:                  /* WAI */
                push_word(PC);
                push_word(IX);
                push_byte(A);
                push_byte(B);
                push_byte(CCR);
                if (get_flag(IF)) {
                    reason = STOP_HALT;
                    continue;
                } else {
                    SET_FLAG(IF);
                    PC = CPU_BD_get_mword(0xFFFE) & ADDRMASK;
                }
                break;
            case 0x3F:                  /* SWI */
                push_word(PC);
                push_word(IX);
                push_byte(A);
                push_byte(B);
                push_byte(CCR);
                SET_FLAG(IF);
                PC = CPU_BD_get_mword(0xFFFA) & ADDRMASK; // JBN was 0xFFFB
                break;
            case 0x40:                  /* NEG A */
                A = (0 - A) & 0xFF;
                COND_SET_FLAG_V(A == 0x80);  // JBN was &
                COND_SET_FLAG(A,CF);
                COND_SET_FLAG_N(A);
                COND_SET_FLAG_Z(A);
                break;
            case 0x43:                  /* COM A */
                A = ~A & 0xFF;
                CLR_FLAG(VF);
                SET_FLAG(CF);
                COND_SET_FLAG_N(A);
                COND_SET_FLAG_Z(A);
                break;
            case 0x44:                  /* LSR A */
                COND_SET_FLAG(A & 0x01,CF);
                A = (A >> 1) & 0xFF;
                CLR_FLAG(NF);
                COND_SET_FLAG_Z(A);
                COND_SET_FLAG_V(get_flag(NF) ^ get_flag(CF));
                break;
            case 0x46:                  /* ROR A */
                hi = get_flag(CF);
                COND_SET_FLAG(A & 0x01,CF);
                A = (A >> 1) & 0xFF;
                if (hi)
                    A |= 0x80;
                COND_SET_FLAG_N(A);
                COND_SET_FLAG_Z(A);
                COND_SET_FLAG_V(get_flag(NF) ^ get_flag(CF));
                break;
            case 0x47:                  /* ASR A */
                COND_SET_FLAG(A & 0x01,CF);
                lo = A & 0x80;
                A = (A >> 1) & 0xFF;
                A |= lo; 
                COND_SET_FLAG_N(A);
                COND_SET_FLAG_Z(A);
                COND_SET_FLAG_V(get_flag(NF) ^ get_flag(CF));
                break;
            case 0x48:                  /* ASL A */
                COND_SET_FLAG(A & 0x80,CF);
                A = (A << 1) & 0xFF;
                COND_SET_FLAG_N(A);
                COND_SET_FLAG_Z(A);
                COND_SET_FLAG_V(get_flag(NF) ^ get_flag(CF));
                break;
            case 0x49:                  /* ROL A */
                hi = get_flag(CF);
                COND_SET_FLAG(A & 0x80,CF);
                A = (A << 1) & 0xFF;
                if (hi)
                    A |= 0x01;
                COND_SET_FLAG_N(A);
                COND_SET_FLAG_Z(A);
                COND_SET_FLAG_V(get_flag(NF) ^ get_flag(CF));
                break;
            case 0x4A:                  /* DEC A */
                COND_SET_FLAG_V(A == 0x80);
                A = (A - 1) & 0xFF;
                COND_SET_FLAG_N(A);
                COND_SET_FLAG_Z(A);
                break;
            case 0x4C:                  /* INC A */
                COND_SET_FLAG_V(A == 0x7F);
                A = (A + 1) & 0xFF;
                COND_SET_FLAG_N(A);
                COND_SET_FLAG_Z(A);
                break;
            case 0x4D:                  /* TST A */
                lo = (A - 0) & 0xFF;
                CLR_FLAG(VF);
                CLR_FLAG(CF);
                COND_SET_FLAG_N(lo);
                COND_SET_FLAG_Z(lo);
                break;
            case 0x4F:                  /* CLR A */
                A = 0;
                CLR_FLAG(NF);
                CLR_FLAG(VF);
                CLR_FLAG(CF);
                SET_FLAG(ZF);
                break;
            case 0x50:                  /* NEG B */
                B = (0 - B) & 0xFF;
                COND_SET_FLAG_V(B == 0x80);  // JBN was &
                COND_SET_FLAG(B,CF);
                COND_SET_FLAG_N(B);
                COND_SET_FLAG_Z(B);
                break;
            case 0x53:                  /* COM B */
                B = ~B;
                B &= 0xFF;
                CLR_FLAG(VF);
                SET_FLAG(CF);
                COND_SET_FLAG_N(B);
                COND_SET_FLAG_Z(B);
                break;
            case 0x54:                  /* LSR B */
                COND_SET_FLAG(B & 0x01,CF);
                B = (B >> 1) & 0xFF;
                CLR_FLAG(NF);
                COND_SET_FLAG_Z(B);
                COND_SET_FLAG_V(get_flag(NF) ^ get_flag(CF));
                break;
            case 0x56:                  /* ROR B */
                hi = get_flag(CF);
                COND_SET_FLAG(B & 0x01,CF);
                B = (B >> 1) & 0xFF;
                if (hi)
                    B |= 0x80;
                COND_SET_FLAG_N(B);
                COND_SET_FLAG_Z(B);
                COND_SET_FLAG_V(get_flag(NF) ^ get_flag(CF));
                break;
            case 0x57:                  /* ASR B */
                COND_SET_FLAG(B & 0x01,CF);
                lo = B & 0x80;
                B = (B >> 1) & 0xFF;
                B |= lo; 
                COND_SET_FLAG_N(B);
                COND_SET_FLAG_Z(B);
                COND_SET_FLAG_V(get_flag(NF) ^ get_flag(CF));
                break;
            case 0x58:                  /* ASL B */
                COND_SET_FLAG(B & 0x80,CF);
                B = (B << 1) & 0xFF;
                COND_SET_FLAG_N(B);
                COND_SET_FLAG_Z(B);
                COND_SET_FLAG_V(get_flag(NF) ^ get_flag(CF));
                break;
            case 0x59:                  /* ROL B */
                hi = get_flag(CF);
                COND_SET_FLAG(B & 0x80,CF);
                B = (B << 1) & 0xFF;
                if (hi)
                    B |= 0x01;
                COND_SET_FLAG_N(B);
                COND_SET_FLAG_Z(B);
                COND_SET_FLAG_V(get_flag(NF) ^ get_flag(CF));
                break;
            case 0x5A:                  /* DEC B */
                COND_SET_FLAG_V(B == 0x80);
                B = (B - 1) & 0xFF;
                COND_SET_FLAG_N(B);
                COND_SET_FLAG_Z(B);
                break;
            case 0x5C:                  /* INC B */
                COND_SET_FLAG_V(B == 0x7F);
                B = (B + 1) & 0xFF;
                COND_SET_FLAG_N(B);
                COND_SET_FLAG_Z(B);             
		break;
            case 0x5D:                  /* TST B */
                lo = (B - 0) & 0xFF;
                CLR_FLAG(VF);
                CLR_FLAG(CF);
                COND_SET_FLAG_N(lo);
                COND_SET_FLAG_Z(lo);
                break;
            case 0x5F:                  /* CLR B */
                B = 0;
                CLR_FLAG(NF);
                CLR_FLAG(VF);
                CLR_FLAG(CF);
                SET_FLAG(ZF);
                break;
            case 0x60:                  /* NEG ind */
                DAR = get_indir_addr();
                lo = (0 - CPU_BD_get_mbyte(DAR)) & 0xFF;
                CPU_BD_put_mbyte(DAR, lo);
                COND_SET_FLAG_V(lo == 0x80);  // JBN was &
                COND_SET_FLAG(lo,CF);
                COND_SET_FLAG_N(lo);
                COND_SET_FLAG_Z(lo);
                break;
            case 0x63:                  /* COM ind */
                DAR = get_indir_addr();
                lo = ~CPU_BD_get_mbyte(DAR);
                lo &= 0xFF;
                CPU_BD_put_mbyte(DAR, lo);
                CLR_FLAG(VF);
                SET_FLAG(CF);
                COND_SET_FLAG_N(lo);
                COND_SET_FLAG_Z(lo);
                break;
            case 0x64:                  /* LSR ind */
                DAR = get_indir_addr();
                lo = CPU_BD_get_mbyte(DAR);
                COND_SET_FLAG(lo & 0x01,CF);
                lo >>= 1;
                CPU_BD_put_mbyte(DAR, lo);
                CLR_FLAG(NF);
                COND_SET_FLAG_Z(lo);
                COND_SET_FLAG_V(get_flag(NF) ^ get_flag(CF));
                break;
            case 0x66:                  /* ROR ind */
                DAR = get_indir_addr();
                lo = CPU_BD_get_mbyte(DAR);
                hi = get_flag(CF);
                COND_SET_FLAG(lo & 0x01,CF);
                lo >>= 1;
                if (hi)
                    lo |= 0x80;
                CPU_BD_put_mbyte(DAR, lo);
                COND_SET_FLAG_N(lo);
                COND_SET_FLAG_Z(lo);
                COND_SET_FLAG_V(get_flag(NF) ^ get_flag(CF));
                break;
            case 0x67:                  /* ASR ind */
                DAR = get_indir_addr();
                lo = CPU_BD_get_mbyte(DAR);
                COND_SET_FLAG(lo & 0x01,CF);
                lo = (lo & 0x80) | (lo >> 1);
                CPU_BD_put_mbyte(DAR, lo);
                COND_SET_FLAG_N(lo);
                COND_SET_FLAG_Z(lo);
                COND_SET_FLAG_V(get_flag(NF) ^ get_flag(CF));
                break;
            case 0x68:                  /* ASL ind */
                DAR = get_indir_addr();
                lo = CPU_BD_get_mbyte(DAR);
                COND_SET_FLAG(lo & 0x80,CF);
                lo <<= 1;
                CPU_BD_put_mbyte(DAR, lo);
                COND_SET_FLAG_N(lo);
                COND_SET_FLAG_Z(lo);
                COND_SET_FLAG_V(get_flag(NF) ^ get_flag(CF));
                break;
            case 0x69:                  /* ROL ind */
                DAR = get_indir_addr();
                lo = CPU_BD_get_mbyte(DAR);
                hi = get_flag(CF);
                COND_SET_FLAG(lo & 0x80,CF);
                lo <<= 1;
                if (hi)
                    lo |= 0x01;
                CPU_BD_put_mbyte(DAR, lo);
                COND_SET_FLAG_N(lo);
                COND_SET_FLAG_Z(lo);
                COND_SET_FLAG_V(get_flag(NF) ^ get_flag(CF));
                break;
            case 0x6A:                  /* DEC ind */
                DAR = get_indir_addr();
                lo = CPU_BD_get_mbyte(DAR);
                COND_SET_FLAG_V(lo == 0x80);
                lo = (lo - 1) & 0xFF;
                CPU_BD_put_mbyte(DAR, lo);
                COND_SET_FLAG_N(lo);
                COND_SET_FLAG_Z(lo);
                break;
            case 0x6C:                  /* INC ind */
                DAR= get_indir_addr();
                lo = CPU_BD_get_mbyte(DAR);
                COND_SET_FLAG_V(lo == 0x7F);
                lo = (lo + 1) & 0xFF;
                CPU_BD_put_mbyte(DAR, lo);
                COND_SET_FLAG_N(lo);
                COND_SET_FLAG_Z(lo);          
                break;
            case 0x6D:                  /* TST ind */
                lo = (get_indir_val() - 0) & 0xFF;
                CLR_FLAG(VF);
                CLR_FLAG(CF);
                COND_SET_FLAG_N(lo);
                COND_SET_FLAG_Z(lo);
                break;
            case 0x6E:                  /* JMP ind */
                PC = get_indir_addr();
                break;
            case 0x6F:                  /* CLR ind */
                CPU_BD_put_mbyte(get_indir_addr(), 0);
                CLR_FLAG(NF);
                CLR_FLAG(VF);
                CLR_FLAG(CF);
                SET_FLAG(ZF);
                break;
            case 0x70:                  /* NEG ext */
                DAR = get_ext_addr();
                lo = (0 - CPU_BD_get_mbyte(DAR)) & 0xFF;
                CPU_BD_put_mbyte(DAR, lo);
                COND_SET_FLAG_V(lo == 0x80); // JBN was &
                CLR_FLAG(CF);
                if (lo)
                    SET_FLAG(CF);
                COND_SET_FLAG_N(lo);
                COND_SET_FLAG_Z(lo);
                break;
            case 0x73:                  /* COM ext */
                DAR = get_ext_addr();
                lo = ~CPU_BD_get_mbyte(DAR);
                lo &= 0xFF;
                CPU_BD_put_mbyte(DAR, lo);
                CLR_FLAG(VF);
                SET_FLAG(CF);
                COND_SET_FLAG_N(lo);
                COND_SET_FLAG_Z(lo);
                break;
            case 0x74:                  /* LSR ext */
                DAR = get_ext_addr();
                lo = CPU_BD_get_mbyte(DAR);
                COND_SET_FLAG(lo & 0x01,CF);
                lo >>= 1;
                CPU_BD_put_mbyte(DAR, lo);
                CLR_FLAG(NF);
                COND_SET_FLAG_Z(lo);
                COND_SET_FLAG_V(get_flag(NF) ^ get_flag(CF));
                break;
            case 0x76:                  /* ROR ext */
                DAR = get_ext_addr();
                hi = get_flag(CF);
                lo = CPU_BD_get_mbyte(DAR);
                COND_SET_FLAG(lo & 0x01,CF);
                lo >>= 1;
                if (hi)
                    lo |= 0x80;
                CPU_BD_put_mbyte(DAR, lo);
                COND_SET_FLAG_N(lo);
                COND_SET_FLAG_Z(lo);
                COND_SET_FLAG_V(get_flag(NF) ^ get_flag(CF));
                break;
            case 0x77:                  /* ASR ext */
                DAR = get_ext_addr();
                lo = CPU_BD_get_mbyte(DAR);
                COND_SET_FLAG(lo & 0x01,CF);
                hi = lo & 0x80;
                lo >>= 1;
                lo |= hi;
                CPU_BD_put_mbyte(DAR, lo);
                COND_SET_FLAG_N(lo);
                COND_SET_FLAG_Z(lo);
                COND_SET_FLAG_V(get_flag(NF) ^ get_flag(CF));
                break;
            case 0x78:                  /* ASL ext */
                DAR = get_ext_addr();
                lo = CPU_BD_get_mbyte(DAR);
                COND_SET_FLAG(lo & 0x80,CF);
                lo <<= 1;
                CPU_BD_put_mbyte(DAR, lo);
                COND_SET_FLAG_N(lo);
                COND_SET_FLAG_Z(lo);
                COND_SET_FLAG_V(get_flag(NF) ^ get_flag(CF));
                break;
            case 0x79:                  /* ROL ext */
                DAR = get_ext_addr();
                lo = CPU_BD_get_mbyte(DAR);
                hi = get_flag(CF);
                COND_SET_FLAG(lo & 0x80,CF);
                lo <<= 1;
                if (hi)
                    lo |= 0x01;
                CPU_BD_put_mbyte(DAR, lo);
                COND_SET_FLAG_N(lo);
                COND_SET_FLAG_Z(lo);
                COND_SET_FLAG_V(get_flag(NF) ^ get_flag(CF));
                break;
            case 0x7A:                  /* DEC ext */
                DAR = get_ext_addr();
                lo = CPU_BD_get_mbyte(DAR);
                COND_SET_FLAG_V(lo == 0x80);
                lo = (lo - 1) & 0xFF;
                CPU_BD_put_mbyte(DAR, lo);
                COND_SET_FLAG_N(lo);
                COND_SET_FLAG_Z(lo);
                break;
            case 0x7C:                  /* INC ext */
                DAR = get_ext_addr();
                lo = CPU_BD_get_mbyte(DAR);
                COND_SET_FLAG_V(lo == 0x7F);
                lo = (lo + 1) & 0xFF;
                CPU_BD_put_mbyte(DAR, lo);
                COND_SET_FLAG_N(lo);
                COND_SET_FLAG_Z(lo);        
                break;
            case 0x7D:                  /* TST ext */
                lo = CPU_BD_get_mbyte(get_ext_addr()) - 0;
                CLR_FLAG(VF);
                CLR_FLAG(CF);
                COND_SET_FLAG_N(lo);
                lo &= 0xFF;
                COND_SET_FLAG_Z(lo);
                break;
            case 0x7E:                  /* JMP ext */
                PC = get_ext_addr() & ADDRMASK;
                break;
            case 0x7F:                  /* CLR ext */
                CPU_BD_put_mbyte(get_ext_addr(), 0);
                CLR_FLAG(NF);
                CLR_FLAG(VF);
                CLR_FLAG(CF);
                SET_FLAG(ZF);
                break;
            case 0x80:                  /* SUB A imm */
                op1 = get_dir_addr();
                tmp1 = A;
                A = A - op1;
                COND_SET_FLAG_N(A);
                COND_SET_FLAG_C(A);
                condevalVs(A, tmp1, op1);
                A &= 0xFF;
                COND_SET_FLAG_Z(A);
                break;
            case 0x81:                  /* CMP A imm */
                op1 = get_dir_addr();
                lo = A - op1;
                COND_SET_FLAG_N(lo);
                COND_SET_FLAG_C(lo);
                condevalVs(lo, A, op1);
                lo &= 0xFF;
                COND_SET_FLAG_Z(lo);
                break;
            case 0x82:                  /* SBC A imm */
                op1 = get_dir_addr();
                tmp1 = A;
                A = A - op1 - get_flag(CF);
                COND_SET_FLAG_N(A);
                COND_SET_FLAG_C(A);
                condevalVs(A, tmp1, op1);
                A &= 0xFF;
                COND_SET_FLAG_Z(A);
                break;
            case 0x84:                  /* AND A imm */
                A = (A & get_dir_addr()) & 0xFF;
                CLR_FLAG(VF);
                COND_SET_FLAG_N(A);
                COND_SET_FLAG_Z(A);
                break;
            case 0x85:                  /* BIT A imm */
                lo = (A & get_dir_addr()) & 0xFF;
                CLR_FLAG(VF);
                COND_SET_FLAG_N(lo);
                COND_SET_FLAG_Z(lo);
                break;
            case 0x86:                  /* LDA A imm */
                A = get_dir_addr();
                CLR_FLAG(VF);
                COND_SET_FLAG_N(A);
                COND_SET_FLAG_Z(A);
                break;
            case 0x88:                  /* EOR A imm */
                A = (A ^ get_dir_addr()) & 0xFF;
                CLR_FLAG(VF);
                COND_SET_FLAG_N(A);
                COND_SET_FLAG_Z(A);
                break;
            case 0x89:                  /* ADC A imm */
                op1 = get_dir_addr();
                tmp1 = A;
                tmp2 = op1 + get_flag(CF);
                A = A + tmp2;
                COND_SET_FLAG_H(A, tmp1, tmp2);
                COND_SET_FLAG_N(A);
                COND_SET_FLAG_C(A);
                condevalVa(A, tmp1, tmp2);
                A &= 0xFF;
                COND_SET_FLAG_Z(A);
                break;
            case 0x8A:                  /* ORA A imm */
                A = (A | get_dir_addr()) & 0xFF;
                CLR_FLAG(VF);
                COND_SET_FLAG_N(A);
                COND_SET_FLAG_Z(A);
                break;
            case 0x8B:                  /* ADD A imm */
                op1 = get_dir_addr();
                tmp1 = A;
                A = A + op1;
                COND_SET_FLAG_H(A, tmp1, op1);
                COND_SET_FLAG_N(A);
                COND_SET_FLAG_C(A);
                condevalVa(A, tmp1, op1);
                A &= 0xFF;
                COND_SET_FLAG_Z(A);
                break;
            case 0x8C:                  /* CPX imm */
                op1 = IX - get_ext_addr();
                COND_SET_FLAG_Z(op1);
                COND_SET_FLAG_N(op1 >> 8);
                COND_SET_FLAG_V(op1 & 0x10000);
                break;
            case 0x8D:                  /* BSR rel */
                lo = get_rel_addr();
                push_word(PC);
                PC = PC + lo;
                PC &= ADDRMASK;
                break;
            case 0x8E:                  /* LDS imm */
                SP = get_ext_addr();
                COND_SET_FLAG_N(SP >> 8);
                COND_SET_FLAG_Z(SP);
                CLR_FLAG(VF);
                break;
            case 0x90:                  /* SUB A dir */
                op1 = get_dir_val();
                tmp1 = A;
                A = A - op1;
                COND_SET_FLAG_N(A);
                COND_SET_FLAG_C(A);
                condevalVs(A, tmp1, op1);
                A &= 0xFF;
                COND_SET_FLAG_Z(A);
                break;
            case 0x91:                  /* CMP A dir */
                op1 = get_dir_val();
                lo = A - op1;
                COND_SET_FLAG_N(lo);
                COND_SET_FLAG_C(lo);
                condevalVs(lo, A, op1);
                lo &= 0xFF;
                COND_SET_FLAG_Z(lo);
                break;
            case 0x92:                  /* SBC A dir */
                op1 = get_dir_val();
                tmp1 = A;
                A = A - op1 - get_flag(CF);
                COND_SET_FLAG_N(A);
                COND_SET_FLAG_C(A);
                condevalVs(A, tmp1, op1);
                A &= 0xFF;
                COND_SET_FLAG_Z(A);
                break;
            case 0x94:                  /* AND A dir */
                A = (A & get_dir_val()) & 0xFF;
                CLR_FLAG(VF);
                COND_SET_FLAG_N(A);
                COND_SET_FLAG_Z(A);
                break;
            case 0x95:                  /* BIT A dir */
                lo = (A & get_dir_val()) & 0xFF;
                CLR_FLAG(VF);
                COND_SET_FLAG_N(lo);
                COND_SET_FLAG_Z(lo);
                break;
            case 0x96:                  /* LDA A dir */
                A = get_dir_val();
                CLR_FLAG(VF);
                COND_SET_FLAG_N(A);
                COND_SET_FLAG_Z(A);
                break;
            case 0x97:                  /* STA A dir */
                CPU_BD_put_mbyte(get_dir_addr(), A);
                CLR_FLAG(VF);
                COND_SET_FLAG_N(A);
                COND_SET_FLAG_Z(A);
                break;
            case 0x98:                  /* EOR A dir */
                A = (A ^ get_dir_val()) & 0xFF;
                CLR_FLAG(VF);
                COND_SET_FLAG_N(A);
                COND_SET_FLAG_Z(A);
                break;
            case 0x99:                  /* ADC A dir */
                op1 = get_dir_val(); 
                tmp1 = A;
                tmp2 = op1 + get_flag(CF);
                A = A + tmp2;
                COND_SET_FLAG_H(A, tmp1, tmp2);
                COND_SET_FLAG_N(A);
                COND_SET_FLAG_C(A);
                condevalVa(A, tmp1, tmp2);
                A &= 0xFF;
                COND_SET_FLAG_Z(A);
                break;
            case 0x9A:                  /* ORA A dir */
                A = (A | get_dir_val()) & 0xFF;
                CLR_FLAG(VF);
                COND_SET_FLAG_N(A);
                COND_SET_FLAG_Z(A);
                break;
            case 0x9B:                  /* ADD A dir */
                op1 = get_dir_val();
                tmp1 = A;
                A = A + op1;
                COND_SET_FLAG_H(A, tmp1, op1);
                COND_SET_FLAG_N(A);
                COND_SET_FLAG_C(A);
                condevalVa(A, tmp1, op1);
                A &= 0xFF;
                COND_SET_FLAG_Z(A);
                break;
            case 0x9C:                  /* CPX dir */
                op1 = IX - CPU_BD_get_mword(get_dir_addr());
                COND_SET_FLAG_Z(op1);
                COND_SET_FLAG_N(op1 >> 8);
                COND_SET_FLAG_V(op1 & 0x10000);
                break;
            case 0x9E:                  /* LDS dir */
                SP = CPU_BD_get_mword(get_dir_addr());
                COND_SET_FLAG_N(SP >> 8);
                COND_SET_FLAG_Z(SP);
                CLR_FLAG(VF);
                break;
            case 0x9F:                  /* STS dir */
                CPU_BD_put_mword(get_dir_addr(), SP);
                COND_SET_FLAG_N(SP >> 8);
                COND_SET_FLAG_Z(SP);
                CLR_FLAG(VF);
                break;
            case 0xA0:                  /* SUB A ind */
                op1 = get_indir_val();
                tmp1 = A;
                A = A - op1;
                COND_SET_FLAG_N(A);
                COND_SET_FLAG_C(A);
                condevalVs(A, tmp1, op1);
                A &= 0xFF;
                COND_SET_FLAG_Z(A);
                break;
            case 0xA1:                  /* CMP A ind */
                op1 = get_indir_val();
                lo = A - op1;
                COND_SET_FLAG_N(lo);
                COND_SET_FLAG_C(lo);
                condevalVs(lo, A, op1);
                lo &= 0xFF;
                COND_SET_FLAG_Z(lo);
                break;
            case 0xA2:                  /* SBC A ind */
                op1 = get_indir_val();
                tmp1 = A;
                A = A - op1 - get_flag(CF);
                COND_SET_FLAG_N(A);
                COND_SET_FLAG_C(A);
                condevalVs(A, tmp1, op1);
                A &= 0xFF;
                COND_SET_FLAG_Z(A);
                break;
            case 0xA4:                  /* AND A ind */
                A = (A & get_indir_val()) & 0xFF;
                CLR_FLAG(VF);
                COND_SET_FLAG_N(A);
                COND_SET_FLAG_Z(A);
                break;
            case 0xA5:                  /* BIT A ind */
                lo = (A & get_indir_val()) & 0xFF;
                CLR_FLAG(VF);
                COND_SET_FLAG_N(lo);
                COND_SET_FLAG_Z(lo);
                break;
            case 0xA6:                  /* LDA A ind */
                A = get_indir_val();
                CLR_FLAG(VF);
                COND_SET_FLAG_N(A);
                COND_SET_FLAG_Z(A);
                break;
            case 0xA7:                  /* STA A ind */
                CPU_BD_put_mbyte(get_indir_addr(), A);
                CLR_FLAG(VF);
                COND_SET_FLAG_N(A);
                COND_SET_FLAG_Z(A);
                break;
            case 0xA8:                  /* EOR A ind */
                A = (A ^ get_indir_val()) & 0xFF;
                CLR_FLAG(VF);
                COND_SET_FLAG_N(A);
                COND_SET_FLAG_Z(A);
                break;
            case 0xA9:                  /* ADC A ind */
                op1 = get_indir_val();
                tmp1 = A;
                tmp2 = op1 + get_flag(CF);
                A = A + tmp2;
                COND_SET_FLAG_H(A, tmp1, tmp2);
                COND_SET_FLAG_N(A);
                COND_SET_FLAG_C(A);
                condevalVa(A, tmp1, tmp2);
                A &= 0xFF;
                COND_SET_FLAG_Z(A);
                break;
            case 0xAA:                  /* ORA A ind */
                A = (A | get_indir_val()) & 0xFF;
                CLR_FLAG(VF);
                COND_SET_FLAG_N(A);
                COND_SET_FLAG_Z(A);
                break;
            case 0xAB:                  /* ADD A ind */
                op1 = get_indir_val();
                tmp1 = A;
                A = A + op1;
                COND_SET_FLAG_H(A, tmp1, op1);
                COND_SET_FLAG_N(A);
                COND_SET_FLAG_C(A);
                condevalVa(A, tmp1, op1);
                A &= 0xFF;
                COND_SET_FLAG_Z(A);
                break;
            case 0xAC:                  /* CPX ind */
                op1 = (IX - get_indir_addr()) & ADDRMASK;
                COND_SET_FLAG_Z(op1);
                COND_SET_FLAG_N(op1 >> 8);
                COND_SET_FLAG_V(op1 & 0x10000);
                break;
            case 0xAD:                  /* JSR ind */
                DAR = get_indir_addr();
                push_word(PC);
                PC = DAR;
                break;
            case 0xAE:                  /* LDS ind */
                SP = CPU_BD_get_mword(get_indir_addr());
                COND_SET_FLAG_N(SP >> 8);
                COND_SET_FLAG_Z(SP);
                CLR_FLAG(VF);
                break;
            case 0xAF:                  /* STS ind */
                CPU_BD_put_mword(get_indir_addr(), SP);
                COND_SET_FLAG_N(SP >> 8);
                COND_SET_FLAG_Z(SP);
                CLR_FLAG(VF);
                break;
            case 0xB0:                  /* SUB A ext */
                op1 = get_ext_val();
                tmp1 = A;
                A = A - op1;
                COND_SET_FLAG_N(A);
                COND_SET_FLAG_C(A);
                condevalVs(A, tmp1, op1);
                A &= 0xFF;
                COND_SET_FLAG_Z(A);
                break;
            case 0xB1:                  /* CMP A ext */
                op1 = get_ext_val();
                lo = A - op1;
                COND_SET_FLAG_N(lo);
                COND_SET_FLAG_C(lo);
                condevalVs(lo, A, op1);
                lo &= 0xFF;
                COND_SET_FLAG_Z(lo);
                break;
            case 0xB2:                  /* SBC A ext */
                op1 = get_ext_val();
                tmp1 = A;
                A = A - op1 - get_flag(CF);
                COND_SET_FLAG_N(A);
                COND_SET_FLAG_C(A);
                condevalVs(A, tmp1, op1);
                A &= 0xFF;
                COND_SET_FLAG_Z(A);
                break;
            case 0xB4:                  /* AND A ext */
                A = (A & get_ext_val()) & 0xFF;
                CLR_FLAG(VF);
                COND_SET_FLAG_N(A);
                COND_SET_FLAG_Z(A);
                break;
            case 0xB5:                  /* BIT A ext */
                lo = (A & get_ext_val()) & 0xFF;
                CLR_FLAG(VF);
                COND_SET_FLAG_N(lo);
                COND_SET_FLAG_Z(lo);
                break;
            case 0xB6:                  /* LDA A ext */
                A = get_ext_val();
                CLR_FLAG(VF);
                COND_SET_FLAG_N(A);
                COND_SET_FLAG_Z(A);
                break;
            case 0xB7:                  /* STA A ext */
                CPU_BD_put_mbyte(get_ext_addr(), A);
                CLR_FLAG(VF);
                COND_SET_FLAG_N(A);
                COND_SET_FLAG_Z(A);
                break;
            case 0xB8:                  /* EOR A ext */
                A = (A ^ get_ext_val()) & 0xFF;
                CLR_FLAG(VF);
                COND_SET_FLAG_N(A);
                COND_SET_FLAG_Z(A);
                break;
            case 0xB9:                  /* ADC A ext */
                op1 = get_ext_val();
                tmp1 = A;
                tmp2 = op1 + get_flag(CF);
                A = A + tmp2;
                COND_SET_FLAG_H(A, tmp1, tmp2);
                COND_SET_FLAG_N(A);
                COND_SET_FLAG_C(A);
                condevalVa(A, tmp1, tmp2);
                A &= 0xFF;
                COND_SET_FLAG_Z(A);
                break;
            case 0xBA:                  /* ORA A ext */
                A = (A | get_ext_val()) & 0xFF;
                CLR_FLAG(VF);
                COND_SET_FLAG_N(A);
                COND_SET_FLAG_Z(A);
                break;
            case 0xBB:                  /* ADD A ext */
                op1 = get_ext_val();
                tmp1 = A;
                A = A + op1;
                COND_SET_FLAG_H(A, tmp1, op1);
                COND_SET_FLAG_N(A);
                COND_SET_FLAG_C(A);
                condevalVa(A, tmp1, op1);
                A &= 0xFF;
                COND_SET_FLAG_Z(A);
                break;
            case 0xBC:                  /* CPX ext */
                op1 = (IX - CPU_BD_get_mword(get_ext_addr()));// & ADDRMASK;
                COND_SET_FLAG_Z(op1);
                COND_SET_FLAG_N(op1 >> 8);
                COND_SET_FLAG_V(op1 & 0x10000);
                break;
            case 0xBD:                  /* JSR ext */
                DAR = get_ext_addr();
                push_word(PC);
                PC = DAR;
                break;
            case 0xBE:                  /* LDS ext */
                SP = CPU_BD_get_mword(get_ext_addr());
                COND_SET_FLAG_N(SP >> 8);
                COND_SET_FLAG_Z(SP);
                CLR_FLAG(VF);
                break;
            case 0xBF:                  /* STS ext */
                CPU_BD_put_mword(get_ext_addr(), SP);
                COND_SET_FLAG_N(SP >> 8);
                COND_SET_FLAG_Z(SP);
                CLR_FLAG(VF);
                break;
            case 0xC0:                  /* SUB B imm */
                op1 = get_dir_addr();
                tmp1 = B;
                B = B - op1;
                COND_SET_FLAG_N(B);
                COND_SET_FLAG_C(B);
                condevalVs(B, tmp1, op1);
                B &= 0xFF;
                COND_SET_FLAG_Z(B);
                break;
            case 0xC1:                  /* CMP B imm */
                op1 = get_dir_addr();
                lo = B - op1;
                COND_SET_FLAG_N(lo);
                COND_SET_FLAG_C(lo);
                condevalVs(lo, B, op1);
                lo &= 0xFF;
                COND_SET_FLAG_Z(lo);
                break;
            case 0xC2:                  /* SBC B imm */
                op1 = get_dir_addr();
                tmp1 = B;
                B = B - op1 - get_flag(CF);
                COND_SET_FLAG_N(B);
                COND_SET_FLAG_C(B);
                condevalVs(B, tmp1, op1);
                B &= 0xFF;
                COND_SET_FLAG_Z(B);
                break;
            case 0xC4:                  /* AND B imm */
                B = (B & get_dir_addr()) & 0xFF;
                CLR_FLAG(VF);
                COND_SET_FLAG_N(B);
                COND_SET_FLAG_Z(B);
                break;
            case 0xC5:                  /* BIT B imm */
                lo = (B & get_dir_addr()) & 0xFF;
                CLR_FLAG(VF);
                COND_SET_FLAG_N(lo);
                COND_SET_FLAG_Z(lo);
                break;
            case 0xC6:                  /* LDA B imm */
                B = get_dir_addr();
                CLR_FLAG(VF);
                COND_SET_FLAG_N(B);
                COND_SET_FLAG_Z(B);
                break;
            case 0xC8:                  /* EOR B imm */
                B = (B ^ get_dir_addr()) & 0xFF;
                CLR_FLAG(VF);
                COND_SET_FLAG_N(B);
                COND_SET_FLAG_Z(B);
                break;
            case 0xC9:                  /* ADC B imm */
                op1 = get_dir_addr();
                tmp1 = B;
                tmp2 = op1 + get_flag(CF);
                B = B + tmp2;
                COND_SET_FLAG_H(B, tmp1, tmp2);
                COND_SET_FLAG_N(B);
                COND_SET_FLAG_C(B);
                condevalVa(B, tmp1, tmp2);
                B &= 0xFF;
                COND_SET_FLAG_Z(B);
                break;
            case 0xCA:                  /* ORA B imm */
                B = (B | get_dir_addr()) & 0xFF;
                CLR_FLAG(VF);
                COND_SET_FLAG_N(B);
                COND_SET_FLAG_Z(B);
                break;
            case 0xCB:                  /* ADD B imm */
                op1 = get_dir_addr();
                tmp1 = B;
                B = B + op1;
                COND_SET_FLAG_H(B, tmp1, op1);
                COND_SET_FLAG_N(B);
                COND_SET_FLAG_C(B);
                condevalVa(B, tmp1, op1);
                B &= 0xFF;
                COND_SET_FLAG_Z(B);
                break;
            case 0xCE:                  /* LDX imm */
                IX = get_ext_addr();
                COND_SET_FLAG_N(IX >> 8);
                COND_SET_FLAG_Z(IX);
                CLR_FLAG(VF);
                break;
            case 0xD0:                  /* SUB B dir */
                op1 = get_dir_val();
                tmp1 = B;
                B = B - op1;
                COND_SET_FLAG_N(B);
                COND_SET_FLAG_C(B);
                condevalVs(B, tmp1, op1);
                B &= 0xFF;
                COND_SET_FLAG_Z(B);
                break;
            case 0xD1:                  /* CMP B dir */
                op1 = get_dir_val();
                lo = B - op1;
                COND_SET_FLAG_N(lo);
                COND_SET_FLAG_Z(lo);
                COND_SET_FLAG_C(lo);
                condevalVs(lo, B, op1);
                break;
            case 0xD2:                  /* SBC B dir */
                op1 = get_dir_val();
                tmp1 = B;
                B = B - op1 - get_flag(CF);
                COND_SET_FLAG_N(B);
                COND_SET_FLAG_C(B);
                condevalVs(B, tmp1, op1);
                B &= 0xFF;
                COND_SET_FLAG_Z(B);
                break;
            case 0xD4:                  /* AND B dir */
                B = (B & get_dir_val()) & 0xFF;
                CLR_FLAG(VF);
                COND_SET_FLAG_N(B);
                COND_SET_FLAG_Z(B);
                break;
            case 0xD5:                  /* BIT B dir */
                lo = (B & get_dir_val()) & 0xFF;
                CLR_FLAG(VF);
                COND_SET_FLAG_N(lo);
                COND_SET_FLAG_Z(lo);
                break;
            case 0xD6:                  /* LDA B dir */
                B = get_dir_val();
                CLR_FLAG(VF);
                COND_SET_FLAG_N(B);
                COND_SET_FLAG_Z(B);
                break;
            case 0xD7:                  /* STA B dir */
                CPU_BD_put_mbyte(get_dir_addr(), B);
                CLR_FLAG(VF);
                COND_SET_FLAG_N(B);
                COND_SET_FLAG_Z(B);
                break;
            case 0xD8:                  /* EOR B dir */
                B = (B ^ get_dir_val()) & 0xFF;
                CLR_FLAG(VF);
                COND_SET_FLAG_N(B);
                COND_SET_FLAG_Z(B);
                break;
            case 0xD9:                  /* ADC B dir */
                op1 = get_dir_val();
                tmp1 = B;
                tmp2 = op1 + get_flag(CF);
                B = B + tmp2;
                COND_SET_FLAG_H(B, tmp1, tmp2);
                COND_SET_FLAG_N(B);
                COND_SET_FLAG_C(B);
                condevalVa(B, tmp1, tmp2);
                B &= 0xFF;
                COND_SET_FLAG_Z(B);
                break;
            case 0xDA:                  /* ORA B dir */
                B = (B | get_dir_val()) & 0xFF;
                CLR_FLAG(VF);
                COND_SET_FLAG_N(B);
                COND_SET_FLAG_Z(B);
                break;
            case 0xDB:                  /* ADD B dir */
                op1 = get_dir_val();
                tmp1 = B;
                B = B + op1;
                COND_SET_FLAG_H(B, tmp1, op1);
                COND_SET_FLAG_N(B);
                COND_SET_FLAG_C(B);
                condevalVa(B, tmp1, op1);
                B &= 0xFF;
                COND_SET_FLAG_Z(B);
                break;
            case 0xDE:                  /* LDX dir */
                IX = CPU_BD_get_mword(get_dir_addr());
                COND_SET_FLAG_N(IX >> 8);
                COND_SET_FLAG_Z(IX);
                CLR_FLAG(VF);
                break;
            case 0xDF:                  /* STX dir */
                CPU_BD_put_mword(get_dir_addr(), IX);
                COND_SET_FLAG_N(IX >> 8);
                COND_SET_FLAG_Z(IX);
                CLR_FLAG(VF);
                break;
            case 0xE0:                  /* SUB B ind */
                op1 = get_indir_val();
                tmp1 = B;
                B = B - op1;
                COND_SET_FLAG_N(B);
                COND_SET_FLAG_C(B);
                condevalVs(B, tmp1, op1);
                B &= 0xFF;
                COND_SET_FLAG_Z(B);
                break;
            case 0xE1:                  /* CMP B ind */
                op1 = get_indir_val();
                lo = B - op1;
                COND_SET_FLAG_N(lo);
                COND_SET_FLAG_C(lo);
                condevalVs(lo, B, op1);
                lo &= 0xFF;
                COND_SET_FLAG_Z(lo);
                break;
            case 0xE2:                  /* SBC B ind */
                op1 = get_indir_val();
                tmp1 = B;
                B = B - op1 - get_flag(CF);
                COND_SET_FLAG_N(B);
                COND_SET_FLAG_C(B);
                condevalVs(B, tmp1, op1);
                B &= 0xFF;
                COND_SET_FLAG_Z(B);
                break;
            case 0xE4:                  /* AND B ind */
                B = (B & get_indir_val()) & 0xFF;
                CLR_FLAG(VF);
                COND_SET_FLAG_N(B);
                COND_SET_FLAG_Z(B);
                break;
            case 0xE5:                  /* BIT B ind */
                lo = (B & get_indir_val()) & 0xFF;
                CLR_FLAG(VF);
                COND_SET_FLAG_N(lo);
                COND_SET_FLAG_Z(lo);
                break;
            case 0xE6:                  /* LDA B ind */
                B = get_indir_val();
                CLR_FLAG(VF);
                COND_SET_FLAG_N(B);
                COND_SET_FLAG_Z(B);
                break;
            case 0xE7:                  /* STA B ind */
                CPU_BD_put_mbyte(get_indir_addr(), B);
                CLR_FLAG(VF);
                COND_SET_FLAG_N(B);
                COND_SET_FLAG_Z(B);
                break;
            case 0xE8:                  /* EOR B ind */
                B = (B ^ get_indir_val()) & 0xFF;
                CLR_FLAG(VF);
                COND_SET_FLAG_N(B);
                COND_SET_FLAG_Z(B);
                break;
            case 0xE9:                  /* ADC B ind */
                op1 = get_indir_val();
                tmp1 = B;
                tmp2 = op1 + get_flag(CF);
                B = B + tmp2;
                COND_SET_FLAG_H(B, tmp1, tmp2);
                COND_SET_FLAG_N(B);
                COND_SET_FLAG_C(B);
                condevalVa(B, tmp1, tmp2);
                B &= 0xFF;
                COND_SET_FLAG_Z(B);
                break;
            case 0xEA:                  /* ORA B ind */
                B = (B | get_indir_val()) & 0xFF;
                CLR_FLAG(VF);
                COND_SET_FLAG_N(B);
                COND_SET_FLAG_Z(B);
                break;
            case 0xEB:                  /* ADD B ind */
                op1 = get_indir_val();
                tmp1 = B;
                B = B + op1;
                COND_SET_FLAG_H(B, tmp1, op1);
                COND_SET_FLAG_N(B);
                COND_SET_FLAG_C(B);
                condevalVa(B, tmp1, op1);
                B &= 0xFF;
                COND_SET_FLAG_Z(B);
                break;
            case 0xEE:                  /* LDX ind */
                IX = CPU_BD_get_mword(get_indir_addr());
                COND_SET_FLAG_N(IX >> 8);
                COND_SET_FLAG_Z(IX);
                CLR_FLAG(VF);
                break;
            case 0xEF:                  /* STX ind */
                CPU_BD_put_mword(get_indir_addr(), IX);
                COND_SET_FLAG_N(IX >> 8);
                COND_SET_FLAG_Z(IX);
                CLR_FLAG(VF);
                break;
            case 0xF0:                  /* SUB B ext */
                op1 = get_ext_val();
                tmp1 = B;
                B = B - op1;
                COND_SET_FLAG_N(B);
                COND_SET_FLAG_C(B);
                condevalVs(B, tmp1, op1);
                B &= 0xFF;
                COND_SET_FLAG_Z(B);
                break;
            case 0xF1:                  /* CMP B ext */
                op1 = get_ext_val();
                lo = B - op1;
                COND_SET_FLAG_N(lo);
                COND_SET_FLAG_C(lo);
                condevalVs(lo, B, op1);
                lo &= 0xFF;
                COND_SET_FLAG_Z(lo);
                break;
            case 0xF2:                  /* SBC B ext */
                op1 = get_ext_val();
                tmp1 = B;
                B = B - op1 - get_flag(CF);
                COND_SET_FLAG_N(B);
                COND_SET_FLAG_C(B);
                condevalVs(B, tmp1, op1);
                B &= 0xFF;
                COND_SET_FLAG_Z(B);
                break;
            case 0xF4:                  /* AND B ext */
                B = (B & get_ext_val()) & 0xFF;
                CLR_FLAG(VF);
                COND_SET_FLAG_N(B);
                COND_SET_FLAG_Z(B);
                break;
            case 0xF5:                  /* BIT B ext */
                lo = (B & get_ext_val()) & 0xFF;
                CLR_FLAG(VF);
                COND_SET_FLAG_N(lo);
                COND_SET_FLAG_Z(lo);
                break;
            case 0xF6:                  /* LDA B ext */
                B = get_ext_val();
                CLR_FLAG(VF);
                COND_SET_FLAG_N(B);
                COND_SET_FLAG_Z(B);
                break;
            case 0xF7:                  /* STA B ext */
                CPU_BD_put_mbyte(get_ext_addr(), B);
                CLR_FLAG(VF);
                COND_SET_FLAG_N(B);
                COND_SET_FLAG_Z(B);
                break;
            case 0xF8:                  /* EOR B ext */
                B = (B ^ get_ext_val()) & 0xFF;
                CLR_FLAG(VF);
                COND_SET_FLAG_N(B);
                COND_SET_FLAG_Z(B);
                break;
            case 0xF9:                  /* ADC B ext */
                op1 = get_ext_val();
                tmp1 = B;
                tmp2 = op1 + get_flag(CF);
                B = B + tmp2;
                COND_SET_FLAG_H(B, tmp1, tmp2);
                COND_SET_FLAG_N(B);
                COND_SET_FLAG_C(B);
                condevalVa(B, tmp1, tmp2);
                B &= 0xFF;
                COND_SET_FLAG_Z(B);
                break;
            case 0xFA:                  /* ORA B ext */
                B = (B | get_ext_val()) & 0xFF;
                CLR_FLAG(VF);
                COND_SET_FLAG_N(B);
                COND_SET_FLAG_Z(B);
                break;
            case 0xFB:                  /* ADD B ext */
                op1 = get_ext_val();
                tmp1 = B;
                B = B + op1;
                COND_SET_FLAG_H(B, tmp1, op1);
                COND_SET_FLAG_N(B);
                COND_SET_FLAG_C(B);
                condevalVa(B, tmp1, op1);
                B &= 0xFF;
                COND_SET_FLAG_Z(B);
                break;
            case 0xFE:                  /* LDX ext */
                IX = CPU_BD_get_mword(get_ext_addr());
                COND_SET_FLAG_N(IX >> 8);
                COND_SET_FLAG_Z(IX);
                CLR_FLAG(VF);
                break;
            case 0xFF:                  /* STX ext */
                CPU_BD_put_mword(get_ext_addr(), IX);
                COND_SET_FLAG_N(IX >> 8);
                COND_SET_FLAG_Z(IX);
                CLR_FLAG(VF);
                break;

            default: {                  /* Unassigned */
//                if (m6800_unit.flags & UNIT_OPSTOP) {
                    reason = STOP_OPCODE;
                    PC--;
//                }
                break;
            }
        }
    }
    /* Simulation halted - lets dump all the registers! */
    dump_regs();
    saved_PC = PC;
    return reason;
}

/* dump the working registers */

void dump_regs(void)
{
    char diag[60];
    WriteToConsole("\r\nSimulation halted!");
    sprintf(diag, "Last instruction executed: PC=%04X opcode=%02X", PC-1, CPU_BD_get_mbyte(PC-1) & 0xFF);
    WriteToConsole(diag);
    sprintf(diag, "PC=%04X SP=%04X IX=%04X ", PC, SP, IX);
    WriteToConsole(diag);
    sprintf(diag, "A=%02X B=%02X CCR=%02X", A, B, CCR);
    WriteToConsole(diag);
}

/*
void dump_regs1(void)
{
    printf("PC=%04X SP=%04X IX=%04X ", PC, SP, IX);
    printf("A=%02X B=%02X CCR=%02X\n", A, B, CCR);
}
*/
/* fetch an instruction or byte */
int32_t fetch_byte(int32_t flag)
{
    unsigned char val;
    char text[32];
    char *op;
    
    val = CPU_BD_get_mbyte(PC) & 0xFF;   // fetch byte
    op = (char *) opcode[val];
    
    if (TRACE_PC) {  // display source code
        switch (flag) {
            case 0:                     // opcode fetch
                // op = (char *)opcode[val];
                trace(PC, op, CCR, B, A, IX, SP);
                break;
            case 1:                     // byte operand fetch
                //sprintf(text"0%02XH", val);
                //Serial.print(text);
                break;
        }
    }
   
    PC = (PC + 1) & ADDRMASK;           /* increment PC */
    return val;
}

/* fetch a word */
int32_t fetch_word(void)
{
    uint16_t val;

    val = CPU_BD_get_mbyte(PC) << 8;     /* fetch high byte */
    val |= CPU_BD_get_mbyte(PC + 1) & 0xFF; /* fetch low byte */
//    if (m6800_dev.dctrl & DEBUG_asm) printf("0%04XH", val);
    PC = (PC + 2) & ADDRMASK;           /* increment PC */
    return val;
}

/* push a byte to the stack */
void push_byte(unsigned char val)
{
    CPU_BD_put_mbyte(SP, val & 0xFF);
    SP = (SP - 1) & ADDRMASK;
}

/* push a word to the stack */
void push_word(uint16_t val)
{
    push_byte(val & 0xFF);
    push_byte(val >> 8);
}

/* pop a byte from the stack */
unsigned char pop_byte(void)
{
    register unsigned char res;

    SP = (SP + 1) & ADDRMASK;
    res = CPU_BD_get_mbyte(SP);
    return res;
}

/* pop a word from the stack */
uint16_t pop_word(void)
{
    register uint16_t res;

    res = pop_byte() << 8;
    res |= pop_byte();
    return res;
}

/* this routine does the jump to relative offset if the condition is
   met.  Otherwise, execution continues at the current PC. */

void go_rel(int32_t cond)
{
    int32_t temp;

    temp = get_rel_addr();
    if (cond)
        PC += temp;
    PC &= ADDRMASK;
}

/* returns the relative offset sign-extended */

int32_t get_rel_addr(void)
{
    int32_t temp;

    temp = fetch_byte(1);
    if (temp & 0x80)
        temp |= 0xFF00;
    return temp & ADDRMASK;
}

/* returns the value at the direct address pointed to by PC */

int32_t get_dir_val(void)
{
    return CPU_BD_get_mbyte(get_dir_addr());
}

/* returns the direct address pointed to by PC */

int32_t get_dir_addr(void)
{
    int32_t temp;

    temp = fetch_byte(1);
    return temp & 0xFF;
}

/* returns the value at the indirect address pointed to by PC */

int32_t get_indir_val(void)
{
    return CPU_BD_get_mbyte(get_indir_addr());
}

/* returns the indirect address pointed to by PC or immediate byte */

int32_t get_indir_addr(void)
{
    int32_t temp;
    
    temp = (fetch_byte(1) + IX) & ADDRMASK;
    return temp;
}

/* returns the value at the extended address pointed to by PC */

int32_t get_ext_val(void)
{
    return CPU_BD_get_mbyte(get_ext_addr());
}

/* returns the extended address pointed to by PC or immediate word */

int32_t get_ext_addr(void)
{
    int32_t temp;

    temp = fetch_word();
    return temp;
}

/* return 1 for flag set or 0 for flag clear */

int32_t get_flag(int32_t flg)
{
    if (CCR & flg)
        return 1;
    else
        return 0;
}

/* test and set V for addition */

void condevalVa(int32_t res, int32_t op1, int32_t op2)
{
  uint32_t r = res&0x80;
  uint32_t x = op1&0x80;  // Moto "ACCX"
  uint32_t m = op2&0x80;  // Moto "M"
  
        COND_SET_FLAG_V( (x && m && !r)  ||  (!x  && !m  && r));
}

/* test and set V for subtraction. op1, op2 order is important for subtraction! */
void condevalVs(int32_t res, int32_t op1, int32_t op2)
{
  uint32_t r = res&0x80;
  uint32_t x = op1&0x80;  // Moto "ACCX"
  uint32_t m = op2&0x80;  // Moto "M"
 
  
        COND_SET_FLAG_V( (x && !m && !r)  ||  (!x && m && r));
}
/* Reset routine */

void m6800_reset (void)
{
    CCR = CCR_ALWAYS_ON | IF;
    int_req = 0;
//    sim_brk_types = sim_brk_dflt = SWMASK ('E');

    // Serial.println("Reading reset vector bytes from address 0xFFFE and 0xFFFF...");

    int high = CPU_BD_get_mbyte(0xFFFE);
    int low = CPU_BD_get_mbyte(0xFFFF);

    // Serial.print("Byte @ FFFE: ");
    // Serial.println(high, HEX);
    // Serial.print("Byte @ FFFF: ");
    // Serial.println(low, HEX);

    saved_PC = (high << 8) | low;

    // Serial.print("Byte @ ");
    // Serial.print(saved_PC, HEX);
    // Serial.print(": ");
    // Serial.println(CPU_BD_get_mbyte(saved_PC), HEX);

    SP = 0x01FD;
    RAM_0000_BFFF[0x01FD] = 0x00;  // Low byte of return address (0xFF00)
    RAM_0000_BFFF[0x01FC] = 0xFF;  // High byte
    PC = saved_PC;
}
/* end of m6800.c */
