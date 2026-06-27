/* include/nam/abi.h
 *
 * The frozen C-compatible ABI for the automaton tier.
 *
 * RULES (Phase 1, Section 2):
 *   - This file is plain C POD. No C++ above this line.
 *   - These structs are VERSIONED and must NEVER be silently reordered.
 *   - sizeof(AutomatonVM) == 40  (4 + 4 + 4*8)
 *   - AutomatonVM is trivially copyable and standard layout: fork is a
 *     literal struct copy, O(1), with no hidden state.
 */
#ifndef NAM_ABI_H
#define NAM_ABI_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ABI version. Bump on ANY layout change. */
#define NAM_ABI_VERSION 1

typedef struct {
    uint32_t base; /* codec selector -- NOT baked into the number */
    uint32_t phase; /* periodic-orbit phase / index                */
    uint64_t state[4]; /* over-provisioned for degree-4 algebraic     */
} AutomatonVM;

typedef struct {
    uint32_t digit;
    AutomatonVM next;
} NumVMStep;

typedef NumVMStep (*NumVMFn)(AutomatonVM);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* NAM_ABI_H */
