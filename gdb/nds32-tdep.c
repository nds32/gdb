/* Target-dependent code for NDS32 architecture, for GDB.

   Copyright (C) 2006-2013 Free Software Foundation, Inc.
   Contributed by Andes Technology Corporation.

   This file is part of GDB.

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

#include "defs.h"
#include <stdint.h>
#include "frame.h"
#include "frame-unwind.h"
#include "frame-base.h"
#include "symtab.h"
#include "gdbtypes.h"
#include "gdbcmd.h"
#include "gdbcore.h"
#include "gdb_string.h"
#include "value.h"
#include "reggroups.h"
#include "inferior.h"
#include "symfile.h"
#include "objfiles.h"
#include "osabi.h"
#include "language.h"
#include "arch-utils.h"
#include "regcache.h"
#include "trad-frame.h"
#include "dis-asm.h"
#include "gdb_assert.h"
#include "user-regs.h"
#include "elf-bfd.h"
#include "dwarf2-frame.h"
#include "ui-file.h"
#include "remote.h"
#include "target-descriptions.h"
#include "sim-regno.h"
#include "gdb/sim-nds32.h"

#include "nds32-tdep.h"
#include "elf/nds32.h"
#include "opcode/nds32.h"

/* Simple macro for chop LSB immediate bits from an instruction.  */
#define CHOP_BITS(insn, n)	(insn & ~__MASK (n))

extern void _initialize_nds32_tdep (void);

/* "break16 0" for breakpoint_from_pc.
   It is always supported now, so always insert break16.  */
static const gdb_byte NDS32_BREAK16[] = { 0xEA, 0x00 };

/* The standard register names.  */
static const char *nds32_regnames[] =
{
  /* 32 GPRs.  */
  "r0", "r1", "r2", "r3", "r4", "r5", "r6", "r7",
  "r8", "r9", "r10", "r11", "r12", "r13", "r14", "r15",
  "r16", "r17", "r18", "r19", "r20", "r21", "r22", "r23",
  "r24", "r25", "r26", "r27", "fp", "gp", "lp", "sp",

  /* 5 User Registers. */
  "pc", "d0lo", "d0hi", "d1lo", "d1hi",
};

/* Mnemonic names for registers.  */
struct nds32_register_alias
{
  const char *name;
  const char *alias;
};

/* Register alias for user_reg_map_name_to_regnum ().  */
static const struct nds32_register_alias nds32_register_aliases[] =
{
  {"r15", "ta"},
  {"r26", "p0"},
  {"r27", "p1"},
  {"fp", "r28"},
  {"gp", "r29"},
  {"lp", "r30"},
  {"sp", "r31"},

  {"ir0", "psw"},
  {"ir1", "ipsw"},
  {"ir2", "p_psw"},
  {"ir3", "ivb"},
  {"ir4", "eva"},
  {"ir5", "p_eva"},
  {"ir6", "itype"},
  {"ir7", "p_itype"},
  {"ir8", "merr"},
  {"ir9", "ipc"},
  {"ir10", "p_ipc"},
  {"ir11", "oipc"},
  {"ir12", "p_p0"},
  {"ir13", "p_p1"},
  {"ir14", "int_mask"},
  {"ir15", "int_pend"},

  {"cr0", "cpu_ver"},
  {"cr1", "icm_cfg"},
  {"cr2", "dcm_cfg"},
  {"cr3", "mmu_cfg"},
  {"cr4", "msc_cfg"},
  {"cr5", "core_id"},
  {"cr6", "fucop_exist"},

  {"mr0", "mmu_ctl"},
  {"mr1", "l1_pptb"},
  {"mr2", "tlb_vpn"},
  {"mr3", "tlb_data"},
  {"mr4", "tlb_misc"},
  {"mr5", "vlpt_idx"},
  {"mr6", "ilmb"},
  {"mr7", "dlmb"},
  {"mr8", "cache_ctl"},
  {"mr9", "hsmp_saddr"},
  {"mr10", "hsmp_eaddr"},

  {"pfr0", "pfmc0"},
  {"pfr1", "pfmc1"},
  {"pfr2", "pfmc2"},
  {"pfr3", "pfm_ctl"},

  {"dmar0", "dma_cfg"},
  {"dmar1", "dma_gcsw"},
  {"dmar2", "dma_chnsel"},
  {"dmar3", "dma_act"},
  {"dmar4", "dma_setup"},
  {"dmar5", "dma_isaddr"},
  {"dmar6", "dma_esaddr"},
  {"dmar7", "dma_tcnt"},
  {"dmar8", "dma_status"},
  {"dmar9", "dma_2dset"},
  {"dmar10", "dma_2dsctl"},
};

/* Value of a register alias.  BATON is register name of the alias,
   because system registers do not have fixed register number.
   We must look-up them when access.  */

static struct value *
nds32_value_of_reg (struct frame_info *frame, const void *baton)
{
  struct gdbarch *gdbarch = get_frame_arch (frame);
  int regnum;

  regnum = user_reg_map_name_to_regnum (gdbarch, (const char *) baton, -1);

  return value_of_register (regnum, frame);
}

/* Implement the gdbarch_frame_align method.  */

static CORE_ADDR
nds32_frame_align (struct gdbarch *gdbarch, CORE_ADDR sp)
{
  /* 8-byte aligned.  */
  return align_down (sp, 8);
}

/* Implement the gdbarch_breakpoint_from_pc method.  */

static const gdb_byte *
nds32_breakpoint_from_pc (struct gdbarch *gdbarch, CORE_ADDR *pcptr,
			  int *lenptr)
{
  gdb_assert (pcptr != NULL);
  gdb_assert (lenptr != NULL);

  if ((*pcptr) & 1)
    error (_("Bad address %p for inserting breakpoint.\n"
	     "Address must be at least 2-byte aligned."), (void *) *pcptr);

  /* Always insert 16-bit break instruction.  */
  *lenptr = 2;
  return NDS32_BREAK16;
}

/* Implement the gdbarch_remote_breakpoint_from_pc method.  */

static void
nds32_remote_breakpoint_from_pc (struct gdbarch *gdbarch, CORE_ADDR *pcptr,
				 int *kindptr)
{
  if ((*pcptr) & 1)
    error (_("bad address %p for inserting breakpoint"), (void *) *pcptr);

  /* ICEman/AICE have trouble on reading memory when the pcptr is P/A,
     but CPU is in V/A mode.  This code prevent GDB from reading memory.
     ICEman will read memory itself if needed.  */

  *kindptr = 2;
}

/* Implement the gdbarch_dwarf2_reg_to_regnum method.
   Map DWARF regnum from GCC to GDB regnum.  */

static int
nds32_dwarf_dwarf2_reg_to_regnum (struct gdbarch *gdbarch, int num)
{
  const int DXR = 34;
  const int FSR = 38;
  const int FDR = FSR + 32;

  if (num >= 0 && num < 32)			/* R0 - R31 */
    return num;
  else if (num >= DXR && num < DXR + 4)		/* D0/D1 */
    return num - DXR + NDS32_D0LO_REGNUM;
  else if (num >= FSR && num < FSR + 32)	/* FS */
    return num - FSR + user_reg_map_name_to_regnum (gdbarch, "fs0", -1);
  else if (num >= FDR && num < FDR + 32)	/* FD */
    return num - FDR + user_reg_map_name_to_regnum (gdbarch, "fd0", -1);

  /* No match, return a inaccessible register number.  */
  return gdbarch_num_regs (gdbarch) + gdbarch_num_pseudo_regs (gdbarch);
}

/* Implement gdbarch_register_sim_regno method.  */

static int
nds32_register_sim_regno (struct gdbarch *gdbarch, int regnum)
{
  /* Use target-descriptions for register mapping. */

  /* Only makes sense to supply raw registers.  */
  gdb_assert (regnum >= 0 && regnum < gdbarch_num_regs (gdbarch));

  if (regnum < NDS32_NUM_REGS)
    return regnum;
  if (regnum >= NDS32_SIM_FD0_REGNUM && regnum < NDS32_SIM_FD0_REGNUM + 32)
    return SIM_NDS32_FD0_REGNUM + regnum - NDS32_SIM_FD0_REGNUM;
  switch (regnum)
    {
    case NDS32_SIM_IFCLP_REGNUM:
      return SIM_NDS32_IFCLP_REGNUM;
    case NDS32_SIM_ITB_REGNUM:
      return SIM_NDS32_ITB_REGNUM;
    case NDS32_SIM_PSW_REGNUM:
      return SIM_NDS32_PSW_REGNUM;
    }

  return LEGACY_SIM_REGNO_IGNORE;
}

enum type_option
{
  NO_FLAGS = 0,
  USE_FLAGS = 1,
};

/* Allocate a new type.  Use this instead of arch_composite_type () or
   arch_type () directly, because it must be a 4-byte word, but we may
   not append all the fields.  If `opt' is USE_FLAGS, then a field for
   flags is appended.  */

static struct type *
nds32_init_type (struct gdbarch *gdbarch, char *name, enum type_option opt)
{
  struct type *type;

  gdb_assert (opt == USE_FLAGS || opt == NO_FLAGS);

  type = arch_type (gdbarch, TYPE_CODE_STRUCT, 4, name);
  TYPE_TAG_NAME (type) = name;
  INIT_CPLUS_SPECIFIC (type);

  if (opt == USE_FLAGS)
    {
      int i;
      struct type *flags_type =
	arch_flags_type (TYPE_OWNER (type).gdbarch, "nds32_dummy_flags_type",
			 TYPE_LENGTH (type));
      append_composite_type_field_raw (type, "", flags_type);

      /* Hide all the flags from users. Only explicitly appended flags
	 are shown to users.  For example,
	   {[ #1 #3 #16 #17 #18 ], INTL = Lv1, POM = Superuser}
	 We don't want users to see this, INTL and POM should not be
	 displayed in the flags field.  */
      for (i = 0; i < (int) TYPE_LENGTH (type) * TARGET_CHAR_BIT; i++)
	append_flags_type_flag (flags_type, i, NULL);
    }

  return type;
}

/* Append a flags bit.  */

static void
nds32_append_flag (struct type *type, int bitpos, char *name)
{
  int nfields;

  gdb_assert (TYPE_CODE (type) == TYPE_CODE_STRUCT);

  nfields = TYPE_NFIELDS (type);

  if (nfields == 0
      || TYPE_CODE (TYPE_FIELD_TYPE (type, 0)) != TYPE_CODE_FLAGS)
    {
      internal_error (__FILE__, __LINE__,
		      _("Pseudo field for flags must be the first one."));
    }

  /* Append flag in the first field.  */
  append_flags_type_flag (TYPE_FIELD_TYPE (type, 0), bitpos, name);
}

/* Append a bit-field.  */

static void
nds32_append_bitfield (struct type *type, struct type *field_type,
		       int bitpos_from, int bitpos_to, char *name)
{
  struct field *f;

  gdb_assert (TYPE_CODE (type) == TYPE_CODE_STRUCT);
  gdb_assert (bitpos_to >= bitpos_from);
  gdb_assert (TYPE_NFIELDS (type) != 0 || strcmp (name, "") != 0);

  f = append_composite_type_field_raw (type, xstrdup (name), field_type);
  SET_FIELD_BITPOS (f[0], bitpos_from);
  FIELD_BITSIZE (f[0]) = bitpos_to - bitpos_from + 1;
}

/* Allocate an enumeration type.  The only reason to call this function
   is that we want it to be UNSIGNED instead.  This is only useful for
   enum-type bit-field.  */

static struct type *
nds32_init_enum (struct gdbarch *gdbarch, char *name)
{
  struct type *type;

  type = arch_type (gdbarch, TYPE_CODE_ENUM, 4, name);
  TYPE_UNSIGNED (type) = 1;
  return type;
}

static void
nds32_append_enum (struct type *type, int enumval, char *name)
{
  struct field *f;

  gdb_assert (TYPE_CODE (type) == TYPE_CODE_ENUM);
  f = append_composite_type_field_raw (type, name, NULL);
  SET_FIELD_ENUMVAL (f[0], enumval);
}

/* Register name and its type.  */

struct type_entry
{
  const char *name;
  struct type *type;
};

/* Hash code function.  Simply a wrapper for htab_hash_string.
   This function should only be called by htab_expand,
   so PTR is the pointer to inserted entry.  */

static hashval_t
type_hash_string (const PTR ptr)
{
  struct type_entry *pe = (struct type_entry *) ptr;

  return htab_hash_string (pe->name);
}

/* Equal function for hash.  Simply a wrapper for strcmp.
   This function should only be called by htab_find[_slot]_with_hash,
   so P2 the ELEMENT argument to it.  */

static int
type_name_eq (const PTR p1, const PTR p2)
{
  struct type_entry *pe = (struct type_entry *) p1;
  const char *name = (const char *) p2;

  return strcmp (pe->name, name) == 0;
}

/* Allocate a hash table for register/type pair.  */

static htab_t
nds32_alloc_type_tab (int size)
{
  return htab_create_alloc (size, type_hash_string, type_name_eq,
			    NULL /* htab_del */ ,
			    xcalloc, xfree);
}

/* Look up a register by name.  */

static struct type *
nds32_type_lookup (htab_t htab, const char *name)
{
  struct type_entry *pe;
  hashval_t hash;

  hash = htab_hash_string (name);
  pe = (struct type_entry *) htab_find_with_hash (htab, name, hash);

  if (pe == NULL)
    return NULL;
  return pe->type;
}

/* Insert a type for a specific register name.  */

static void
nds32_type_insert (htab_t htab, const char *name, struct type *type)
{
  struct type_entry ent;
  struct type_entry **pe;
  hashval_t hash;

  hash = htab_hash_string (name);
  pe = (struct type_entry **)
    htab_find_slot_with_hash (htab, name, hash, INSERT);

  /* If the entry already exists, there must be a bug in nds32_alloc_types.  */
  gdb_assert (pe != NULL && *pe == NULL);

  *pe = xmalloc (sizeof (**pe));
  (*pe)->name = xstrdup (name);
  (*pe)->type = type;
}

/* Create types for registers and insert them to type table by name.  */

static void
nds32_alloc_types (struct gdbarch *gdbarch)
{
  const struct builtin_type *bt = builtin_type (gdbarch);
  struct gdbarch_tdep *tdep = gdbarch_tdep (gdbarch);
  struct type *type, *stype1, *stype2;

  tdep->type_tab = nds32_alloc_type_tab (24);

  /* fucpr */
  type = arch_flags_type (gdbarch, "builtin_type_nds32_fucpr", 4);
  append_flags_type_flag (type, 0, "CP0EN");
  append_flags_type_flag (type, 1, "CP1EN");
  append_flags_type_flag (type, 2, "CP2EN");
  append_flags_type_flag (type, 3, "CP3EN");
  append_flags_type_flag (type, 31, "AUEN");
  nds32_type_insert (tdep->type_tab, "fucpr", type);

  /* fpcfg */
  type = nds32_init_enum (gdbarch, "builtin_type_nds32_fpcfg");
  nds32_append_enum (type, 0, "8SP_4DP");
  nds32_append_enum (type, 1, "16SP_8DP");
  nds32_append_enum (type, 2, "32SP_16DP");
  nds32_append_enum (type, 3, "32SP_32DP");
  stype1 = type;

  type = nds32_init_type (gdbarch, "builtin_fpcfg_type", USE_FLAGS);
  nds32_append_flag (type, 0, "SP");
  nds32_append_flag (type, 1, "DP");
  nds32_append_bitfield (type, stype1, 2, 3, "FREG");
  nds32_append_flag (type, 4, "FMA");
  nds32_append_bitfield (type, bt->builtin_uint8, 22, 26, "IMVER");
  nds32_append_bitfield (type, bt->builtin_uint8, 27, 31, "AVER");
  nds32_type_insert (tdep->type_tab, "fpcfg", type);

  /* fpcsr */
  type = nds32_init_enum (gdbarch, "builtin_type_nds32_fpcsr_rm");
  nds32_append_enum (type, 0, "RTNE");
  nds32_append_enum (type, 1, "RTPI");
  nds32_append_enum (type, 2, "RTMI");
  nds32_append_enum (type, 3, "RTZ");
  stype1 = type;

  type = nds32_init_type (gdbarch, "builtin_type_nds32_fpcsr", USE_FLAGS);
  nds32_append_bitfield (type, stype1, 0, 1, "RM");
  nds32_append_flag (type, 2, "IVO");
  nds32_append_flag (type, 3, "DBZ");
  nds32_append_flag (type, 4, "OVF");
  nds32_append_flag (type, 5, "UDF");
  nds32_append_flag (type, 6, "IEX");
  nds32_append_flag (type, 7, "IVOE");
  nds32_append_flag (type, 8, "DBZE");
  nds32_append_flag (type, 9, "OVFE");
  nds32_append_flag (type, 10, "UDFE");
  nds32_append_flag (type, 11, "IEXE");
  nds32_append_flag (type, 12, "DNZ");
  nds32_append_flag (type, 13, "IVOT");
  nds32_append_flag (type, 14, "DBZT");
  nds32_append_flag (type, 15, "OVFT");
  nds32_append_flag (type, 16, "UDFT");
  nds32_append_flag (type, 17, "IEXT");
  nds32_append_flag (type, 18, "DNIT");
  nds32_append_flag (type, 19, "RIT");
  nds32_type_insert (tdep->type_tab, "fpcsr", type);

  /* ir0 - processor status word register
     ir1 - interruption PSW register
     ir2 - previous IPSW register */
  type = nds32_init_enum (gdbarch, "builtin_type_nds32_psw_intl");
  nds32_append_enum (type, 0, "NO");
  nds32_append_enum (type, 1, "Lv1");
  nds32_append_enum (type, 2, "Lv2");
  nds32_append_enum (type, 3, "Lv3");
  stype1 = type;

  type = nds32_init_enum (gdbarch, "builtin_type_nds32_psw_pow");
  nds32_append_enum (type, 0, "User");
  nds32_append_enum (type, 1, "Superuser");
  nds32_append_enum (type, 2, "Reserved(2)");
  nds32_append_enum (type, 3, "Reserved(3)");
  stype2 = type;

  type = nds32_init_type (gdbarch, "builtin_type_nds32_psw", USE_FLAGS);
  nds32_append_flag (type, 0, "GIE");
  nds32_append_bitfield (type, stype1, 1, 2, "INTL");
  nds32_append_bitfield (type, stype2, 3, 4, "POM");
  nds32_append_flag (type, 5, "BE");
  nds32_append_flag (type, 6, "IT");
  nds32_append_flag (type, 7, "DT");
  nds32_append_flag (type, 8, "IME");
  nds32_append_flag (type, 9, "DME");
  nds32_append_flag (type, 10, "DEX");
  nds32_append_flag (type, 11, "HSS");
  nds32_append_flag (type, 12, "DRBE");
  nds32_append_flag (type, 13, "AEN");
  nds32_append_flag (type, 14, "WBNA");
  nds32_append_flag (type, 15, "IFCON");
  nds32_append_flag (type, 20, "OV");
  nds32_type_insert (tdep->type_tab, "ir0", type);
  nds32_type_insert (tdep->type_tab, "ir1", type);
  nds32_type_insert (tdep->type_tab, "ir2", type);

  /* ir3 - Interrupt Vector Base Register */
  type = nds32_init_enum (gdbarch, "builtin_type_nds32_ivb_esz");
  nds32_append_enum (type, 0, "4_byte");
  nds32_append_enum (type, 1, "16_byte");
  nds32_append_enum (type, 2, "64_byte");
  nds32_append_enum (type, 3, "256_byte");
  stype1 = type;

  type = nds32_init_type (gdbarch, "builtin_type_nds32_ivb", USE_FLAGS);
  nds32_append_flag (type, 13, "EVIC");
  nds32_append_bitfield (type, stype1, 14, 15, "ESZ");
  nds32_append_bitfield (type, bt->builtin_uint16, 16, 31, "IVBASE");
  nds32_type_insert (tdep->type_tab, "ir3", type);

  /* ir6 - Interruption Type Register
     ir7 - Previous ITYPE */
  type = nds32_init_type (gdbarch, "builtin_type_nds32_itype", USE_FLAGS);
  nds32_append_bitfield (type, bt->builtin_uint8, 0, 3, "ETYPE");
  nds32_append_flag (type, 4, "INST");
  nds32_append_bitfield (type, bt->builtin_uint16, 16, 30, "SWID");
  nds32_type_insert (tdep->type_tab, "ir6", type);
  nds32_type_insert (tdep->type_tab, "ir7", type);

  /* ir14 - Interruption Masking Register */
  type = arch_flags_type (gdbarch, "builtin_type_nds32_int_mask", 4);
  append_flags_type_flag (type, 0, "H0IM");
  append_flags_type_flag (type, 1, "H1IM");
  append_flags_type_flag (type, 2, "H2IM");
  append_flags_type_flag (type, 3, "H3IM");
  append_flags_type_flag (type, 4, "H4IM");
  append_flags_type_flag (type, 5, "H5IM");
  append_flags_type_flag (type, 16, "SIM");
  append_flags_type_flag (type, 29, "ALZ");
  append_flags_type_flag (type, 30, "IDIVZE");
  append_flags_type_flag (type, 31, "DSSIM");
  nds32_type_insert (tdep->type_tab, "ir14", type);

  /* ir18 - Interruption Prioirty Register */
  type = nds32_init_type (gdbarch, "builtin_type_nds32_int_pri", NO_FLAGS);
  nds32_append_bitfield (type, bt->builtin_uint8, 0, 1, "H0PRI");
  nds32_append_bitfield (type, bt->builtin_uint8, 2, 3, "H1PRI");
  nds32_append_bitfield (type, bt->builtin_uint8, 4, 5, "H2PRI");
  nds32_append_bitfield (type, bt->builtin_uint8, 6, 7, "H3PRI");
  nds32_append_bitfield (type, bt->builtin_uint8, 8, 9, "H4PRI");
  nds32_append_bitfield (type, bt->builtin_uint8, 10, 11, "H5PRI");
  nds32_append_bitfield (type, bt->builtin_uint8, 12, 13, "H6PRI");
  nds32_append_bitfield (type, bt->builtin_uint8, 14, 15, "H7PRI");
  nds32_append_bitfield (type, bt->builtin_uint8, 16, 17, "H8PRI");
  nds32_append_bitfield (type, bt->builtin_uint8, 18, 19, "H9PRI");
  nds32_append_bitfield (type, bt->builtin_uint8, 20, 21, "H10PRI");
  nds32_append_bitfield (type, bt->builtin_uint8, 22, 23, "H11PRI");
  nds32_append_bitfield (type, bt->builtin_uint8, 24, 25, "H12PRI");
  nds32_append_bitfield (type, bt->builtin_uint8, 26, 27, "H13PRI");
  nds32_append_bitfield (type, bt->builtin_uint8, 28, 29, "H14PRI");
  nds32_append_bitfield (type, bt->builtin_uint8, 30, 31, "H15PRI");
  nds32_type_insert (tdep->type_tab, "ir18", type);

  /* mr0 - MMU Control Register */
  type = nds32_init_enum (gdbarch, "builtin_type_nds32_mmuctl_ntc");
  nds32_append_enum (type, 0, "NCA_NCO");
  nds32_append_enum (type, 1, "NCA_CO");
  nds32_append_enum (type, 2, "CA_WB");
  nds32_append_enum (type, 3, "CA_WT");
  stype1 = type;

  type = nds32_init_type (gdbarch, "builtin_type_nds32_mmu_ctl", USE_FLAGS);
  nds32_append_flag (type, 0, "D");
  nds32_append_bitfield (type, stype1, 1, 2, "NTC0");
  nds32_append_bitfield (type, stype1, 3, 4, "NTC1");
  nds32_append_bitfield (type, stype1, 5, 6, "NTC2");
  nds32_append_bitfield (type, stype1, 7, 8, "NTC3");
  nds32_append_flag (type, 9, "TBALCK");
  nds32_append_flag (type, 10, "MPZIU");
  nds32_append_bitfield (type, bt->builtin_uint8, 11, 12, "NTM0");
  nds32_append_bitfield (type, bt->builtin_uint8, 13, 14, "NTM1");
  nds32_append_bitfield (type, bt->builtin_uint8, 15, 16, "NTM2");
  nds32_append_bitfield (type, bt->builtin_uint8, 17, 18, "NTM3");
  nds32_append_flag (type, 19, "DREE");
  nds32_type_insert (tdep->type_tab, "mr0", type);

  /* mr1 */
  type = nds32_init_type (gdbarch, "builtin_type_nds32_l1_pptb", USE_FLAGS);
  nds32_append_flag (type, 0, "NV");
  nds32_append_bitfield (type, bt->builtin_uint32, 12, 31, "L1_PPT_BASE");
  nds32_type_insert (tdep->type_tab, "mr1", type);

  /* mr2 */
  type = nds32_init_type (gdbarch, "builtin_type_nds32_tlb_vpn", NO_FLAGS);
  nds32_append_bitfield (type, bt->builtin_uint32, 12, 31, "VPN");
  nds32_type_insert (tdep->type_tab, "mr2", type);

  /* mr3 */
  type = nds32_init_type (gdbarch, "builtin_type_nds32_tlb_data", USE_FLAGS);
  nds32_append_flag (type, 0, "V");
  nds32_append_bitfield (type, bt->builtin_uint8, 1, 3, "M");
  nds32_append_flag (type, 4, "D");
  nds32_append_flag (type, 5, "X");
  nds32_append_flag (type, 6, "A");
  nds32_append_flag (type, 7, "G");
  nds32_append_bitfield (type, bt->builtin_uint8, 8, 10, "C");
  nds32_append_bitfield (type, bt->builtin_uint32, 12, 31, "PPN");
  nds32_type_insert (tdep->type_tab, "mr3", type);

  /* mr4 - TLB Access Misc Register */
  type = nds32_init_enum (gdbarch, "builtin_type_nds32_tlb_misc_acc_psz");
  nds32_append_enum (type, 0, "4KB");
  nds32_append_enum (type, 1, "8KB");
  nds32_append_enum (type, 2, "16KB");
  nds32_append_enum (type, 3, "64KB");
  nds32_append_enum (type, 4, "256KB");
  nds32_append_enum (type, 5, "1MB");
  nds32_append_enum (type, 6, "4MB");
  nds32_append_enum (type, 7, "16MB");
  nds32_append_enum (type, 8, "64MB");
  nds32_append_enum (type, 9, "256MB");
  stype1 = type;

  type = nds32_init_type (gdbarch, "builtin_type_nds32_tlb_misc", NO_FLAGS);
  nds32_append_bitfield (type, stype1, 0, 3, "ACC_PSZ");
  nds32_append_bitfield (type, bt->builtin_uint32, 4, 12, "CID");
  nds32_type_insert (tdep->type_tab, "mr4", type);

  /* mr6 */
  type = nds32_init_enum (gdbarch, "builtin_type_nds32_ilm_size");
  nds32_append_enum (type, 0, "4KB");
  nds32_append_enum (type, 1, "8KB");
  nds32_append_enum (type, 2, "16KB");
  nds32_append_enum (type, 3, "32KB");
  nds32_append_enum (type, 4, "64KB");
  nds32_append_enum (type, 5, "128KB");
  nds32_append_enum (type, 6, "256KB");
  nds32_append_enum (type, 7, "512KB");
  nds32_append_enum (type, 8, "1024KB");
  nds32_append_enum (type, 9, "1KB");
  nds32_append_enum (type, 10, "2KB");
  nds32_append_enum (type, 15, "0KB");
  stype1 = type;

  type = nds32_init_type (gdbarch, "builtin_type_nds32_ilmb", NO_FLAGS);
  nds32_append_bitfield (type, bt->builtin_uint8, 0, 0, "IEN");
  nds32_append_bitfield (type, stype1, 1, 4, "ILMSZ");
  nds32_append_bitfield (type, bt->builtin_data_ptr, 0, 31, "(raw)");
  nds32_type_insert (tdep->type_tab, "mr6", type);

  /* mr7 */
  type = nds32_init_enum (gdbarch, "builtin_type_nds32_dlm_size");
  nds32_append_enum (type, 0, "4KB");
  nds32_append_enum (type, 1, "8KB");
  nds32_append_enum (type, 2, "16KB");
  nds32_append_enum (type, 3, "32KB");
  nds32_append_enum (type, 4, "64KB");
  nds32_append_enum (type, 5, "128KB");
  nds32_append_enum (type, 6, "256KB");
  nds32_append_enum (type, 7, "512KB");
  nds32_append_enum (type, 8, "1024KB");
  nds32_append_enum (type, 9, "1KB");
  nds32_append_enum (type, 10, "2KB");
  nds32_append_enum (type, 15, "0KB");
  stype1 = type;

  type = nds32_init_type (gdbarch, "builtin_type_nds32_dlmb", NO_FLAGS);
  nds32_append_bitfield (type, bt->builtin_uint8, 0, 0, "DEN");
  nds32_append_bitfield (type, stype1, 1, 4, "DLMSZ");
  nds32_append_bitfield (type, bt->builtin_uint8, 5, 5, "DBM");
  nds32_append_bitfield (type, bt->builtin_uint8, 6, 6, "DBB");
  nds32_append_bitfield (type, bt->builtin_data_ptr, 0, 31, "(raw)");
  nds32_type_insert (tdep->type_tab, "mr7", type);

  /* mr8 - Cache Control Register */
  type = arch_flags_type (gdbarch, "builtin_type_nds32_cache_ctl", 4);
  append_flags_type_flag (type, 0, "IC_EN");
  append_flags_type_flag (type, 1, "DC_EN");
  append_flags_type_flag (type, 2, "ICALCK");
  append_flags_type_flag (type, 3, "DCALCK");
  append_flags_type_flag (type, 4, "DCCWF");
  append_flags_type_flag (type, 5, "DCPMW");
  nds32_type_insert (tdep->type_tab, "mr8", type);

  /* dr40 - EDM Configuration Register */
  type = nds32_init_type (gdbarch, "builtin_type_nds32_edm_cfg", USE_FLAGS);
  nds32_append_bitfield (type, bt->builtin_uint8, 0, 2, "BC");
  nds32_append_flag (type, 3, "DIMU");
  nds32_append_flag (type, 4, "DALM");
  nds32_append_bitfield (type, bt->builtin_uint16, 16, 31, "VER");
  nds32_type_insert (tdep->type_tab, "dr40", type);

  /* dmar0 - DMA Configuration Register */
  type = nds32_init_type (gdbarch, "builtin_type_nds32_dma_cfg", USE_FLAGS);
  nds32_append_bitfield (type, bt->builtin_uint8, 0, 1, "NCHN");
  nds32_append_flag (type, 2, "UNEA");
  nds32_append_flag (type, 3, "2DET");
  nds32_append_bitfield (type, bt->builtin_uint16, 16, 31, "VER");
  nds32_type_insert (tdep->type_tab, "dmar0", type);

  /* cr0 - CPU Version Register */
  type = arch_flags_type (gdbarch, "builtin_type_nds32_cpuver_cfgid", 2);
  append_flags_type_flag (type, 0, "PERF_EXT");
  append_flags_type_flag (type, 1, "16_EXT");
  append_flags_type_flag (type, 2, "PERF_EXT2");
  append_flags_type_flag (type, 3, "COP_EXT");
  append_flags_type_flag (type, 4, "STR_EXT");
  stype1 = type;

  type = nds32_init_enum (gdbarch, "builtin_type_nds32_cpuver_cpuid");
  nds32_append_enum (type, 0x8, "N8");
  nds32_append_enum (type, 0x9, "N9");
  nds32_append_enum (type, 0xA, "N10");
  nds32_append_enum (type, 0xC, "N12");
  nds32_append_enum (type, 0xD, "N13");
  nds32_append_enum (type, 0xE, "N14");
  stype2 = type;

  type = nds32_init_type (gdbarch, "builtin_type_nds32_cpuver", NO_FLAGS);
  nds32_append_bitfield (type, stype1, 0, 15, "CFGID");
  nds32_append_bitfield (type, bt->builtin_uint8, 16, 23, "REV");
  nds32_append_bitfield (type, stype2, 24, 31, "CPUID");
  nds32_type_insert (tdep->type_tab, "cr0", type);

  /* cr4 - Misc Configuration Register */
  type = nds32_init_type (gdbarch, "builtin_type_nds32_msc_cfg", USE_FLAGS);
  nds32_append_flag (type, 0, "EDM");
  nds32_append_flag (type, 1, "LMDMA");
  nds32_append_flag (type, 2, "PFM");
  nds32_append_flag (type, 3, "HSMP");
  nds32_append_flag (type, 4, "TRACE");
  nds32_append_flag (type, 5, "DIV");
  nds32_append_flag (type, 6, "MAC");
  nds32_append_bitfield (type, bt->builtin_uint8, 7, 8, "AUDIO");
  nds32_append_flag (type, 9, "L2c");
  nds32_append_flag (type, 10, "RDREG");
  nds32_append_flag (type, 11, "ADR24");
  nds32_append_flag (type, 12, "INTLC");
  nds32_append_bitfield (type, bt->builtin_uint8, 13, 15, "BASEV");
  nds32_append_flag (type, 16, "NOD");
  nds32_append_flag (type, 17, "IMV");
  nds32_append_flag (type, 18, "IMR");
  nds32_append_flag (type, 19, "IFC");
  nds32_append_flag (type, 20, "MCU");
  nds32_type_insert (tdep->type_tab, "cr4", type);

  /* cr6 - FPU and Coprocessor Existence Configuration Register */
  type = arch_flags_type (gdbarch, "builtin_type_nds32_fucop_exist", 4);
  append_flags_type_flag (type, 0, "CP0EX");
  append_flags_type_flag (type, 1, "CP1EX");
  append_flags_type_flag (type, 2, "CP2EX");
  append_flags_type_flag (type, 3, "CP3EX");
  append_flags_type_flag (type, 31, "AUEX");
  nds32_type_insert (tdep->type_tab, "cr6", type);
}

/* Implement gdbarch_register_type method.

   Return the GDB type object for the "standard" data type of data in
   register REGNUM.  It get pretty messy here.  I want to specify a type
   on a bit-field for better representation, but they cannot be done by
   tdesc-xml.  */

static struct type *
nds32_register_type (struct gdbarch *gdbarch, int regnum)
{
  int num_regs = gdbarch_num_regs (gdbarch);
  const struct builtin_type *bt = builtin_type (gdbarch);
  struct gdbarch_tdep *tdep = gdbarch_tdep (gdbarch);
  struct type *type;
  const char *reg_name;

  /* Currently, only FSR are supported.  */
  if (regnum >= num_regs && regnum < num_regs + 32)
    return builtin_type (gdbarch)->builtin_float;
  else if (regnum >= num_regs)
    return NULL;

  /* NDS32 predefined types for specific registers.  */
  if (tdep->type_tab == NULL)
    nds32_alloc_types (gdbarch);

  reg_name = user_reg_map_regnum_to_name (gdbarch, regnum);
  if (reg_name == NULL)
    reg_name = "";
  type = nds32_type_lookup (tdep->type_tab, reg_name);
  if (type != NULL)
    return type;

  /* Type provided by target-description.  */
  if (tdesc_has_registers (gdbarch_target_desc (gdbarch)))
    {
      type = tdesc_register_type (gdbarch, regnum);
      if (type != NULL)
	return type;
    }

  /* Floating pointer registers. e.g., fs0 or fd0.  */
  if (strlen (reg_name) >= 3 && reg_name[0] == 'f' && reg_name[2] >= '0'
      && reg_name[2] <= '9')
    {
      if (reg_name[1] == 's')
	return bt->builtin_float;
      else if (reg_name[1] == 'd')
	return bt->builtin_double;
    }

  /* GPRs.  */
  if (regnum == NDS32_PC_REGNUM || regnum == NDS32_LP_REGNUM)
    return bt->builtin_func_ptr;
  else if (regnum == NDS32_SP_REGNUM || regnum == NDS32_FP_REGNUM
	   || regnum == NDS32_GP_REGNUM)
    return bt->builtin_data_ptr;
  else if (regnum < 32)
    return bt->builtin_int32;

  /* We don't know.  Display it in hex-form.  */
  return bt->builtin_data_ptr;
}


/* nds32 register groups.  */
static struct reggroup *nds32_cr_reggroup;
static struct reggroup *nds32_ir_reggroup;
static struct reggroup *nds32_mr_reggroup;
static struct reggroup *nds32_dr_reggroup;
static struct reggroup *nds32_pfr_reggroup;
static struct reggroup *nds32_dmar_reggroup;
static struct reggroup *nds32_racr_reggroup;
static struct reggroup *nds32_idr_reggroup;
static struct reggroup *nds32_audio_reggroup;

static void
nds32_init_reggroups (void)
{
  /* gpr usr sr */
  nds32_cr_reggroup = reggroup_new ("cr", USER_REGGROUP);
  nds32_ir_reggroup = reggroup_new ("ir", USER_REGGROUP);
  nds32_mr_reggroup = reggroup_new ("mr", USER_REGGROUP);
  nds32_dr_reggroup = reggroup_new ("dr", USER_REGGROUP);
  nds32_pfr_reggroup = reggroup_new ("pfr", USER_REGGROUP);
  nds32_dmar_reggroup = reggroup_new ("dmar", USER_REGGROUP);
  nds32_racr_reggroup = reggroup_new ("racr", USER_REGGROUP);
  nds32_idr_reggroup = reggroup_new ("idr", USER_REGGROUP);

  nds32_audio_reggroup = reggroup_new ("audio", USER_REGGROUP);
}

static void
nds32_add_reggroups (struct gdbarch *gdbarch)
{
  /* Target-independent groups.  */
  reggroup_add (gdbarch, general_reggroup);
  reggroup_add (gdbarch, float_reggroup);
  reggroup_add (gdbarch, all_reggroup);
  reggroup_add (gdbarch, system_reggroup);

  /* System register groups.  */
  reggroup_add (gdbarch, nds32_cr_reggroup);
  reggroup_add (gdbarch, nds32_ir_reggroup);
  reggroup_add (gdbarch, nds32_mr_reggroup);
  reggroup_add (gdbarch, nds32_dr_reggroup);
  reggroup_add (gdbarch, nds32_pfr_reggroup);
  reggroup_add (gdbarch, nds32_dmar_reggroup);
  reggroup_add (gdbarch, nds32_racr_reggroup);
  reggroup_add (gdbarch, nds32_idr_reggroup);
}

/* Implement the gdbarch_register_reggroup_p method.  */

static int
nds32_register_reggroup_p (struct gdbarch *gdbarch, int regnum,
			   struct reggroup *group)
{
  int i;
  struct reggroup *groups[] =
  {
      nds32_cr_reggroup, nds32_ir_reggroup, nds32_mr_reggroup,
      nds32_dr_reggroup, nds32_pfr_reggroup, nds32_dmar_reggroup,
      nds32_racr_reggroup, nds32_idr_reggroup
  };
  static const char *prefix[] =
  {
      "cr", "ir", "mr", "dr", "pfr", "dmar", "racr", "idr"
  };

  gdb_assert (ARRAY_SIZE (groups) == ARRAY_SIZE (prefix));

  /* GPRs. */
  if (group == general_reggroup)
    return regnum <= NDS32_PC_REGNUM;

  /* System Registers are grouped by prefix.  */
  else if (group == system_reggroup)
    return (regnum > NDS32_PC_REGNUM)
	   && TYPE_CODE (register_type (gdbarch, regnum)) != TYPE_CODE_FLT;

  for (i = 0; i < (int) ARRAY_SIZE (groups); i++)
    {
      if (group == groups[i])
	{
	  const char *regname = tdesc_register_name (gdbarch, regnum);

	  if (!regname)
	    return 0;
	  return strstr (regname, prefix[i]) == regname;
	}
    }

  return default_register_reggroup_p (gdbarch, regnum, group);
}

/* Implement the tdesc_pseudo_register_name method.

   This function is called when
   1. Target-description is used, and the register is pseudo.
   2. Target-description is NOT used, because
       i. the target is simulator,
      ii. or the target is legacy target, so tdesc is not supported.  */

static const char *
nds32_register_name (struct gdbarch *gdbarch, int regnum)
{
  static const char *fpu_pseudo_names[] =
  {
    "fs0", "fs1", "fs2", "fs3", "fs4", "fs5", "fs6", "fs7",
    "fs8", "fs9", "fs10", "fs11", "fs12", "fs13", "fs14", "fs15",
    "fs16", "fs17", "fs18", "fs19", "fs20", "fs21", "fs22", "fs23",
    "fs24", "fs25", "fs26", "fs27", "fs28", "fs29", "fs30", "fs31"
  };
  static const char *sim_names[] =
  {
    "fd0", "fd1", "fd2", "fd3", "fd4", "fd5", "fd6", "fd7",
    "fd8", "fd9", "fd10", "fd11", "fd12", "fd13", "fd14", "fd15",
    "fd16", "fd17", "fd18", "fd19", "fd20", "fd21", "fd22", "fd23",
    "fd24", "fd25", "fd26", "fd27", "fd28", "fd29", "fd30", "fd31",
    "ifclp", "itb", "ir0"
  };
  int num_regs = gdbarch_num_regs (gdbarch);

  /* Currently, only pseudo FSR are supported.  */
  if (regnum >= num_regs && regnum < num_regs + 32)
    return fpu_pseudo_names[regnum - num_regs];

  /* GPRs.  */
  if (regnum < (int) ARRAY_SIZE (nds32_regnames))
    return nds32_regnames[regnum];

  /* Registers between NUM_REGS and SMI_NUM_REGS are
     simulator registers.  */
  if (regnum >= NDS32_NUM_REGS && regnum < NDS32_SIM_NUM_REGS)
    return sim_names[regnum - NDS32_NUM_REGS];

  warning (_("Unknown nds32 pseudo register %d."), regnum);
  return NULL;
}

/* Implement the gdbarch_pseudo_register_read method.

   For legacy target, target-description and FPRs are not support.
   Use Rcmd to access FPU registers.  */

static enum register_status
nds32_pseudo_register_read (struct gdbarch *gdbarch,
			    struct regcache *regcache, int regnum,
			    gdb_byte *buf)
{
  char name_buf[8];
  gdb_byte reg_buf[8];
  int offset;
  enum register_status status = REG_UNKNOWN;

  /* Sanity check.  */
  regnum -= gdbarch_num_regs (gdbarch);
  if (regnum > gdbarch_num_pseudo_regs (gdbarch))
    return status;

  /* Currently, only FSR are supported.  */
  if (regnum < 32)
    {
      int fd_regnum;

      /* fs0 is always the high-part of fd0.  */
      if (gdbarch_byte_order (gdbarch) == BFD_ENDIAN_BIG)
	offset = (regnum & 1) ? 4 : 0;
      else
	offset = (regnum & 1) ? 0 : 4;

      xsnprintf (name_buf, sizeof (name_buf), "fd%d", regnum >> 1);
      fd_regnum = user_reg_map_name_to_regnum (gdbarch, name_buf,
					       strlen (name_buf));
      status = regcache_raw_read (regcache, fd_regnum, reg_buf);
      if (status == REG_VALID)
	memcpy (buf, reg_buf + offset, 4);
    }

  return status;
}

/* Implement the gdbarch_pseudo_register_write method.  */

static void
nds32_pseudo_register_write (struct gdbarch *gdbarch,
			     struct regcache *regcache, int regnum,
			     const gdb_byte *buf)
{
  char name_buf[8];
  gdb_byte reg_buf[8];
  int offset;

  /* Sanity check.  */
  regnum -= gdbarch_num_regs (gdbarch);
  if (regnum > gdbarch_num_pseudo_regs (gdbarch))
    return;

  /* Currently, only FSR are supported.  */
  if (regnum < 32)
    {
      int fd_regnum;

      /* fs0 is always the high-part of fd0.  */
      if (gdbarch_byte_order (gdbarch) == BFD_ENDIAN_BIG)
	offset = (regnum & 1) ? 4 : 0;
      else
	offset = (regnum & 1) ? 0 : 4;

      xsnprintf (name_buf, sizeof (name_buf), "fd%d", regnum >> 1);
      fd_regnum = user_reg_map_name_to_regnum (gdbarch, name_buf,
					       strlen (name_buf));
      regcache_raw_read (regcache, fd_regnum, reg_buf);
      memcpy (reg_buf + offset, buf, 4);
      regcache_raw_write (regcache, fd_regnum, reg_buf);
    }

  return;
}

/* Skip prologue should be conservative, and frame-unwind should be
   relative-aggressive.*/

static int
nds32_analyze_prologue (struct gdbarch *gdbarch, CORE_ADDR pc,
			CORE_ADDR scan_limit, CORE_ADDR *pl_endptr)
{
  uint32_t insn;
  CORE_ADDR cpc = -1;		/* Candidate PC if no suitable PC is found.  */

  /* If there is no buffer to store result, ignore this prologue decoding.  */
  if (pl_endptr == NULL)
    return 0;

  /* Look up end of prologue.  */
  for (; pc < scan_limit; )
    {
      insn = read_memory_unsigned_integer (pc, 4, BFD_ENDIAN_BIG);

      if ((insn & 0x80000000) == 0)
	{
	  /* 32-bit instruction.  */

	  pc += 4;
	  if (insn == N32_ALU1 (ADD, REG_GP, REG_TA, REG_GP))
	    {
	      /* add $gp, $ta, $gp */
	      continue;
	    }
	  else if (CHOP_BITS (insn, 15) == N32_TYPE2 (ADDI, REG_SP, REG_SP, 0))
	    {
	      /* addi $sp, $sp, imm15 */
	      cpc = pc;
	      continue;
	    }
	  else if (CHOP_BITS (insn, 15) == N32_TYPE2 (ADDI, REG_FP, REG_FP, 0))
	    {
	      /* addi $fp, $sp, imm15 */
	      cpc = pc;
	      continue;
	    }
	  else if (insn == N32_ALU2 (MFUSR, REG_TA, 31, 0))
	    {
	      /* mfusr $ta, PC  ; group=0, sub=0x20=mfusr */
	      continue;
	    }
	  else if (CHOP_BITS (insn, 20) == N32_TYPE1 (MOVI, REG_TA, 0))
	    {
	      /* movi $ta, imm20s */
	      continue;
	    }
	  else if (CHOP_BITS (insn, 20) == N32_TYPE1 (SETHI, REG_GP, 0))
	    {
	      /* sethi $gp, imm20 */
	      continue;
	    }
	  else if (CHOP_BITS (insn, 15) == N32_TYPE2 (ORI, REG_GP, REG_GP, 0))
	    {
	      /* ori $gp, $gp, imm15 */
	      continue;
	    }
	  else if (CHOP_BITS (insn, 15) == N32_TYPE2 (SWI, REG_LP, REG_FP, 0))
	    {
	      /* Unlike swi, we should stop when lwi.  */
	      /* swi $lp, [$sp + (imm15s<<2)] */
	      continue;
	    }
	  else if (CHOP_BITS (insn, 15) == N32_TYPE2 (SWI_BI, REG_LP, REG_FP, 0))
	    {
	      /* swi.bi $rt, [$sp], (imm15s<<2) */
	      continue;
	    }
	  else if (N32_OP6 (insn) == N32_OP6_LSMW && (insn & __BIT (5)))
	    {
	      /* bit-5 for SMW */

	      /* smwa?.(a|b)(d|i)m? rb,[ra],re,enable4 */
	      int ra;

	      ra = N32_RA5 (insn);

	      switch (ra)
		{
		case NDS32_FP_REGNUM:
		case NDS32_SP_REGNUM:
		  cpc = pc;
		  continue; /* found and continue */
		default:
		  break;
		}
	    }

	  if (N32_OP6 (insn) == N32_OP6_COP && N32_COP_CP (insn) == 0
	      && (N32_COP_SUB (insn) == N32_FPU_FSS
		  || N32_COP_SUB (insn) == N32_FPU_FSD)
	      && (N32_RA5 (insn) == REG_SP || N32_RA5 (insn) == REG_FP))
	    {
	      /* CP shoud be CP0 */
	      /* fs[sd][.bi] $fst, [$sp + ($r0 << sv)] */
	      continue;
	    }

	  /* fssi    $fst, [$ra + (imm12s << 2)]
	     fssi.bi $fst, [$ra], (imm12s << 2)
	     fsdi    $fdt, [$ra + (imm12s << 2)]
	     fsdi.bi $fdt, [$ra], (imm12s << 2) */
	  if ((N32_OP6 (insn) == N32_OP6_SWC || N32_OP6 (insn) == N32_OP6_SDC)
	      && (N32_RA5 (insn) == REG_SP || N32_RA5 (insn) == REG_FP))
	    {
	      /* BI bit is dont-care.  */
	      continue;
	    }

	  pc -= 4;
	  break;
	}
      else
	{
	  /* 16-bit instruction */
	  pc += 2;
	  insn >>= 16;

	  /* 1. If the instruction is j/b, then we stop
		i.e., OP starts with 10, and beqzs8, bnezs8.
	     2. If the operations will change sp/fp or based on sp/fp,
		then we are in the prologue.
	     3. If we don't know what's it, then stop.  */

	  if (CHOP_BITS (insn, 10) == N16_TYPE10 (ADDI10S, 0))
	    {
	      /* addi10s */
	      continue;
	    }
	  else if (__GF (insn, 7, 8) == N16_T25_PUSH25)
	    {
	      /* push25 */
	      continue;
	    }
	  else if (insn == N16_TYPE55 (MOV55, REG_FP, REG_SP))
	    {
	      /* mov55 fp, sp */
	      continue;
	    }

	  /* swi450 */
	  switch (insn & ~__MF (-1, 5, 4))
	    {
	      case N16_TYPE45 (SWI450, 0, REG_SP):
	      case N16_TYPE45 (SWI450, 0, REG_FP):
		break;
	    }
	  /* swi37 - implied fp */
	  if (__GF (insn, 11, 4) == N16_T37_XWI37
	      && (insn & __BIT (7)))
	    continue;

	  /* swi37sp - implied */
	  if (__GF (insn, 11, 4) == N16_T37_XWI37SP
	      && (insn & __BIT (7)))
	    continue;

	  /* If the a instruction is not accepted,
	     don't go futher.  */
	  pc -= 2;
	  break;
	}
    }

  if (pc >= scan_limit)
    {
      /* If we can not find end of prologue before scan_limit,
	 we assume that end of prologue is on pc_after_stack_adject. */
      if (cpc != -1)
	pc = cpc;
    }

  *pl_endptr = pc;

  return 0;
}

/* Implement the gdbarch_skip_prologue method.

   Find the end of function prologue.  */

static CORE_ADDR
nds32_skip_prologue (struct gdbarch *gdbarch, CORE_ADDR pc)
{
  CORE_ADDR func_addr, func_end;
  struct symtab_and_line sal = { 0 };
  LONGEST return_value;
  const char *func_name;
  enum bfd_endian byte_order = gdbarch_byte_order (gdbarch);
  const int search_limit = 128;

  /* See what the symbol table says */
  if (find_pc_partial_function (pc, &func_name, &func_addr, &func_end))
    {
      sal = find_pc_line (func_addr, 0);

      if (sal.line != 0 && sal.end <= func_end)
	func_end = sal.end;
      else
	{
	  /* Either there's no line info, or the line after the prologue
	     is after the end of the function.  In this case, there probably
	     isn't a prologue.  */
	  func_end = min (func_end, func_addr + search_limit);
	}
    }
  else
    func_end = pc + search_limit;

  /* If current instruction is not readable, just quit.  */
  if (!safe_read_memory_integer (pc, 4, byte_order, &return_value))
    return pc;

  /* Find the end of prologue.  */
  if (nds32_analyze_prologue (gdbarch, pc, func_end, &sal.end) < 0)
    return pc;

  return sal.end;
}

struct nds32_unwind_cache
{
  /* The previous frame's inner most stack address.
     Used as this frame ID's stack_addr.  */
  CORE_ADDR prev_sp;

  /* The frame's base, optionally used by the high-level debug info.  */
  CORE_ADDR base;
  int size;

  /* How far the SP and FP have been offset from the start of
     the stack frame (as defined by the previous frame's stack
     pointer).  */
  LONGEST sp_offset;
  LONGEST fp_offset;
  int use_frame;

  /* Table indicating the location of each and every register.  */
  struct trad_frame_saved_reg *saved_regs;
};

static struct nds32_unwind_cache *
nds32_alloc_frame_cache (struct frame_info *this_frame)
{
  struct nds32_unwind_cache *cache;

  cache = FRAME_OBSTACK_ZALLOC (struct nds32_unwind_cache);
  cache->saved_regs = trad_frame_alloc_saved_regs (this_frame);
  cache->size = 0;
  cache->sp_offset = 0;
  cache->fp_offset = 0;
  cache->use_frame = 0;
  cache->base = 0;

  return cache;
}

/* Implement the gdbarch_in_function_epilogue_p method.  */

static int
nds32_in_function_epilogue_p (struct gdbarch *gdbarch, CORE_ADDR addr)
{
  uint32_t insn;
  int r = 0;

  insn = read_memory_unsigned_integer (addr, 4, BFD_ENDIAN_BIG);
  if ((insn & 0x80000000) == 0)
    {
      /* 32-bit instruction */

      /* ret */
      if (insn == N32_JREG (JR, 0, REG_LP, 0, 1))
	r = 1;
      /* iret */
      else if (insn == N32_TYPE0 (MISC, N32_MISC_IRET))
	r = 2;
    }
  else
    {
      if (insn == N16_TYPE5 (RET5, REG_LP))
	r = 3;
    }
  return r > 0;
}

/* Put here the code to store, into fi->saved_regs, the addresses of
   the saved registers of frame described by FRAME_INFO.  This
   includes special registers such as pc and fp saved in special ways
   in the stack frame.  sp is even more special: the address we return
   for it IS the sp for the next frame.  */

static struct nds32_unwind_cache *
nds32_frame_unwind_cache (struct frame_info *this_frame,
			  void **this_prologue_cache)
{
  CORE_ADDR pc, scan_limit;
  ULONGEST prev_sp;
  ULONGEST next_base;
  ULONGEST fp_base;
  int i;
  uint32_t insn;
  struct nds32_unwind_cache *info;
  struct gdbarch *gdbarch = get_frame_arch (this_frame);

  if ((*this_prologue_cache))
    return (*this_prologue_cache);

  info = nds32_alloc_frame_cache (this_frame);

  info->base = get_frame_register_unsigned (this_frame, NDS32_FP_REGNUM);
  (*this_prologue_cache) = info;

  if (info->base == 0)
    return info;

  pc = get_frame_func (this_frame);
  scan_limit = get_frame_pc (this_frame);

  for (; pc > 0 && pc < scan_limit; )
    {
      insn = read_memory_unsigned_integer (pc, 4, BFD_ENDIAN_BIG);

      if ((insn & 0x80000000) == 0)
	{
	  /* 32-bit instruction */

	  pc += 4;
	  if (insn == N32_ALU1 (ADD, REG_GP, REG_TA, REG_GP))
	    {
	      /* add $gp, $ta, $gp */
	      continue;
	    }
	  if (N32_OP6 (insn) == N32_OP6_ADDI)
	    {
	      int rt = N32_RT5 (insn);
	      int ra = N32_RA5 (insn);
	      int imm15s = N32_IMM15S (insn);

	      if (rt == ra && rt == NDS32_SP_REGNUM)
		{
		  info->sp_offset += imm15s;
		  continue;
		}
	      else if (rt == NDS32_FP_REGNUM && ra == NDS32_SP_REGNUM)
		{
		  info->fp_offset = info->sp_offset + imm15s;
		  info->use_frame = 1;
		  continue;
		}
	      else if (rt == ra)
		/* Prevent stop analyzing form iframe.  */
		continue;
	    }

	  if (insn == N32_ALU2 (MFUSR, REG_TA, 31, 0))
	    {
	      /* mfusr $ta, PC  ; group=0, sub=0x20=mfusr */
	      continue;
	    }
	  if (CHOP_BITS (insn, 20) == N32_TYPE1 (MOVI, REG_TA, 0))
	    {
	      /* movi $ta, imm20s */
	      continue;
	    }
	  if (CHOP_BITS (insn, 20) == N32_TYPE1 (SETHI, REG_GP, 0))
	    {
	      /* sethi $gp, imm20 */
	      continue;
	    }
	  if (CHOP_BITS (insn, 15) == N32_TYPE2 (ORI, REG_GP, REG_GP, 0))
	    {
	      /* ori $gp, $gp, imm15 */
	      continue;
	    }
	  if (N32_OP6 (insn) == N32_OP6_LSMW && (insn & __BIT (5)))
	    {
	      /* smwa?.(a|b)(d|i)m? rb,[ra],re,enable4 */

	      int rb, re, ra, enable4, i;
	      int aligned;
	      int m = 0;
	      int di;	   /* dec=-1 or inc=1 */
	      int rn;	   /* number of registers.  */
	      char enb4map[2][4] = {
		  {0, 1, 2, 3} /* smw */,
		  {3, 1, 2, 0} /* smwa */ };
	      /* `base' is the highest/last address for access memory.
		 e.g., [ lp ] ___ base shoule be here.
		       [ fp ]
		       [ r6 ] */
	      ULONGEST base = -1;

	      rb = N32_RT5 (insn);
	      ra = N32_RA5 (insn);
	      re = N32_RB5 (insn);
	      enable4 = (insn >> 6) & 0x0F;
	      aligned = (insn & 3) ? 1 : 0;
	      di = (insn & (1 << 3)) ? -1 : 1;

	      rn = 0;
	      rn += (enable4 & 0x1) ? 1 : 0;
	      rn += (enable4 & 0x2) ? 1 : 0;
	      rn += (enable4 & 0x4) ? 1 : 0;
	      rn += (enable4 & 0x8) ? 1 : 0;
	      if (rb < NDS32_FP_REGNUM && re < NDS32_FP_REGNUM)
		{
		  /* reg-list should not include fp,gp,lp,sp
		     ie, the rb==re==sp case, anyway... */
		  rn += (re - rb) + 1;
		}

	      /* Let's consider how Ra should update.  */
	      if (insn & (1 << 0x2))    /* m-bit is set */
		{
		  m = rn * 4;			/* 4*TNReg */
		}
	      else
		m = 0;	  /* don't update Ra */

	      switch (ra)
		{
		case NDS32_FP_REGNUM:
		  base = info->fp_offset;
		  info->fp_offset += m * di;
		  break;
		case NDS32_SP_REGNUM:
		  base = info->sp_offset;
		  info->sp_offset += m * di;
		  break;
		default:
		  /* Only RA is FP or SP is handled.  */
		  base = -1;
		  break;
		}
	      if (base == -1)
		break;	  /* Exit the loop.  */

	      if (insn & (1 << 0x4))	/* b:0, a:1 */
		base += 4 * di;		/* a: use Ra+4 (for i),
					      or Ra-4 (for d) */
	      /* else base = base;	b use Ra */

	      /* We should consider both increasing and decreasing case.

		 Either case stores registers in the same order.
		 To simplify the code (yes, the loops),
		 I used the same pushing order, but from different side.  */

	      if (di == 1)		/* Increasing.  */
		base += (rn * 4 - 4);
	      /* else, in des case, we already are on the top */

	      for (i = 0; i < 4; i++)
		{
		  if (enable4 & (1 << enb4map[aligned][i]))
		    {
		      info->saved_regs[NDS32_SP_REGNUM -
				       (enb4map[aligned][i])].addr = base;
		      base -= 4;
		    }
		}

	      /* Skip re == rb == sp > fp.  */
	      for (i = re; i >= rb && rb < NDS32_FP_REGNUM; i--)
		{
		  info->saved_regs[i].addr = base;
		  base -= 4;
		}

	      continue;
	    }
	  /* swi $lp, [$sp + (imm15s << 2)] */
	  /* We must check if $rt is $lp to determine it is
	     in prologue or not.  */
	  if (CHOP_BITS (insn, 15) == N32_TYPE2 (SWI, REG_LP, REG_FP, 0))
	    {
	      int imm15s;

	      /* swi $lp, [$sp + (imm15s<<2)] */
	      imm15s = N32_IMM15S (insn);
	      info->saved_regs[NDS32_LP_REGNUM].addr = info->sp_offset
						       + (imm15s << 2);
	      continue;
	    }
	  /* swi.bi $rt, [$sp], (imm15s << 2) */
	  if (CHOP_BITS (insn, 15) == N32_TYPE2 (SWI_BI, REG_LP, REG_FP, 0))
	    {
	      unsigned int rt5 = 0;
	      unsigned int ra5 = 0;
	      int imm15s = 0;
	      rt5 = N32_RT5 (insn);
	      ra5 = N32_RA5 (insn);
	      imm15s = N32_IMM15S (insn);

	      if (ra5 == NDS32_SP_REGNUM)
		{
		  info->saved_regs[rt5].addr = info->sp_offset;
		  info->sp_offset += (imm15s << 2);
		}
	      else if (ra5 == NDS32_FP_REGNUM)
		{
		  info->saved_regs[rt5].addr = info->fp_offset;
		  info->fp_offset += (imm15s << 2);
		}
	      continue;
	    }

	  if (N32_OP6 (insn) == N32_OP6_COP && N32_COP_CP (insn) == 0
	      && (N32_COP_SUB (insn) == N32_FPU_FSS
		  || N32_COP_SUB (insn) == N32_FPU_FSD)
	      && (N32_RA5 (insn) == REG_SP || N32_RA5 (insn) == REG_FP))
	    {
	      /* CP shoud be CP0 */
	      /* fs[sd][.bi] $fst, [$sp + ($r0 << sv)] */
	      continue;
	    }

	  /* fssi $fst, [$ra + (imm12s << 2)]
	     fssi.bi $fst, [$ra], (imm12s << 2)
	     fsdi $fdt, [$ra + (imm12s << 2)]
	     fsdi.bi $fdt, [$ra], (imm12s << 2) */
	  if ((N32_OP6 (insn) == N32_OP6_SWC || N32_OP6 (insn) == N32_OP6_SDC)
	      && (N32_RA5 (insn) == REG_SP || N32_RA5 (insn) == REG_FP))
	    {
	      /* fssi and fsdi have the same form.  */
	      /* Only .bi form should be handled to adjust reg.  */
	      unsigned int ra5 = 0;
	      int imm12s = 0;

	      ra5 = N32_RA5 (insn);
	      imm12s = N32_IMM12S (insn);

	      if (imm12s & 0x800)
		imm12s = (imm12s - (0x800 << 1));

	      switch (ra5)
		{
		case NDS32_FP_REGNUM:
		  info->fp_offset += (imm12s << 2);
		  break;
		case NDS32_SP_REGNUM:
		  info->sp_offset += (imm12s << 2);
		  break;
		}

	      continue;
	    }

	  break;
	}
      else
	{
	  /* 16-bit instruction */
	  pc += 2;
	  insn >>= 16;

	  /* 1. If the instruction is j/b, then we stop
		i.e., OP starts with 10, and beqzs8, bnezs8.
	     2. If the operations will change sp/fp or based on sp/fp,
		then we are in the prologue.
	     3. If we don't know what's it, then stop.  */

	  if (__GF (insn, 13, 2) == 2)
	    {
	      /* These are all branch instructions.  */
	      pc -= 2;
	      break;
	    }
	  else if (__GF (insn, 9, 6) == 0x34)
	    {
	      /* beqzs8, bnezs8 */
	      pc -= 2;
	      break;
	    }

	  if (CHOP_BITS (insn, 10) == N16_TYPE10 (ADDI10S, 0))
	    {
	      /* addi10s */
	      info->sp_offset += N16_IMM10S (insn);
	      continue;
	    }

	  if (__GF (insn, 7, 8) == N16_T25_PUSH25)
	    {
	      /* push25 */
	      int imm8u = (insn & 0x1f) << 3;
	      int re = ((insn & 0x60) >> 5) & 0x3;
	      int m[] = {4, 6, 8, 12};

	      /* Operation 1 - smw.adm R6, [sp], Re, #0xe */
	      info->saved_regs[NDS32_LP_REGNUM].addr = info->sp_offset - 0x4;
	      info->saved_regs[NDS32_GP_REGNUM].addr = info->sp_offset - 0x8;
	      info->saved_regs[NDS32_FP_REGNUM].addr = info->sp_offset - 0xC;
	      info->sp_offset -= m[re] * 4;

	      switch (re)
		{
		  case 3:
		    info->saved_regs[14].addr = info->sp_offset + 0x20;
		    info->saved_regs[13].addr = info->sp_offset + 0x1C;
		    info->saved_regs[12].addr = info->sp_offset + 0x18;
		    info->saved_regs[11].addr = info->sp_offset + 0x14;
		  case 2:
		    info->saved_regs[10].addr = info->sp_offset + 0x10;
		    info->saved_regs[9].addr = info->sp_offset + 0xC;
		  case 1:
		    info->saved_regs[8].addr = info->sp_offset + 0x8;
		    info->saved_regs[7].addr = info->sp_offset + 0x4;
		  case 0:
		    info->saved_regs[6].addr = info->sp_offset;
		}

	      /* Operation 2 - sp = sp - imm5u<<3 */
	      info->sp_offset -= imm8u;

	      /* Operation 3 - if (Re >= 1) R8 = concat (PC(31,2), 2`b0) */
	      continue;
	    }

	  /* mov55 fp, sp */
	  if (insn == N16_TYPE55 (MOV55, REG_FP, REG_SP))
	    {
		info->fp_offset = info->sp_offset;
		info->use_frame = 1;
		continue;
	    }
	  /* swi450 */
	  switch (insn & ~__MF (-1, 5, 4))
	    {
	      case N16_TYPE45 (SWI450, 0, REG_SP):
	      case N16_TYPE45 (SWI450, 0, REG_FP):
		break;
	    }
	  /* swi37 - implied fp */
	  if (__GF (insn, 11, 4) == N16_T37_XWI37
	      && (insn & __BIT (7)))
	    continue;

	  /* swi37sp - implied */
	  if (__GF (insn, 11, 4) == N16_T37_XWI37SP
	      && (insn & __BIT (7)))
	    continue;

	  break;
	  }
    }

  info->size = -info->sp_offset;
  /* Compute the previous frame's stack pointer (which is also the
     frame's ID's stack address), and this frame's base pointer.

     Assume that the FP is this frame's SP but with that pushed
     stack space added back.  */
  next_base = get_frame_register_unsigned (this_frame, NDS32_SP_REGNUM);
  prev_sp = next_base + info->size;
  fp_base = get_frame_register_unsigned (this_frame, NDS32_FP_REGNUM);
  if (info->use_frame && fp_base > 0)
    {
      /* Try to use FP if possible. */
      prev_sp = fp_base - info->fp_offset;
    }

  /* Convert that SP/BASE into real addresses.  */
  info->prev_sp = prev_sp;
  info->base = next_base;

  /* Adjust all the saved registers so that they contain addresses and
     not offsets.  */
  for (i = 0; i < gdbarch_num_regs (gdbarch) - 1; i++)
    {
      if (trad_frame_addr_p (info->saved_regs, i))
	{
	  info->saved_regs[i].addr = info->prev_sp + info->saved_regs[i].addr;
	}
    }

  /* The previous frame's SP needed to be computed.
     Save the computed value.  */
  trad_frame_set_value (info->saved_regs, NDS32_SP_REGNUM, prev_sp);

  return info;
}

/* Implement the gdbarch_skip_permanent_breakpoint method.  */

static void
nds32_skip_permanent_breakpoint (struct regcache *regcache)
{
  gdb_byte insn[2];
  CORE_ADDR current_pc = regcache_read_pc (regcache);

  target_read_memory (current_pc, insn, sizeof (insn));

  if (memcmp (insn, NDS32_BREAK16, sizeof (insn)) != 0)
    return;

  current_pc += 2;
  regcache_write_pc (regcache, current_pc);
}

/* Implement the gdbarch_read_pc method.  */

static CORE_ADDR
nds32_read_pc (struct regcache *regcache)
{
  ULONGEST pc;
  regcache_cooked_read_unsigned (regcache, NDS32_PC_REGNUM, &pc);
  return pc;
}

/* Implement the gdbarch_write_pc method.  */

static void
nds32_write_pc (struct regcache *regcache, CORE_ADDR val)
{
  regcache_cooked_write_unsigned (regcache, NDS32_PC_REGNUM, val);
}

/* Implement the gdbarch_unwind_pc method.  */

static CORE_ADDR
nds32_unwind_pc (struct gdbarch *gdbarch, struct frame_info *this_frame)
{
  /* This snippet code is a mess.

     In most case, LP is the actually register being saved.
     Hence when unwinding pc for backtrace, LP should be the one.
     That is, for frames (level > 0), unwinding the PC means
     unwinding LP from the this_frame.

     However, for a top frame (level==0), unwinding PC means
     the current program counter (PC).
     Besides, for dummy frame, PC stored in dummy_frame is the one
     we want.

     We have to have these cases to make backtrace work properly.  */

  CORE_ADDR pc;
  pc = frame_unwind_register_unsigned (this_frame, NDS32_PC_REGNUM);
  return pc;
}

/* Implement the gdbarch_unwind_sp method.  */

static CORE_ADDR
nds32_unwind_sp (struct gdbarch *gdbarch, struct frame_info *this_frame)
{
  return frame_unwind_register_unsigned (this_frame, NDS32_SP_REGNUM);
}

/* If these is exactly one float point type field in the struct,
   the alignment of the struct is the size of the float pointer type.  */

static int
nds32_float_in_struct (struct type *type)
{
  struct type *actual_type;

  type = check_typedef (type);
  if (TYPE_CODE (type) != TYPE_CODE_STRUCT || TYPE_NFIELDS (type) != 1)
    return 0;

  actual_type = check_typedef (TYPE_FIELD_TYPE (type, 0));
  if (TYPE_CODE (actual_type) == TYPE_CODE_FLT)
    {
      gdb_assert (TYPE_LENGTH (type) == 8 || TYPE_LENGTH (type) == 4);
      return TYPE_LENGTH (type);
    }
  return 0;
}

/* Get the alignment of the type.

   The alignment requirement of a structure is the largest alignment
   requirement of its member, so we should traverse every member to
   find the largest alignment.

   For example,
     struct { int a; int b; char c } is 4-byte aligned,
   and
     struct {long long a; char c} is 8-byte aligned.  */

static int
nds32_type_align (struct type *type)
{
  int align = 0;		/* Current max alignment.  */
  int i;

  gdb_assert (type != NULL);
  if (type == NULL)
    return 0;

  if (type->main_type->nfields == 0)
    return type->length;

  switch (TYPE_CODE (type))
    {
    case TYPE_CODE_ARRAY:
      return nds32_type_align (TYPE_TARGET_TYPE (type));
    case TYPE_CODE_ENUM:
      return TYPE_LENGTH (type);
    }

  /* For structs with only one float/double are treated as float/double.  */
  align = nds32_float_in_struct (type);
  if (align != 0)
    return align;

  for (i = 0; i < TYPE_NFIELDS (type); i++)
    {
      int r = nds32_type_align (TYPE_FIELD_TYPE (type, i));

      if (r > align)
	align = r;
    }

  return align;
}

/* Implement the gdbarch_push_dummy_call method.  */

static CORE_ADDR
nds32_push_dummy_call (struct gdbarch *gdbarch, struct value *function,
		       struct regcache *regcache, CORE_ADDR bp_addr,
		       int nargs, struct value **args, CORE_ADDR sp,
		       int struct_return, CORE_ADDR struct_addr)
{
  const int REND = 6;		/* Max arguments number.  */
  int goff = 0;			/* Current gpr for argument.  */
  int foff = 0;			/* Currnet gpr for argument.  */
  int soff = 0;			/* Current stack offset.  */
  int i;
  enum type_code typecode;
  CORE_ADDR regval;
  enum bfd_endian byte_order = gdbarch_byte_order (gdbarch);
  struct gdbarch_tdep *tdep = gdbarch_tdep (gdbarch);
  int fs0_regnum = -1, fd0_regnum = -1;

  if (tdep->abi_use_fpr)
    {
      /* Use FP registers to pass arguments.  */
      fs0_regnum = user_reg_map_name_to_regnum (gdbarch, "fs0", -1);
      fd0_regnum = user_reg_map_name_to_regnum (gdbarch, "fd0", -1);
    }

  /* Set the return address.  For the nds32, the return breakpoint is
     always at BP_ADDR.  */
  regcache_cooked_write_unsigned (regcache, NDS32_LP_REGNUM, bp_addr);

  /* If STRUCT_RETURN is true, then the struct return address (in
     STRUCT_ADDR) will consume the first argument-passing register.
     Both adjust the register count and store that value.  */
  if (struct_return)
    {
      regcache_cooked_write_unsigned (regcache, NDS32_R0_REGNUM, struct_addr);
      goff++;
    }

  /* Now make sure there's space on the stack */
  for (i = 0; i < nargs; i++)
    {
      struct type *type = value_type (args[i]);
      int align = nds32_type_align (type);

      /* If align is zero, it may be an empty struct.
	 Just ignore the argument of empty struct.  */
      if (align == 0)
	continue;

      sp -= TYPE_LENGTH (type);
      sp = align_down (sp, align);
    }

  /* Allocate 24-byte for ABI V1.  */
  sp -= 24;
  /* Stack must be 8-byte aligned.  */
  sp = align_down (sp, 8);

  soff = 0;
  for (i = 0; i < nargs; i++)
    {
      const gdb_byte *val;
      int align, len;
      struct type *type;

      type = value_type (args[i]);
      typecode = TYPE_CODE (type);
      align = nds32_float_in_struct (type);
      if (align)
	typecode = TYPE_CODE_FLT;
      else
	align = nds32_type_align (type);
      len = TYPE_LENGTH (type);

      /* For current ABI, the caller pushes arguments in registers,
	 callee stores unnamed arguments in stack,
	 and then va_arg fetch arguments in stack.
	 Therefore, we don't have to handle variadic function specially.  */

      val = value_contents (args[i]);

      /* Once we start using stack, all arguments should go to stack
	 When use_fpr, all flt must go to fs/fd; otherwise go to stack.  */
      if (tdep->abi_use_fpr && typecode == TYPE_CODE_FLT)
	{
	  /* Adjust alignment.  */
	  if ((align >> 2) > 0)
	    foff = align_up (foff, align >> 2);

	  if (foff < REND && !soff)
	    {
	      if (tdep->abi_use_fpr && fs0_regnum == -1)
		goto error_no_fpr;

	      switch (len)
		{
		case 4:
		  regcache_cooked_write (regcache, fs0_regnum + foff, val);
		  foff++;
		  continue;
		case 8:
		  regcache_cooked_write (regcache, fd0_regnum + foff / 2, val);
		  foff += 2;
		  continue;
		default:
		  /* Long double? */
		  internal_error (__FILE__, __LINE__,
				  "Do not know how to handle %d-byte double.\n",
				  len);
		  break;
		}
	    }
	}
      else if (!soff)
	{
	  /* Adjust alignment, and only adjust one time for one argument.  */
	  if ((align >> 2) > 0)
	    goff = align_up (goff, align >> 2);
	  if (!tdep->abi_split && len > (REND - goff) * 4)
	    goff = REND;
	}

      /*
	When passing arguments,

	* A composite type not larger than 4 bytes is passed
	  in $rN. The format is as if the value is loaded with
	  load instruction of corresponding size. (i.g., LB, LH, LW)

	   For example,

		  r0
		  31      0
	  little: [x x b a]
	     BIG: [x x a b]

	 * Otherwise, a composite type is passed in consective registers.
	   The size is rounded up to the nearest multiple of 4.
	   The successive registers hold the parts of the argument as if
	   were loaded using lmw instructions.

	  For example,

		  r0	     r1
		  31      0 31	     0
	  little: [d c b a] [x x x e]
	     BIG: [a b c d] [e x x x]


	When push an argument in stack,

	* A composite type not larger than 4 bytes is copied
	  to memory at the next free space, in little-endian.
	  In big-endian, the last byte of the argument is aligned
	  at the next word address.  For example,

	  sp [ - ]  [ b ] hi
	     [ - ]  [ a ]
	     [ b ]  [ - ]
	     [ a ]  [ - ] lo
	    little   BIG
       */

      if (len > 4)
	len = align_up (len, 4);

      while (len > 0)
	{
	  if (soff
	      || (typecode == TYPE_CODE_FLT && tdep->abi_use_fpr && foff == REND)
	      || goff == REND)
	    {
	      int rlen = (len > 4) ? 4 : len;

	      if (byte_order == BFD_ENDIAN_BIG)
		write_memory (sp + soff + 4 - rlen, val, rlen);
	      else
		write_memory (sp + soff, val, rlen);
	      soff += 4;
	    }
	  else
	    {
	      regval = extract_unsigned_integer (val, (len > 4) ? 4 : len,
						 byte_order);
	      regcache_cooked_write_unsigned (regcache,
					      goff + NDS32_R0_REGNUM, regval);
	      goff++;
	    }

	  len -= register_size (gdbarch, goff);
	  val += register_size (gdbarch, goff);
	}
    }

  /* Finally, update the SP register.  */
  regcache_cooked_write_unsigned (regcache, NDS32_SP_REGNUM, sp);

  return sp;

error_no_fpr:
  /* If use_fpr, but no floating-point register exists,
     then it is an error.  */
  error (_("Fail to call. FS0-FS5 is required."));
}

/* Extract the value to be returned from REGCACHE and copy it into
   REGBUF.  */

static void
nds32_extract_return_value (struct type *type, struct regcache *regcache,
			    gdb_byte *readbuf)
{
  int len = TYPE_LENGTH (type);
  int typecode = TYPE_CODE (type);
  struct gdbarch *gdbarch = get_regcache_arch (regcache);
  struct gdbarch_tdep *tdep = gdbarch_tdep (gdbarch);
  int fs0_regnum, fd0_regnum;

  /* Although struct are returned in r0/r1 registers, but struct have
     only one single/double floating-point member are returned in FS/FD
     registers.  */
  gdb_assert (TYPE_LENGTH (type) <= 8);
  if (nds32_float_in_struct (type))
    typecode = TYPE_CODE_FLT;

  if (typecode == TYPE_CODE_FLT && tdep->abi_use_fpr)
    {
      fs0_regnum = user_reg_map_name_to_regnum (gdbarch, "fs0", -1);
      fd0_regnum = user_reg_map_name_to_regnum (gdbarch, "fd0", -1);

      if (len == 4)
	regcache_cooked_read (regcache, fs0_regnum, readbuf);
      else if (len == 8)
	regcache_cooked_read (regcache, fd0_regnum, readbuf);
      else
	internal_error (__FILE__, __LINE__,
			_("Cannot extract return value of %d bytes "
			  "long floating point."),
			len);
    }
  else
    {
      /* When returning result,

	* A composite type not larger than 4 bytes is returned
	  in $r0. The format is as if the result is loaded with
	  load instruction of corresponding size. (i.g., LB, LH, LW)

	  For example,

		  r0
		  31      0
	  little: [x x b a]
	     BIG: [x x a b]

	* Otherwise, a composite type not larger than 8 bytes
	  is returned in $r0 and $r1. In little-endian, the first
	  word is loaded in $r0. In big-endian, the last word
	  is loaded in $r1.

	  For example,

		  r0	    r1
		  31      0 31      0
	  little: [d c b a] [x x x e]
	     BIG: [x x x a] [b c d e]
       */

      if (len <= 4)
	{
	  if (gdbarch_byte_order (gdbarch) == BFD_ENDIAN_BIG)
	    regcache_raw_read_part (regcache, NDS32_R0_REGNUM, 4 - len, len,
				    readbuf);
	  else
	    regcache_raw_read_part (regcache, NDS32_R0_REGNUM, 0, len,
				    readbuf);
	}
      else if (len <= 8)
	{
	  int partial = len - 4;

	  if (gdbarch_byte_order (gdbarch) == BFD_ENDIAN_BIG)
	    {
	      regcache_raw_read_part (regcache, NDS32_R0_REGNUM, 4 - partial,
				      partial, readbuf);
	      regcache_raw_read (regcache, NDS32_R0_REGNUM + 1,
				 readbuf + partial);

	    }
	  else
	    {
	      regcache_raw_read (regcache, NDS32_R0_REGNUM, readbuf);
	      regcache_raw_read_part (regcache, NDS32_R0_REGNUM + 1, 0,
				      partial, readbuf + 4);
	    }
	}
      else
	internal_error (__FILE__, __LINE__,
			_("Cannot extract return value of %d bytes long."),
			len);
    }
}

/* Store the return value of TYPE in WRITEBUF into REGCACHE.  */

static void
nds32_store_return_value (struct type *type, struct regcache *regcache,
			  const gdb_byte *writebuf)
{
  int len = TYPE_LENGTH (type);
  int typecode = TYPE_CODE (type);
  struct gdbarch *gdbarch = get_regcache_arch (regcache);
  struct gdbarch_tdep *tdep = gdbarch_tdep (gdbarch);
  int fs0_regnum, fd0_regnum;

  /* Although struct are returned in r0/r1 registers, but struct have
     only one single/double floating-point member are returned in FS/FD
     registers.  */
  gdb_assert (TYPE_LENGTH (type) <= 8);
  if (nds32_float_in_struct (type))
    typecode = TYPE_CODE_FLT;

  if (typecode == TYPE_CODE_FLT && tdep->abi_use_fpr)
    {
      fs0_regnum = user_reg_map_name_to_regnum (gdbarch, "fs0", -1);
      fd0_regnum = user_reg_map_name_to_regnum (gdbarch, "fd0", -1);

      if (len == 4)
	regcache_cooked_write (regcache, fs0_regnum, writebuf);
      else if (len == 8)
	regcache_cooked_write (regcache, fd0_regnum, writebuf);
      else
	internal_error (__FILE__, __LINE__,
			_("Cannot store return value of %d bytes long "
			  "floating point."),
			len);
    }
  else
    {
      if (len <= 4)
	{
	  if (gdbarch_byte_order (gdbarch) == BFD_ENDIAN_BIG)
	    regcache_raw_write_part (regcache, NDS32_R0_REGNUM, 4 - len, len,
				     writebuf);
	  else
	    regcache_raw_write_part (regcache, NDS32_R0_REGNUM, 0, len,
				     writebuf);
	}
      else if (len <= 8)
	{
	  int partial = len - 4;

	  if (gdbarch_byte_order (gdbarch) == BFD_ENDIAN_BIG)
	    {
	      regcache_raw_write_part (regcache, NDS32_R0_REGNUM, 4 - partial,
				       partial, writebuf);
	      regcache_raw_write (regcache, NDS32_R0_REGNUM + 1,
				  writebuf + partial);

	    }
	  else
	    {
	      regcache_raw_write (regcache, NDS32_R0_REGNUM, writebuf);
	      regcache_raw_write_part (regcache, NDS32_R0_REGNUM + 1, 0,
				       partial, writebuf + 4);
	    }
	}
      else
	internal_error (__FILE__, __LINE__,
			_("Cannot store return value of %d bytes long."),
			len);
    }
}

/* Implement the gdbarch_return_value method.  */

static enum return_value_convention
nds32_return_value (struct gdbarch *gdbarch, struct value *func_type,
		    struct type *type, struct regcache *regcache,
		    gdb_byte *readbuf, const gdb_byte *writebuf)
{
  if (TYPE_LENGTH (type) > 8)
    {
      return RETURN_VALUE_STRUCT_CONVENTION;
    }
  else
    {
      /* `readbuf' is used for 'call' to get the return value.
	 `writebuf' is used for 'return' to set the return value.  */
      if (readbuf != NULL)
	nds32_extract_return_value (type, regcache, readbuf);
      if (writebuf != NULL)
	nds32_store_return_value (type, regcache, writebuf);
      return RETURN_VALUE_REGISTER_CONVENTION;
    }
}

/* Assuming NEXT_FRAME->prev is a dummy, return the frame ID of that
   dummy frame.  The frame ID's base needs to match the TOS value
   saved by save_dummy_frame_tos(), and the PC match the dummy frame's
   breakpoint.  */

static struct frame_id
nds32_dummy_id (struct gdbarch *gdbarch, struct frame_info *this_frame)
{
  CORE_ADDR sp, pc;

  sp = get_frame_register_unsigned (this_frame, NDS32_SP_REGNUM);
  pc = get_frame_pc (this_frame);
  return frame_id_build (sp, pc);
}

/* Given a GDB frame, determine the address of the calling function's
   frame.  This will be used to create a new GDB frame struct.  */

static void
nds32_frame_this_id (struct frame_info *this_frame,
		     void **this_prologue_cache, struct frame_id *this_id)
{
  struct nds32_unwind_cache *info;
  CORE_ADDR base;
  CORE_ADDR func;
  struct frame_id id;

  info = nds32_frame_unwind_cache (this_frame, this_prologue_cache);

  /* Get function entry address */
  func = get_frame_func (this_frame);

  /* Hopefully the prologue analysis either correctly determined the
     frame's base (which is the SP from the previous frame), or set
     that base to "NULL".  */
  base = info->prev_sp;
  if (base == 0)
    return;

  id = frame_id_build (base, func);
  (*this_id) = id;
}

/* Get the value of register REGNUM in previous frame.  */

static struct value *
nds32_frame_prev_register (struct frame_info *this_frame,
			   void **this_prologue_cache, int regnum)
{
  struct nds32_unwind_cache *cache;
  cache = nds32_frame_unwind_cache (this_frame, this_prologue_cache);

  if (regnum == NDS32_PC_REGNUM)
    {
      CORE_ADDR lr;

      lr = frame_unwind_register_unsigned (this_frame, NDS32_LP_REGNUM);
      return frame_unwind_got_constant (this_frame, regnum, lr);
    }

  return trad_frame_get_prev_register (this_frame, cache->saved_regs, regnum);
}

/* The register in previous frame.  For example, the previous PC is
   current LP.  */

static struct value *
nds32_dwarf2_prev_register (struct frame_info *this_frame,
			    void **this_cache, int regnum)
{
  CORE_ADDR lp;

  switch (regnum)
    {
    case NDS32_PC_REGNUM:
      lp = frame_unwind_register_unsigned (this_frame, NDS32_LP_REGNUM);
      return frame_unwind_got_constant (this_frame, regnum, lp);
    default:
      internal_error (__FILE__, __LINE__,
		      _("Unexpected register %d"), regnum);
    }

   return NULL;
}

/* Callback of dwarf2_frame_set_init_reg.  */

static void
nds32_dwarf2_frame_init_reg (struct gdbarch *gdbarch, int regnum,
			     struct dwarf2_frame_state_reg *reg,
			     struct frame_info *this_frame)
{
  switch (regnum)
    {
    case NDS32_PC_REGNUM:
      reg->how = DWARF2_FRAME_REG_FN;
      reg->loc.fn = nds32_dwarf2_prev_register;
      break;
    case NDS32_SP_REGNUM:
      reg->how = DWARF2_FRAME_REG_CFA;
      break;
    }
}

/* Implement the gdbarch_get_longjmp_target method.  */

static int
nds32_get_longjmp_target (struct frame_info *frame, CORE_ADDR *pc)
{
  gdb_byte buf[4];
  CORE_ADDR jmp_buf_p;
  struct gdbarch *gdbarch = get_frame_arch (frame);
  enum bfd_endian byte_order = gdbarch_byte_order (gdbarch);
  jmp_buf_p = get_frame_register_unsigned (frame, 0);

  /* Key is in setjmp():
     lmw.bim   r6, [r0], r14
     lmw.bim  r16, [r0], r19, 0xf */

  if (target_read_memory (jmp_buf_p + 15 * 4, buf, 4))
    return 0;

  *pc = extract_unsigned_integer (buf, 4, byte_order);

  return 1;
}

static const struct frame_unwind nds32_frame_unwind =
{
  NORMAL_FRAME,
  default_frame_unwind_stop_reason,
  nds32_frame_this_id,
  nds32_frame_prev_register,
  NULL /* unwind_data */,
  default_frame_sniffer,
  NULL /* dealloc_cache */,
  NULL /* prev_arch */
};

static CORE_ADDR
nds32_frame_base_address (struct frame_info *this_frame, void **this_cache)
{
  struct nds32_unwind_cache *info;

  info = nds32_frame_unwind_cache (this_frame, this_cache);

  return info->base;
}

static const struct frame_base nds32_frame_base =
{
  &nds32_frame_unwind,
  nds32_frame_base_address,
  nds32_frame_base_address,
  nds32_frame_base_address
};

/* Implement the gdbarch_overlay_update method.  */

static void
nds32_simple_overlay_update (struct obj_section *osect)
{
  struct minimal_symbol *minsym = NULL;

  minsym = lookup_minimal_symbol (".nds32.fixed.size", NULL, NULL);
  if (minsym != NULL && osect != NULL)
    {
      bfd *obfd = osect->objfile->obfd;
      asection *bsect = osect->the_bfd_section;
      if (bfd_section_vma (obfd, bsect) < SYMBOL_VALUE_ADDRESS (minsym))
	{
	  osect->ovly_mapped = 1;
	  return;
	}
    }

  simple_overlay_update (osect);
}

/* Implement gdbarch_print_insn method.  */

static int
gdb_print_insn_nds32 (bfd_vma memaddr, disassemble_info *info)
{
  struct obj_section * s = find_pc_section (memaddr);

  /* When disassembling ex9 instructions, they are annotated with
     the original instructions at the end of line.  For example,

	0x00500122 <+82>:    ex9.it #4		! movi $r13, 10

     Dissembler needs the symbol table to extract the original instruction
     in _ITB_BASE_ table.  If the object file is changed, reload symbol
     table.  */

  if (s == NULL || info->section != s->the_bfd_section)
    {
      xfree (info->symtab);
      info->symtab = NULL;
      info->symtab_size = 0;
    }

  if (info->symtab == NULL && s && s->the_bfd_section)
    {
      long storage = bfd_get_symtab_upper_bound (s->objfile->obfd);

      if (storage <= 0)
	goto done;

      info->section = s->the_bfd_section;
      info->symtab = xmalloc (storage);
      info->symtab_size =
	bfd_canonicalize_symtab (s->the_bfd_section->owner, info->symtab);
    }

done:
  return print_insn_nds32 (memaddr, info);
}

/* Callback for gdbarch_init.  */

static struct gdbarch *
nds32_gdbarch_init (struct gdbarch_info info, struct gdbarch_list *arches)
{
  struct gdbarch *gdbarch;
  struct gdbarch_tdep *tdep;
  struct gdbarch_list *best_arch;
  const struct target_desc *tdesc = NULL;
  struct tdesc_arch_data *tdesc_data = NULL;
  int i;

  /* Allocate space for the new architecture.  */
  tdep = XCALLOC (1, struct gdbarch_tdep);
  gdbarch = gdbarch_alloc (&info, tdep);

  if (tdesc_has_registers (info.target_desc))
    {
      int valid_p;
      int fpregs = -1;
      static const char *const nds32_fp_names[] = { "r28", "fp", NULL };
      static const char *const nds32_lp_names[] = { "r30", "lp", NULL };
      static const char *const nds32_sp_names[] = { "r31", "sp", NULL };
      const struct tdesc_feature *feature;

      /* Validate and initialize target-description here.  */
      tdesc = info.target_desc;
      tdesc_data = tdesc_data_alloc ();

      info.tdep_info = (void *) tdesc_data;

      feature = tdesc_find_feature (tdesc, "org.gnu.gdb.nds32.core");
      if (!feature)
	return 0;

      /* Validate  for FP, LP, GP, PC.  */
      valid_p = 1;
      valid_p &= tdesc_numbered_register_choices (feature, tdesc_data,
						  NDS32_FP_REGNUM,
						  nds32_fp_names);
      valid_p &= tdesc_numbered_register_choices (feature, tdesc_data,
						  NDS32_LP_REGNUM,
						  nds32_lp_names);
      valid_p &= tdesc_numbered_register_choices (feature, tdesc_data,
						  NDS32_SP_REGNUM,
						  nds32_sp_names);
      valid_p &= tdesc_numbered_register (feature, tdesc_data,
					  NDS32_PC_REGNUM, "pc");

      if (!valid_p)
	{
	  tdesc_data_cleanup (tdesc_data);
	  xfree (tdep);
	  gdbarch_free (gdbarch);
	  return NULL;
	}

      /* Number R0-R27.  */
      for (i = NDS32_R0_REGNUM; i < NDS32_FP_REGNUM; i++)
	tdesc_numbered_register (feature, tdesc_data, i, nds32_regnames[i]);

      /* Number D0 and D1.  */
      for (i = NDS32_D0LO_REGNUM; i <= NDS32_D1HI_REGNUM; i++)
	tdesc_numbered_register (feature, tdesc_data, i, nds32_regnames[i]);

      /* Find register configuration of FPU.  */
      feature = tdesc_find_feature (tdesc, "org.gnu.gdb.nds32.fpu");
      if (feature != NULL)
	{
	  if (tdesc_unnumbered_register (feature, "fd31"))
	    fpregs = 3;
	  else if (tdesc_unnumbered_register (feature, "fd15"))
	    fpregs = 2;
	  else if (tdesc_unnumbered_register (feature, "fd7"))
	    fpregs = 1;
	  else if (tdesc_unnumbered_register (feature, "fd3"))
	    fpregs = 0;
	}
      tdep->fpu_freg = fpregs;

      /* If FS registers do not exist, make them pseudo registers
	 of FD registers.  */
      if (fpregs != -1 && tdesc_unnumbered_register (feature, "fs0") == 0)
	{
	  int fsregs = (fpregs + 1) * 8;

	  if (fsregs > 32)
	    fsregs = 32;

	  set_gdbarch_num_pseudo_regs (gdbarch, fsregs);
	  set_gdbarch_pseudo_register_read (gdbarch, nds32_pseudo_register_read);
	  set_gdbarch_pseudo_register_write (gdbarch, nds32_pseudo_register_write);
	  set_tdesc_pseudo_register_name (gdbarch, nds32_register_name);
	}

      set_gdbarch_num_regs (gdbarch, NDS32_NUM_REGS);
      tdesc_use_registers (gdbarch, tdesc, tdesc_data);
    }
  else
    {
      /* Simulator or legacy target.  */

      /* Physical 32 FD registers.  */
      set_gdbarch_num_regs (gdbarch, NDS32_SIM_NUM_REGS);
      set_gdbarch_register_name (gdbarch, nds32_register_name);

      /* Pseudo 32 FS registers.  */
      set_gdbarch_num_pseudo_regs (gdbarch, 32);
      set_gdbarch_pseudo_register_read (gdbarch, nds32_pseudo_register_read);
      set_gdbarch_pseudo_register_write (gdbarch, nds32_pseudo_register_write);
    }

  /* Extract the elf_flags, if available.  */
  if (info.abfd && bfd_get_flavour (info.abfd) == bfd_target_elf_flavour)
    {
      int eflags = elf_elfheader (info.abfd)->e_flags;
      int nds32_abi = eflags & EF_NDS_ABI;

      /* Split large arguments by default.  */
      tdep->abi_split = TRUE;
      /* Do not use FPRs by default.  */
      tdep->abi_use_fpr = FALSE;
      switch (nds32_abi)
	{
	case E_NDS_ABI_V2FP:
	  tdep->abi_use_fpr = TRUE;
	  /* Fall-through.  */
	case E_NDS_ABI_V2:
	  tdep->abi_split = FALSE;
	}
    }

  /* If there is already a candidate, use it.  */
  for (best_arch = gdbarch_list_lookup_by_info (arches, &info);
       best_arch != NULL;
       best_arch = gdbarch_list_lookup_by_info (best_arch->next, &info))
    {
      struct gdbarch_tdep *idep = gdbarch_tdep (best_arch->gdbarch);

      if (tdep->abi_split != idep->abi_split)
	continue;
      if (tdep->abi_use_fpr != idep->abi_use_fpr)
	continue;

      /* Check FPU registers.  */
      if (idep->fpu_freg != tdep->fpu_freg)
	continue;

      /* Found a match.  */
      break;
    }

  if (best_arch != NULL)
    {
      xfree (tdep);
      gdbarch_free (gdbarch);
      return best_arch->gdbarch;
    }

  nds32_add_reggroups (gdbarch);

  gdbarch_init_osabi (info, gdbarch);

  /* Override tdesc_register callbacks for system registers.  */
  set_gdbarch_register_reggroup_p (gdbarch, nds32_register_reggroup_p);
  set_gdbarch_register_type (gdbarch, nds32_register_type);

  set_gdbarch_sp_regnum (gdbarch, NDS32_SP_REGNUM);
  set_gdbarch_pc_regnum (gdbarch, NDS32_PC_REGNUM);
  set_gdbarch_read_pc (gdbarch, nds32_read_pc);
  set_gdbarch_write_pc (gdbarch, nds32_write_pc);
  set_gdbarch_unwind_sp (gdbarch, nds32_unwind_sp);
  set_gdbarch_unwind_pc (gdbarch, nds32_unwind_pc);
  set_gdbarch_in_function_epilogue_p (gdbarch, nds32_in_function_epilogue_p);
  set_gdbarch_dwarf2_reg_to_regnum (gdbarch, nds32_dwarf_dwarf2_reg_to_regnum);
  set_gdbarch_register_sim_regno (gdbarch, nds32_register_sim_regno);
  set_gdbarch_push_dummy_call (gdbarch, nds32_push_dummy_call);
  set_gdbarch_return_value (gdbarch, nds32_return_value);
  set_gdbarch_skip_prologue (gdbarch, nds32_skip_prologue);
  set_gdbarch_inner_than (gdbarch, core_addr_lessthan);
  set_gdbarch_breakpoint_from_pc (gdbarch, nds32_breakpoint_from_pc);
  set_gdbarch_remote_breakpoint_from_pc (gdbarch,
					 nds32_remote_breakpoint_from_pc);

  set_gdbarch_frame_align (gdbarch, nds32_frame_align);
  frame_base_set_default (gdbarch, &nds32_frame_base);

  /* Methods for saving / extracting a dummy frame's ID.
     The ID's stack address must match the SP value returned by
     PUSH_DUMMY_CALL, and saved by generic_save_dummy_frame_tos.  */
  set_gdbarch_dummy_id (gdbarch, nds32_dummy_id);
  set_gdbarch_print_insn (gdbarch, gdb_print_insn_nds32);
  set_gdbarch_skip_permanent_breakpoint (gdbarch,
					 nds32_skip_permanent_breakpoint);
  /* Support simple overlay manager.  */
  set_gdbarch_overlay_update (gdbarch, nds32_simple_overlay_update);

  /* Handle longjmp.  */
  set_gdbarch_get_longjmp_target (gdbarch, nds32_get_longjmp_target);

  /* The order of appending is the order it check frame.  */
  dwarf2_frame_set_init_reg (gdbarch, nds32_dwarf2_frame_init_reg);
  dwarf2_append_unwinders (gdbarch);
  frame_unwind_append_unwinder (gdbarch, &nds32_frame_unwind);

  /* Add nds32 register aliases.  */
  /* FIXME: This is a simple workaround to force user-reg being
	    initialized before gdbarch initialized.
	    Without this, calling user_reg_map_name_to_regnum ()
	    will crash.  */
  user_reg_add (gdbarch, "r0", nds32_value_of_reg, "r0");
  for (i = 0; i < (int) ARRAY_SIZE (nds32_register_aliases); i++)
    {
      int regnum;

      regnum = user_reg_map_name_to_regnum
	(gdbarch, nds32_register_aliases[i].name, -1);

      if (regnum == -1)
	continue;

      user_reg_add (gdbarch, nds32_register_aliases[i].alias,
		    nds32_value_of_reg, nds32_register_aliases[i].name);
    }

  return gdbarch;
}

void
_initialize_nds32_tdep (void)
{
  /* Initialize gdbarch.  */
  register_gdbarch_init (bfd_arch_nds32, nds32_gdbarch_init);

  nds32_init_reggroups ();

  register_remote_support_xml ("nds32");
}
