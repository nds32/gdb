#ifndef ELF32_NDS32_H
#define ELF32_NDS32_H

/*
 * Relocation flags encoded in r_addend.
 */

/* Relocation flags for R_NDS32_ERLAX_ENTRY.  */

/* Set if relax on this section is done or disabled.  */
#define R_NDS32_RELAX_ENTRY_DISABLE_RELAX_FLAG			(1 << 31)
/* Optimize for performance.  */
#define R_NDS32_RELAX_ENTRY_OPTIMIZE_FLAG			(1 << 30)
/* Optimize for size.  Branch destination 4-byte adjustment
   may be disabled.  */
#define R_NDS32_RELAX_ENTRY_OPTIMIZE_FOR_SPACE_FLAG		(1 << 29)
#define R_NDS32_RELAX_ENTRY_OPTIMIZE_FOR_SPACE_UNALIGN_FLAG	(1 << 28)
/* EX9 and link-time IFC must be explicitly enabled, so we
   won't mess up handcraft assembly code.  */
/* Enable EX9 optimization for this section.  */
#define R_NDS32_RELAX_ENTRY_EX9_FLAG				(1 << 2)
/* Enable IFC optimization for this section.  */
#define R_NDS32_RELAX_ENTRY_IFC_FLAG				(1 << 3)


/* Relocation flags for R_NDS32_INSN16.  */

#define R_NDS32_INSN16_CONVERT_FLAG				(1 << 0)
/* Convert a gp-relative access (e.g., lwi.gp)
   to fp-as-gp access (lwi37.fp).
   This value is used by linker internally only.
   It's fine to change the vlaue.  */
#define R_NDS32_INSN16_FP7U2_FLAG				(1 << 1)

/* Relocation flags for R_NDS32_RELAX_REGION_OMIT_FP_START/END.  */

/* OMIT_FP_FLAG marks the region for applying fp-as-gp
   optimization.  */
#define R_NDS32_RELAX_REGION_OMIT_FP_FLAG			(1 << 0)
/* NOT_OMIT_FP_FLAG is set if this region is not worth
   for fp-as-gp.  */
#define R_NDS32_RELAX_REGION_NOT_OMIT_FP_FLAG			(1 << 1)
/* Suppress EX9 optimization in the region.  */
#define R_NDS32_RELAX_REGION_NO_EX9_FLAG			(1 << 2)
/* A Innermost loop region.  Some optimizations is suppressed
   in this region due to performance drop.  */
#define R_NDS32_RELAX_REGION_INNERMOST_LOOP_FLAG		(1 << 4)

/* Relax tag for nds32_elf_relax_section, we have to specify which
   optimization do in this round.  */
enum
{
  NDS32_RELAX_NONE_ROUND = 0,
  NDS32_RELAX_JUMP_IFC_ROUND = 1,
  NDS32_RELAX_EX9_BUILD_ROUND,
  NDS32_RELAX_EX9_REPLACE_ROUND

};

/* Optimization status mask.  */
#define NDS32_RELAX_JUMP_IFC_DONE	(1 << 0)
#define NDS32_RELAX_EX9_DONE		(1 << 1)

/* Optimization turn on mask.  */
#define NDS32_RELAX_JUMP_IFC_ON		(1 << 0)
#define NDS32_RELAX_EX9_ON		(1 << 1)

void nds32_insertion_sort
  (void *base, size_t nmemb, size_t size,
   int (*compar) (const void *lhs, const void *rhs));

int nds32_elf_ex9_init (void);
void nds32_elf_ex9_reloc_jmp (struct bfd_link_info *link_info);
void nds32_elf_ex9_finish (struct bfd_link_info *link_info, bfd_boolean);
bfd_boolean nds32_elf_ex9_itb_base (struct bfd_link_info *info);
void nds32_elf_ex9_import_table (struct bfd_link_info *link_info);

bfd_boolean nds32_elf_ifc_reloc (void);
bfd_boolean nds32_elf_ifc_finish (void);

int nds32_convert_32_to_16
  (bfd *abfd, uint32_t insn, uint16_t *pinsn16, int *pinsn_type);
int nds32_convert_16_to_32 (bfd *abfd, uint16_t insn16, uint32_t *pinsn);

#endif
