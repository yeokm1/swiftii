/* Token type enum.
 *
 * One byte per token type to keep the lexer state small. Codes are grouped
 * by category so the compiler can do range-tests cheaply (e.g. "is this
 * a binary operator?").
 *
 * The numeric values are not exposed outside the compiler — they may be
 * renumbered freely as long as no external file pins them.
 */
#ifndef SWIFTII_TOKENS_H
#define SWIFTII_TOKENS_H

typedef unsigned char tok_t;

/* Control */
#define TOK_EOF      0
#define TOK_ERROR    1
#define TOK_NEWLINE  2

/* Literals */
#define TOK_INT      0x10
#define TOK_STR      0x11
#define TOK_IDENT    0x12

/* Keywords */
#define TOK_LET      0x20
#define TOK_VAR      0x21
#define TOK_FUNC     0x22
#define TOK_RETURN   0x23
#define TOK_IF       0x24
#define TOK_ELSE     0x25
#define TOK_WHILE    0x26
#define TOK_FOR      0x27
#define TOK_IN       0x28
#define TOK_TRUE     0x29
#define TOK_FALSE    0x2A
#define TOK_NIL      0x2B
/* 0x2C..0x33 reclaimed: TOK_STRUCT / TOK_GUARD dropped 2026-06-14, and
 * TOK_CLASS / TOK_PROTOCOL / TOK_ENUM / TOK_THROWS / TOK_TRY / TOK_CATCH
 * dropped 2026-05-23 (budget sweep). All were reserved-but-unused
 * keywords (struct/guard are "Maybe / probably never" items 25/24) that only
 * occupied keyword-table entries. If a future tier adds them back, pick fresh
 * token codes — old IDs are free. */
#define TOK_BREAK    0x34

/* Single-char operators / punctuation */
#define TOK_PLUS     0x40  /* +  */
#define TOK_MINUS    0x41  /* -  */
#define TOK_STAR     0x42  /* *  */
#define TOK_SLASH    0x43  /* /  */
#define TOK_PERCENT  0x44  /* %  */
#define TOK_BANG     0x45  /* !  */
#define TOK_ASSIGN   0x46  /* =  */
#define TOK_LT       0x47  /* <  */
#define TOK_GT       0x48  /* >  */
#define TOK_LPAREN   0x49  /* (  */
#define TOK_RPAREN   0x4A  /* )  */
#define TOK_LBRACE   0x4B  /* {  */
#define TOK_RBRACE   0x4C  /* }  */
#define TOK_LBRACKET 0x4D  /* [  */
#define TOK_RBRACKET 0x4E  /* ]  */
#define TOK_COMMA    0x4F  /* ,  */
#define TOK_DOT      0x50  /* .  */
#define TOK_COLON    0x51  /* :  */
#define TOK_SEMI     0x52  /* ;  */
#define TOK_QUESTION 0x53  /* ?  */

/* Multi-char operators */
#define TOK_EQ          0x60  /* == */
#define TOK_NEQ         0x61  /* != */
#define TOK_LE          0x62  /* <= */
#define TOK_GE           0x63 /* >= */
#define TOK_AND_AND     0x64  /* && */
#define TOK_OR_OR       0x65  /* || */
#define TOK_QQ          0x66  /* ?? */
#define TOK_ARROW       0x67  /* -> */
#define TOK_PLUS_EQ     0x68  /* += */
#define TOK_MINUS_EQ    0x69  /* -= */
#define TOK_STAR_EQ     0x6A  /* *= */
#define TOK_SLASH_EQ    0x6B  /* /= */
#define TOK_DOT_DOT_LT  0x6C  /* ..< */
#define TOK_DOT_DOT_DOT 0x6D  /* ... */

#endif /* SWIFTII_TOKENS_H */
