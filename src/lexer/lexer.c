/* Streaming lexer.
 *
 * Single-pass character scan over the in-RAM source buffer. The lexer is
 * stateful — each call to `lexer_next` consumes one token and updates the
 * `tok`/`tok_pos`/`tok_len`/`int_val`/`err` fields on the `Lexer` struct.
 *
 * Invariants:
 *   - The scanner never reads past `L->len`. `peek_ch` returns 0 at EOF;
 *     no token form contains a 0 byte, so this is unambiguous.
 *   - On error the lexer sets L->tok = TOK_ERROR and L->err to a static
 *     message. The caller is expected to stop and report.
 *   - Newlines emit TOK_NEWLINE so the parser can use them as statement
 *     separators; the parser is free to skip runs.
 *
 * Decimal integer literals, identifiers, keywords, string literals with
 * escapes/interpolation, all single- and two-char operators, line comments,
 * and block comments.
 */
#include "lexer.h"

#include <stdint.h>

/* From keywords.c */
extern tok_t keyword_lookup(const char *text, uint16_t len);

static unsigned char peek_ch(const Lexer *L) {
  if (L->pos >= L->len) return 0;
  return (unsigned char)L->src[L->pos];
}

static unsigned char peek_ch_at(const Lexer *L, uint16_t off) {
  uint16_t p;
  p = L->pos + off;
  if (p >= L->len) return 0;
  return (unsigned char)L->src[p];
}

static void advance(Lexer *L) {
  unsigned char c;
  if (L->pos >= L->len) return;
  c = (unsigned char)L->src[L->pos++];
  if (c == 0x0A || c == 0x0D) ++L->line;
}

static int is_digit(unsigned char c) {
  return c >= '0' && c <= '9';
}

static int is_ident_start(unsigned char c) {
  return (c >= 'a' && c <= 'z') ||
         (c >= 'A' && c <= 'Z') ||
         c == '_';
}

static int is_ident_cont(unsigned char c) {
  return is_ident_start(c) || is_digit(c);
}

/* Skip spaces, tabs, and comments. Newlines are *not* skipped — they form
 * their own token. */
static void skip_trivia(Lexer *L) {
  unsigned char c;
  for (;;) {
    if (L->pos >= L->len) return;
    c = (unsigned char)L->src[L->pos];
    if (c == ' ' || c == '\t') {
      ++L->pos;
      continue;
    }
    if (c == '/' && peek_ch_at(L, 1) == '/') {
      /* Line comment. Consume until newline or EOF; do NOT consume the
       * newline itself. */
      L->pos += 2;
      while (L->pos < L->len) {
        c = (unsigned char)L->src[L->pos];
        if (c == 0x0A || c == 0x0D) break;
        ++L->pos;
      }
      continue;
    }
    if (c == '/' && peek_ch_at(L, 1) == '*') {
      /* Block comment. No nesting (per docs/using/LANGUAGE.md). */
      L->pos += 2;
      while (L->pos < L->len) {
        c = (unsigned char)L->src[L->pos];
        if (c == '*' && peek_ch_at(L, 1) == '/') {
          L->pos += 2;
          break;
        }
        if (c == 0x0A || c == 0x0D) ++L->line;
        ++L->pos;
      }
      continue;
    }
    return;
  }
}

void lexer_init(Lexer *L, const char *src, uint16_t len) {
  L->src = src;
  L->pos = 0;
  L->len = len;
  L->line = 1;
  L->tok = TOK_EOF;
  L->tok_pos = 0;
  L->tok_len = 0;
  L->tok_line = 1;
  L->int_val = 0;
  L->err = (const char *)0;
}

static void set_error(Lexer *L, const char *msg) {
  L->tok = TOK_ERROR;
  L->err = msg;
}

/* Lex a decimal integer literal starting at the current position. Sets
 * tok_* and int_val. Returns 1 on success, 0 on error.
 *
 * Accepted range is **0..65535** — wider than Int's signed i16 range
 * (-32768..32767) so Apple II addresses can be typed naturally as
 * decimal once the `peek`/`poke` re-land: `49200` for the
 * speaker click ($C030), `49250` for paddle 0 ($C062). Values
 * 32768..65535 are stored as their two's-complement i16 bit pattern:
 * `49200` becomes the Int with `lo=$30 hi=$C0`, which displays as
 * `-16336` if `print`ed but reads back as `49200` (u16) under
 * the pointer-cast interpretation. This matches Applesoft's
 * POKE/PEEK convention, where the address is a full-range u16 even
 * though Integer variables are i16.
 *
 * Budget sweep (2026-05-23) dropped hex/binary/octal prefix
 * forms and underscore digit separators. Sub-slice 2026-05-24
 * widened the decimal range to u16 instead of reintroducing `0x`
 * literals (same UX win, zero cost vs ~150 B for hex). The peek/poke
 * builtins themselves shipped briefly under that sub-slice and were
 * unshipped 2026-05-27 (design doc 011); the u16 lexer range stays
 * because it's zero-cost and a later build will need it. */
static int lex_int(Lexer *L) {
  unsigned char c;
  int32_t acc;       /* 32-bit so we can detect overflow before clamping */

  acc = 0;
  for (;;) {
    c = peek_ch(L);
    if (!is_digit(c)) break;
    acc = (int32_t)(acc * 10 + (int32_t)(c - '0'));
    if (acc > 65535L) {   /* L: the u16 ceiling exceeds cc65's 16-bit int */
      set_error(L, "integer literal out of Int range");
      return 0;
    }
    advance(L);
  }

  L->tok = TOK_INT;
  L->int_val = (int16_t)acc;
  return 1;
}

static void lex_ident_or_kw(Lexer *L) {
  uint16_t start;
  tok_t kw;
  start = L->pos;
  while (L->pos < L->len && is_ident_cont((unsigned char)L->src[L->pos])) {
    ++L->pos;
  }
  L->tok_len = (uint16_t)(L->pos - start);
  kw = keyword_lookup(L->src + start, L->tok_len);
  if (kw != 0) {
    L->tok = kw;
  } else {
    L->tok = TOK_IDENT;
  }
  L->tok_pos = start;
}

/* String literals with escape sequences (`\\`, `\"`, `\n`, `\t`,
 * `\r`, `\0`) and interpolation (`\(expr)`). The lexer's job here is
 * purely to find the closing quote and validate escapes; the compiler
 * is responsible for processing the escape sequences and parsing the
 * interpolated expressions. The token's tok_pos/tok_len span the raw
 * bytes between the surrounding quotes, escapes included.
 *
 * Inside `\(...)`, the lexer tracks paren depth so that the closing
 * quote isn't mistakenly matched inside an expression like `\((1+2))`.
 * Nested string literals inside `\(...)` are not supported. */
static void lex_string(Lexer *L) {
  uint16_t start;
  unsigned char c;
  unsigned char e;
  uint16_t depth;

  advance(L); /* consume opening " */
  start = L->pos;
  for (;;) {
    if (L->pos >= L->len) {
      set_error(L, "unterminated string literal");
      return;
    }
    c = (unsigned char)L->src[L->pos];
    if (c == '"') {
      L->tok = TOK_STR;
      L->tok_pos = start;
      L->tok_len = (uint16_t)(L->pos - start);
      advance(L); /* consume closing " */
      return;
    }
    if (c == 0x0A || c == 0x0D) {
      set_error(L, "newline in string literal");
      return;
    }
    if (c == '\\') {
      advance(L);
      if (L->pos >= L->len) {
        set_error(L, "unterminated string literal");
        return;
      }
      e = (unsigned char)L->src[L->pos];
      if (e == '(') {
        /* Interpolation; scan forward to the matching ')'. */
        advance(L);
        depth = 1;
        while (depth > 0) {
          if (L->pos >= L->len) {
            set_error(L, "unterminated interpolation in string literal");
            return;
          }
          c = (unsigned char)L->src[L->pos];
          if (c == '(') ++depth;
          else if (c == ')') --depth;
          else if (c == '"') {
            set_error(L, "nested string literal not supported in Phase 3 interpolation");
            return;
          } else if (c == 0x0A || c == 0x0D) {
            set_error(L, "newline in string literal");
            return;
          }
          advance(L);
        }
        continue;
      }
      if (e != '\\' && e != '"' && e != 'n' && e != 't' &&
          e != 'r' && e != '0') {
        set_error(L, "unknown string escape sequence");
        return;
      }
      advance(L);   /* consume the escape character */
      continue;
    }
    advance(L);
  }
}

void lexer_next(Lexer *L) {
  unsigned char c;
  unsigned char c2;
  uint16_t start;

  /* Sticky EOF / error. */
  if (L->tok == TOK_ERROR) return;

  skip_trivia(L);

  if (L->pos >= L->len) {
    L->tok = TOK_EOF;
    L->tok_pos = L->pos;
    L->tok_len = 0;
    L->tok_line = L->line;
    return;
  }

  L->tok_line = L->line;
  start = L->pos;
  c = (unsigned char)L->src[L->pos];

  /* Newlines as separators. */
  if (c == 0x0A || c == 0x0D) {
    /* CRLF collapses to one TOK_NEWLINE. */
    advance(L);
    if (c == 0x0D && peek_ch(L) == 0x0A) advance(L);
    L->tok = TOK_NEWLINE;
    L->tok_pos = start;
    L->tok_len = (uint16_t)(L->pos - start);
    return;
  }

  if (is_digit(c)) {
    L->tok_pos = start;
    if (!lex_int(L)) return;
    L->tok_len = (uint16_t)(L->pos - start);
    return;
  }

  if (is_ident_start(c)) {
    lex_ident_or_kw(L);
    return;
  }

  if (c == '"') {
    lex_string(L);
    return;
  }

  /* Punctuation / operators. */
  advance(L);
  c2 = peek_ch(L);
  L->tok_pos = start;
  switch (c) {
    case '+':
      if (c2 == '=') { advance(L); L->tok = TOK_PLUS_EQ; }
      else            L->tok = TOK_PLUS;
      break;
    case '-':
      if (c2 == '=') { advance(L); L->tok = TOK_MINUS_EQ; }
      else if (c2 == '>') { advance(L); L->tok = TOK_ARROW; }
      else            L->tok = TOK_MINUS;
      break;
    case '*':
      if (c2 == '=') { advance(L); L->tok = TOK_STAR_EQ; }
      else            L->tok = TOK_STAR;
      break;
    case '/':
      if (c2 == '=') { advance(L); L->tok = TOK_SLASH_EQ; }
      else            L->tok = TOK_SLASH;
      break;
    case '%':
      L->tok = TOK_PERCENT;
      break;
    case '!':
      if (c2 == '=') { advance(L); L->tok = TOK_NEQ; }
      else            L->tok = TOK_BANG;
      break;
    case '=':
      if (c2 == '=') { advance(L); L->tok = TOK_EQ; }
      else            L->tok = TOK_ASSIGN;
      break;
    case '<':
      if (c2 == '=') { advance(L); L->tok = TOK_LE; }
      else            L->tok = TOK_LT;
      break;
    case '>':
      if (c2 == '=') { advance(L); L->tok = TOK_GE; }
      else            L->tok = TOK_GT;
      break;
    case '&':
      if (c2 == '&') { advance(L); L->tok = TOK_AND_AND; }
      else { set_error(L, "bare '&' is not an operator"); return; }
      break;
    case '|':
      if (c2 == '|') { advance(L); L->tok = TOK_OR_OR; }
      else { set_error(L, "bare '|' is not an operator"); return; }
      break;
    case '?':
      if (c2 == '?') { advance(L); L->tok = TOK_QQ; }
      else            L->tok = TOK_QUESTION;
      break;
    case '.':
      if (c2 == '.' && peek_ch_at(L, 1) == '<') {
        advance(L); advance(L); L->tok = TOK_DOT_DOT_LT;
      } else if (c2 == '.' && peek_ch_at(L, 1) == '.') {
        advance(L); advance(L); L->tok = TOK_DOT_DOT_DOT;
      } else {
        L->tok = TOK_DOT;
      }
      break;
    case '(': L->tok = TOK_LPAREN; break;
    case ')': L->tok = TOK_RPAREN; break;
    case '{': L->tok = TOK_LBRACE; break;
    case '}': L->tok = TOK_RBRACE; break;
    case '[': L->tok = TOK_LBRACKET; break;
    case ']': L->tok = TOK_RBRACKET; break;
    case ',': L->tok = TOK_COMMA; break;
    case ':': L->tok = TOK_COLON; break;
    case ';': L->tok = TOK_SEMI; break;
    default:
      set_error(L, "unexpected character");
      return;
  }
  L->tok_len = (uint16_t)(L->pos - start);
}
