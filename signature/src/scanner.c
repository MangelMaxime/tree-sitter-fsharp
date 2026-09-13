#define TS_SHARED_SCANNER
#include "../../src/scanner.c"

void *tree_sitter_fsharp_signature_external_scanner_create(void) { return scanner_create(); }
void tree_sitter_fsharp_signature_external_scanner_destroy(void *p) { scanner_destroy(p); }
unsigned tree_sitter_fsharp_signature_external_scanner_serialize(void *p, char *buf) { return scanner_serialize(p, buf); }
void tree_sitter_fsharp_signature_external_scanner_deserialize(void *p, const char *buf, unsigned len) { scanner_deserialize(p, buf, len); }
bool tree_sitter_fsharp_signature_external_scanner_scan(void *p, TSLexer *lexer, const bool *valid) { return scanner_scan(p, lexer, valid); }
