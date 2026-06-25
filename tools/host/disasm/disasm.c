/* Bytecode disassembler.
 *
 * Reads a raw bytecode blob and prints it as a numbered listing. Host-
 * only tool — printf is allowed here (this binary is never cross-
 * compiled with cc65). Useful for inspecting `compile_source()` output
 * from unit tests or for verifying that the compiler emits what
 * docs/contributing/OPCODES.md says it should.
 *
 * Usage:
 *   disasm <bytecode-file>
 *
 * Reads at most 8 KB (one program's worth). Larger inputs are truncated
 * with a notice; that matches the FILE_BC_SIZE budget on the Apple II.
 */
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>

#include "vm/opcodes.h"

static const char *opcode_name(unsigned char op) {
  switch (op) {
    case OP_NIL:           return "NIL";
    case OP_TRUE:          return "TRUE";
    case OP_FALSE:         return "FALSE";
    case OP_INT_U8:        return "INT_U8";
    case OP_INT_I16:       return "INT_I16";
    case OP_CONST:         return "CONST";
    case OP_STR:           return "STR";
    case OP_OPT_SOME:      return "OPT_SOME";
    case OP_OPT_NONE:      return "OPT_NONE";
    case OP_POP:           return "POP";
    case OP_DUP:           return "DUP";
    case OP_SWAP:          return "SWAP";
    case OP_OVER:          return "OVER";
    case OP_POP_N:         return "POP_N";
    case OP_GET_GLOBAL:    return "GET_GLOBAL";
    case OP_SET_GLOBAL:    return "SET_GLOBAL";
    case OP_DEFINE_GLOBAL: return "DEFINE_GLOBAL";
    case OP_GET_LOCAL:     return "GET_LOCAL";
    case OP_SET_LOCAL:     return "SET_LOCAL";
    case OP_ADD:           return "ADD";
    case OP_SUB:           return "SUB";
    case OP_MUL:           return "MUL";
    case OP_DIV:           return "DIV";
    case OP_MOD:           return "MOD";
    case OP_NEG:           return "NEG";
    case OP_INC:           return "INC";
    case OP_DEC:           return "DEC";
    case OP_EQ:            return "EQ";
    case OP_NEQ:           return "NEQ";
    case OP_LT:            return "LT";
    case OP_LE:            return "LE";
    case OP_GT:            return "GT";
    case OP_GE:            return "GE";
    case OP_NOT:           return "NOT";
    case OP_STR_EQ:        return "STR_EQ";
    case OP_JUMP:          return "JUMP";
    case OP_JUMP_IF_FALSE: return "JUMP_IF_FALSE";
    case OP_JUMP_IF_TRUE:  return "JUMP_IF_TRUE";
    case OP_LOOP:          return "LOOP";
    case OP_HALT:          return "HALT";
    case OP_CALL:          return "CALL";
    case OP_RETURN:        return "RETURN";
    case OP_RETURN_V:      return "RETURN_V";
    case OP_CALL_BUILTIN:  return "CALL_BUILTIN";
    case OP_TRAP:          return "TRAP";
    default:               return "???";
  }
}

/* Number of operand bytes that follow this opcode. */
static int operand_size(unsigned char op) {
  switch (op) {
    case OP_INT_U8:
    case OP_CONST:
    case OP_POP_N:
    case OP_GET_GLOBAL:
    case OP_SET_GLOBAL:
    case OP_DEFINE_GLOBAL:
    case OP_GET_LOCAL:
    case OP_SET_LOCAL:
    case OP_NEW_ARRAY:
      return 1;
    case OP_INT_I16:
    case OP_STR:
    case OP_JUMP:
    case OP_JUMP_IF_FALSE:
    case OP_JUMP_IF_TRUE:
    case OP_LOOP:
    case OP_NIL_COALESCE:
    case OP_IF_LET:
      return 2;
    case OP_CALL:
    case OP_CALL_BUILTIN:
      return 2;
    default:
      return 0;
  }
}

int main(int argc, char **argv) {
  FILE *fp;
  unsigned char buf[8192];
  size_t n;
  size_t pc;

  if (argc < 2) {
    fprintf(stderr, "usage: disasm <bytecode-file>\n");
    return 1;
  }
  fp = fopen(argv[1], "rb");
  if (!fp) {
    perror(argv[1]);
    return 1;
  }
  n = fread(buf, 1, sizeof(buf), fp);
  if (!feof(fp)) {
    fprintf(stderr, "disasm: input truncated to %zu bytes\n", n);
  }
  fclose(fp);

  /* If this is a `.swb` image (12-byte header, magic 'S','W','B'), decode
   * only the bytecode section so the header bytes aren't mis-read as opcodes.
   * Anything else (e.g. a raw bytecode dump from a unit test) is disassembled
   * from byte 0 as before. Header layout: see src/swb/swb.h. */
  size_t start = 0, end = n;
  if (n >= 12 && buf[0] == 'S' && buf[1] == 'W' && buf[2] == 'B') {
    uint16_t version       = buf[3];
    uint16_t program_start = (uint16_t)buf[4] | ((uint16_t)buf[5] << 8);
    uint16_t bc_len        = (uint16_t)buf[6] | ((uint16_t)buf[7] << 8);
    start = 12;
    end   = (12 + (size_t)bc_len <= n) ? 12 + (size_t)bc_len : n;
    printf("# .swb v%u  program_start=%u  bytecode=%u B\n",
           version, program_start, bc_len);
  }

  pc = start;
  while (pc < end) {
    unsigned char op = buf[pc];
    int operands = operand_size(op);
    printf("%04zx  %02x  %-14s", pc, op, opcode_name(op));
    if (op == OP_CALL_BUILTIN && pc + 2 < end) {
      printf(" id=%u argc=%u", buf[pc + 1], buf[pc + 2]);
    } else if (operands == 1 && pc + 1 < end) {
      printf(" %u", buf[pc + 1]);
    } else if (operands == 2 && pc + 2 < end) {
      uint16_t v = (uint16_t)buf[pc + 1] | ((uint16_t)buf[pc + 2] << 8);
      printf(" %u (0x%04x)", v, v);
    }
    printf("\n");
    pc += (size_t)(1 + operands);
  }
  return 0;
}
