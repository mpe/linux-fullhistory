/* XXX fix me! */
#define tlb_start_vma(tlb, vma)			do { } while (0)
#define tlb_end_vma(tlb, vma)			do { } while (0)
#define tlb_remove_tlb_entry(tlb, pte, address) do { } while (0)
#define tlb_flush(tlb)				flush_tlb_mm((tlb)->mm)

#include <asm-generic/tlb.h>
