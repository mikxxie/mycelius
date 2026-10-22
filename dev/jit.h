#pragma once
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

/* Mycelius JIT (Just-In-Time) compilation interface.
 *
 * Mycelius uses JIT compilation for user-space applications instead of
 * AOT (Ahead-Of-Time) compilation. This allows:
 *   - Dynamic code generation at runtime
 *   - Runtime optimization based on actual usage patterns
 *   - Smaller binaries (no need to ship compiled code)
 *   - Security: code is generated and verified at runtime
 *
 * The JIT compiler compiles a simple stack-based bytecode into
 * native AArch64 machine code that can be executed directly.
 */

/* JIT compilation status */
#define MYCELIUS_JIT_OK           0
#define MYCELIUS_JIT_ERR_INVALID  -1   /* Invalid bytecode */
#define MYCELIUS_JIT_ERR_NOMEM    -2   /* Out of memory */
#define MYCELIUS_JIT_ERR_UNSUPPORTED -3 /* Unsupported instruction */
#define MYCELIUS_JIT_ERR_TOO_BIG  -4   /* Code too large for buffer */

/* JIT compilation flags */
#define MYCELIUS_JIT_DEBUG        0x01   /* Emit debug info */
#define MYCELIUS_JIT_OPTIMIZE     0x02   /* Enable optimizations */
#define MYCELIUS_JIT_PROFILE      0x04   /* Enable profiling */

/* JIT compilation context - opaque handle */
typedef struct mycelius_jit_context mycelius_jit_t;

/* Create a new JIT compilation context.
 * Returns NULL on failure.
 */
mycelius_jit_t *mycelius_jit_create(void);

/* Destroy a JIT compilation context. */
void mycelius_jit_destroy(mycelius_jit_t *jit);

/* Compile bytecode into native AArch64 machine code.
 *
 * jit: JIT context
 * bytecode: pointer to bytecode bytes
 * bytecode_len: number of bytes in bytecode
 * out_buf: output buffer for native code (caller must allocate)
 * out_size: input: buffer size, output: bytes written
 * flags: JIT flags (MYCELIUS_JIT_*)
 *
 * Returns MYCELIUS_JIT_OK on success, negative error code otherwise.
 */
int mycelius_jit_compile(mycelius_jit_t *jit,
                         const u8 *bytecode, u32 bytecode_len,
                         u8 *out_buf, u32 *out_size,
                         u32 flags);

/* Execute compiled native code.
 *
 * jit: JIT context
 * buf: buffer containing native code (from jit_compile)
 * buf_len: number of bytes in buf
 *
 * Returns the return value of the executed code.
 */
u64 mycelius_jit_exec(mycelius_jit_t *jit,
                      const u8 *buf, u32 buf_len);

/* Free a compiled native code buffer. */
void mycelius_jit_free_buf(u8 *buf, u32 buf_len);

/* Bytecode instruction set for Mycelius JIT.
 *
 * The bytecode is a simple stack-based register machine:
 *   - All operations are on a virtual register file (16 registers)
   *   - Memory access via load/store with base+offset addressing
   *   - Control flow via relative jumps
   *
 * Bytecode format:
 *   Each instruction is variable-length: opcode byte followed by operands.
 */

/* Opcodes */
#define JIT_OP_NOP         0x00  /* No operation */
#define JIT_OP_HALT        0x01  /* Halt execution, return top of stack */

/* Arithmetic */
#define JIT_OP_ADD         0x10  /* dst = src1 + src2 */
#define JIT_OP_SUB         0x11  /* dst = src1 - src2 */
#define JIT_OP_MUL         0x12  /* dst = src1 * src2 */
#define JIT_OP_DIV         0x13  /* dst = src1 / src2 */
#define JIT_OP_MOD         0x14  /* dst = src1 % src2 */
#define JIT_OP_AND         0x15  /* dst = src1 & src2 */
#define JIT_OP_OR          0x16  /* dst = src1 | src2 */
#define JIT_OP_XOR         0x17  /* dst = src1 ^ src2 */
#define JIT_OP_SHL         0x18  /* dst = src1 << src2 */
#define JIT_OP_SHR         0x19  /* dst = src1 >> src2 */

/* Comparisons */
#define JIT_OP_CMP_EQ      0x20  /* dst = (src1 == src2) */
#define JIT_OP_CMP_NE      0x21  /* dst = (src1 != src2) */
#define JIT_OP_CMP_LT      0x22  /* dst = (src1 < src2) */
#define JIT_OP_CMP_LE      0x23  /* dst = (src1 <= src2) */
#define JIT_OP_CMP_GT      0x24  /* dst = (src1 > src2) */
#define JIT_OP_CMP_LE      0x25  /* dst = (src1 <= src2) */

/* Control flow */
#define JIT_OP_JMP         0x30  /* Jump to offset */
#define JIT_OP_JMP_EQ      0x31  /* Jump if src1 == src2 */
#define JIT_OP_JMP_NE      0x32  /* Jump if src1 != src2 */
#define JIT_OP_JMP_LT      0x33  /* Jump if src1 < src2 */
#define JIT_OP_JMP_LE      0x34  /* Jump if src1 <= src2 */
#define JIT_OP_JMP_GT      0x35  /* Jump if src1 > src2 */
#define JIT_OP_JMP_LE      0x36  /* Jump if src1 <= src2 */
#define JIT_OP_CALL        0x37  /* Call function at offset */
#define JIT_OP_RET         0x38  /* Return from function */

/* Memory */
#define JIT_OP_LD          0x40  /* dst = [src + offset] */
#define JIT_OP_ST          0x41  /* [dst + offset] = src */
#define JIT_OP_LD_IMM      0x41  /* dst = immediate value */
#define JIT_OP_ST_IMM      0x42  /* [dst + offset] = immediate */

/* Stack operations */
#define JIT_OP_PUSH        0x50  /* Push src onto stack */
#define JIT_OP_POP         0x51  /* Pop from stack into dst */

/* System calls */
#define JIT_OP_SYSCALL     0x60  /* System call with args in registers */