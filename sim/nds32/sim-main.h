/* Simulator for NDS32 processors.

   Copyright (C) 2011-2013 Free Software Foundation, Inc.
   Contributed by Andes Technology Corporation.

   This file is part of simulators.

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 3 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program.  If not, see <http://www.gnu.org/licenses/>.  */

#ifndef _NDS32_MAIN_SIM_H_
#define _NDS32_MAIN_SIM_H_

#include <stdint.h>

#include "sim-basics.h"
#include "sim-signal.h"

typedef struct _sim_cpu SIM_CPU; /* sim-reg.c needs this. */
typedef unsigned32 sim_cia;

#define CIA_GET(cpu)     CPU_PC_GET (cpu)
#define CIA_SET(cpu,val) CPU_PC_SET ((cpu), (val))

#include "sim-base.h"

#include "nds32-mm.h"

typedef union {
  uint32_t u;
  int32_t s;
} reg_t;

enum nds32_internal_flags
{
  /* Set NIF_EX9 to indicate the instructions is executed in ITB.
     JAL and J work differently in ITB.  */
  NIF_EX9 = 1,
  /* Set NIF_BRANCH to indicate an branch is taken.  */
  NIF_BRANCH = 2,
};

struct _sim_cpu {
  /* 32 general purpose registers. */
  reg_t reg_gpr[32];
#define CCPU_GPR	(cpu->reg_gpr)

  /* User registers. 32 group x 32 USR */
  reg_t reg_usr[32 * 32];
#define CCPU_USR	(cpu->reg_usr)

  /* System registers.  Major x Minor x Ext */
  reg_t reg_sr[8 * 16 * 8];
#define CCPU_SR		(cpu->reg_sr)

  /* Floating-point registers. 32 single union 32 double. FIXME */
  reg_t reg_fpr[64];
#define CCPU_FPR	(cpu->reg_fpr)
  reg_t reg_fpcfg;
#define CCPU_FPCFG	(cpu->reg_fpcfg)
  reg_t reg_fpcsr;
#define CCPU_FPCSR	(cpu->reg_fpcsr)

  enum nds32_internal_flags iflags;
  /* If NIF_BRANCH, this is the destination address.  */
  SIM_ADDR	baddr;

  sim_cpu_base base;
};

struct sim_state {
  sim_cpu *cpu[MAX_NR_PROCESSORS];
#if (WITH_SMP)
#define STATE_CPU(sd,n) ((sd)->cpu[n])
#else
#define STATE_CPU(sd,n) ((sd)->cpu[0])
#endif
#define STATE_BOARD_DATA(sd) (&(sd)->board)

  char cmdline[256];		/* cmdline buffer for -mcrt-arg hacking. */

  struct bfd *interp_bfd;	/* For Linux dynamic linker.  */
  uint32_t interp_base;		/* Base address of where interp is loaded. */
  uint32_t exec_base;		/* Base address of where executable is loaded. */
  /* If the same memory-region is already attached (registered) to sim-core,
     the program just crashs. Unfortunately, we have no way to know whether
     the region is attached or not, so I use `mem_attached' to bookkeep it.

     MEMOPT provides `memory-delete all' command to delete all the mappings,
     but if sim_core_attach is used in order to attach device_io, then
     there is no way to detach all. */
  int mem_attached;

  struct nds32_mm mm;
#define STATE_MM(sd) (&(sd)->mm)

  int gprof;

  sim_state_base base;
};

#include "sim-engine.h"
#include "sim-options.h"
#include "run-sim.h"

#endif
