/* Small dependency-free JSON syntax validator for compiler diagnostic tests. */
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned char input[262145];
static size_t at, length;
static void spaces(void) { while (at < length && input[at] != 0 && strchr(" \t\r\n", input[at])) at++; }
static bool take(unsigned char ch) { spaces(); if (at < length && input[at] == ch) { at++; return true; } return false; }
static bool hex4(unsigned *code) {
    *code = 0;
    for (unsigned i = 0; i < 4; i++) {
        if (at == length) return false;
        unsigned char c = input[at++];
        unsigned digit = c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : 16;
        if (digit == 16) return false;
        *code = *code * 16 + digit;
    }
    return true;
}
static bool string(void) {
    if (!take('"')) return false;
    while (at < length) {
        unsigned char ch = input[at++];
        if (ch == '"') return true;
        if (ch < 32) return false;
        if (ch == '\\') {
            if (at == length) return false;
            ch = input[at++];
            if (ch == 'u') {
                unsigned code;
                if (!hex4(&code) || (code >= 0xdc00 && code <= 0xdfff)) return false;
                if (code >= 0xd800 && code <= 0xdbff) {
                    if (at + 2 > length || input[at++] != '\\' || input[at++] != 'u' || !hex4(&code) || code < 0xdc00 || code > 0xdfff) return false;
                }
            } else if (!ch || !strchr("\"\\/bfnrt", ch)) return false;
        }
    }
    return false;
}
static bool value(unsigned depth) {
    spaces();
    if (depth > 64 || at == length) return false;
    if (input[at] == '"') return string();
    if (take('{')) {
        if (take('}')) return true;
        do { if (!string() || !take(':') || !value(depth + 1)) return false; } while (take(','));
        return take('}');
    }
    if (take('[')) {
        if (take(']')) return true;
        do { if (!value(depth + 1)) return false; } while (take(','));
        return take(']');
    }
    const char *words[] = {"true", "false", "null"};
    for (size_t i = 0; i < 3; i++) {
        size_t n = strlen(words[i]);
        if (length - at >= n && !memcmp(input + at, words[i], n)) { at += n; return true; }
    }
    if (input[at] == '-') at++;
    if (at == length) return false;
    if (input[at] == '0') at++;
    else {
        if (input[at] < '1' || input[at] > '9') return false;
        while (at < length && input[at] >= '0' && input[at] <= '9') at++;
    }
    if (at < length && input[at] == '.') {
        at++; size_t start = at;
        while (at < length && input[at] >= '0' && input[at] <= '9') at++;
        if (at == start) return false;
    }
    if (at < length && (input[at] == 'e' || input[at] == 'E')) {
        at++; if (at < length && (input[at] == '+' || input[at] == '-')) at++;
        size_t start = at;
        while (at < length && input[at] >= '0' && input[at] <= '9') at++;
        if (at == start) return false;
    }
    return true;
}
int main(int argc, char **argv) {
    if (argc != 2) return 2;
    FILE *file = fopen(argv[1], "rb");
    if (!file) return 2;
    length = fread(input, 1, sizeof input, file);
    bool failed = ferror(file) || length == sizeof input;
    fclose(file);
    if (failed || !value(0)) return 1;
    spaces();
    return at == length ? 0 : 1;
}
