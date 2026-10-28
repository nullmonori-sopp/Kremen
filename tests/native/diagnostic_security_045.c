#include "../../native/kremen.h"
#include <assert.h>

int main(void) {
    char source[] = "a\tb\xc3\xa9\233\033\342\200\256Z";
    Source input = {.path = "fixture.kr", .data = source, .len = sizeof source - 1};
    Token token = {.src = &input, .offset = sizeof source - 2, .len = 1, .line = 1, .col = (int)sizeof source - 1};
    DiagnosticSpan span;
    km_diagnostic_span(token, &span);
    assert(strcmp(span.text, "a   b\xc3\xa9???Z") == 0);
    assert(span.caret == 9 && span.width == 1);
    FILE *output = tmpfile();
    assert(output != NULL);
    km_diagnostic_text(output, "file-\xce\xbb-\033-\342\200\256-\377");
    assert(fflush(output) == 0 && fseek(output, 0, SEEK_SET) == 0);
    char actual[128] = {0};
    size_t length = fread(actual, 1, sizeof actual - 1, output);
    assert(length != 0 && !ferror(output));
    assert(strcmp(actual, "file-\xce\xbb-\\x1b-\\u202e-\\xff") == 0);
    assert(fclose(output) == 0);
    return 0;
}
