// Susshruth Chandhra Gnan Pothula 251ADB006
// https://github.com/Susshruth-Pothula/Calc-graded-lab
// Compile with: gcc -O2 -Wall -Wextra -std=c17 -o calc calc.c -lm
// Resources and Citations : 
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <math.h>
#include <errno.h>
#include <dirent.h>
#include <sys/stat.h>

// Used in output file and folder names
static const char *firstName = "susshruth";
static const char *lastName = "pothula";
static const char *studentId = "251ADB006";
// Checking memory allocations in one place & exit if anything fails, so callers never need to check for NULL.
static void *checkedRealloc(void *ptr, size_t size) {
    void *result = realloc(ptr, size);
    if (!result) {
        fprintf(stderr, "calc: out of memory\n");
        exit(1);
    }
    return result;
}

static char *copyText(const char *text, size_t count) {
    char *copy = checkedRealloc(NULL, count + 1);
    memcpy(copy, text, count);
    copy[count] = '\0';
    return copy;
}

// Buffer grows as needed so the Large input files can be read too.
// For large files the buffer doubles its capacity when it is full so reading or
// appending n bytes costs O(n) in total with no repeated copying. The whole
// input is read once into this block and the parser works on it directly.
// lines arent copied out.
typedef struct {
    char *data;
    size_t length;
    size_t capacity;
} Buffer;

// This is to make room for extra bytes plus a terminating '\0'.
static void bufferReserve(Buffer *buf, size_t extra) {
    if (buf->length + extra + 1 <= buf->capacity) {
        return;
    }
    size_t newCapacity = buf->capacity ? buf->capacity : 4096;
    while (buf->length + extra + 1 > newCapacity) {
        newCapacity *= 2;
    }
    buf->data = checkedRealloc(buf->data, newCapacity);
    buf->capacity = newCapacity;
}

static void bufferAppend(Buffer *buf, const char *text, size_t count) {
    bufferReserve(buf, count);
    memcpy(buf->data + buf->length, text, count);
    buf->length += count;
    buf->data[buf->length] = '\0';
}

// Reads straight intobuffer until end of file so the file size never
// needs to be known. Also it returns 0 on success & -1 on failure.
static int readFile(const char *path, Buffer *buf) {
    FILE *file = fopen(path, "rb");
    if (!file) {
        return -1;
    }
    for (;;) {
        bufferReserve(buf, 65536);
        size_t room = buf->capacity - buf->length - 1;
        size_t got = fread(buf->data + buf->length, 1, room, file);
        buf->length += got;
        if (got < room) {
            break;
        }
    }
    int failed = ferror(file);
    fclose(file);
    buf->data[buf->length] = '\0';
    return failed ? -1 : 0;
}
// Keep integers & floatingpoint values separate so integer division
// stays as it is unless either operand's a float.

typedef struct {
    int isFloat;
    long long whole; // valid when isFloat is 0
    double real;     // valid when isFloat is 1
} Value;

static Value makeInt(long long number) {
    Value v = { 0, number, 0.0 };
    return v;
}

static Value makeFloat(double number) {
    Value v = { 1, 0, number };
    return v;
}

static double asDouble(Value v) {
    return v.isFloat ? v.real : (double)v.whole;
}

// Use unsigned arithmetic for integer operations to avoid signed overflows undefined behaviour
static long long wrapAdd(long long a, long long b) {
    return (long long)((unsigned long long)a + (unsigned long long)b);
}

static long long wrapSubtract(long long a, long long b) {
    return (long long)((unsigned long long)a - (unsigned long long)b);
}

static long long wrapMultiply(long long a, long long b) {
    return (long long)((unsigned long long)a * (unsigned long long)b);
}

// Exponentiation by squaring : exponent 0 returns 1 including for 0 ** 0.
static long long integerPower(long long base, long long exponent) {
    long long result = 1;
    while (exponent > 0) {
        if (exponent & 1) {
            result = wrapMultiply(result, base);
        }
        base = wrapMultiply(base, base);
        exponent >>= 1;
    }
    return result;
}

// // Positions 'pos' is an index into the whole file & not into the current line so
// every \n & every comment character before it is already counted. The
// 1-based position the assignment asks for is simply index + 1. lineEnd is the index of
// the line's \n  so when the parser runs out of line, index + 1 is exactly the position we require.
typedef struct {
    Value value;
    size_t start;   
    size_t opIndex; // where the ** before it is
} ChainItem;

typedef struct {
    const char *text;
    size_t pos;      
    size_t lineEnd;  
    size_t errorPos; // 1-based position of the first error or 0 if none

    ChainItem *chain; //these are operands of ** chains.Refer parsePower
    size_t chainCount;
    size_t chainCapacity;
} Parser;

static void skipSpaces(Parser *p) {
    while (p->pos < p->lineEnd && (p->text[p->pos] == ' ' || p->text[p->pos] == '\t')) {
        p->pos++;
    }
}

// Only the first error counts, so it is never overwritten.
static void reportError(Parser *p, size_t index) {
    if (p->errorPos == 0) {
        p->errorPos = index + 1;
    }
}

static int hasFailed(const Parser *p) {
    return p->errorPos != 0;
}

static void chainPush(Parser *p, Value value, size_t start, size_t opIndex) {
    if (p->chainCount == p->chainCapacity) {
        p->chainCapacity = p->chainCapacity ? p->chainCapacity * 2 : 16;
        p->chain = checkedRealloc(p->chain, p->chainCapacity * sizeof *p->chain);
    }
    ChainItem item = { value, start, opIndex };
    p->chain[p->chainCount++] = item;
}

//These are the error positions for evaluation errors:
// division by zero: start of the right operand i.e the value that is not allowed
// inf or NaN result: the operator as the operands were valid & the
// operation produced the bad value
static Value applyOperator(Parser *p, char op, Value left, Value right, size_t opIndex, size_t rightStart) {
    if (!left.isFloat && !right.isFloat) {
        switch (op) {
        case '+': return makeInt(wrapAdd(left.whole, right.whole));
        case '-': return makeInt(wrapSubtract(left.whole, right.whole));
        case '*': return makeInt(wrapMultiply(left.whole, right.whole));
        default:
            if (right.whole == 0) {
                reportError(p, rightStart);
                return makeInt(0);
            }
            return makeInt(left.whole / right.whole); // C truncates toward zero as required
        }
    }

    double a = asDouble(left);
    double b = asDouble(right);
    double result;
    switch (op) {
    case '+': result = a + b; break;
    case '-': result = a - b; break;
    case '*': result = a * b; break;
    default:
        if (b == 0.0) {
            reportError(p, rightStart);
            return makeFloat(0.0);
        }
        result = a / b;
        break;
    }
    if (!isfinite(result)) {
        reportError(p, opIndex);
    }
    return makeFloat(result);
}

//A negative integer exponent is reported at the start of the exponent since
// that operand is the one that's not allowed. If either side is a float pow()
// is used & a NaN or infinite result is reported at the ** cause the
// operands were valid and the operation produced the bad value.
static Value applyPower(Parser *p, Value base, Value exponent, size_t opIndex, size_t exponentStart) {
    if (!base.isFloat && !exponent.isFloat) {
        if (exponent.whole < 0) {
            reportError(p, exponentStart);
            return makeInt(0);
        }
        return makeInt(integerPower(base.whole, exponent.whole));
    }
    double result = pow(asDouble(base), asDouble(exponent));
    if (!isfinite(result)) {
        reportError(p, opIndex);
    }
    return makeFloat(result);
}

// Recursive descent with 1 function for each precedence level lowest first:
//   expr := term { ('+' | '-') term }
//   term := power { ('*' | '/') power }
//   power := primary [ '**' power ]
//   primary := NUMBER | '(' expr ')'
// Precendence is right as each level asks the level below for its operands so
// by the time parseExpr sees a '+' parseTerm has already combined every '*' and
// '/' around it so 3 + 2 * 5 is 3 + (2 * 5). Parentheses go back to parseExpr
// which is why they override everything.
// Associativity is right cause parseExpr and parseTerm apply each operator as soon
// as its right operand is read so + - * / group left to right. parsePower
// combines its chain from the right so 2 ** 3 ** 2 is 2 ** (3 ** 2) = 512.
static Value parseExpr(Parser *p);

static int atPowerOperator(const Parser *p) {
    return p->pos + 1 < p->lineEnd && p->text[p->pos] == '*' && p->text[p->pos + 1] == '*';
}

// Numbers can be integers or decimals with digits required on both sides
// of the decimal point. Scientific notation isnt supported.
static Value parseNumber(Parser *p) {
    size_t start = p->pos;
    while (p->pos < p->lineEnd && isdigit((unsigned char)p->text[p->pos])) {
        p->pos++;
    }

    if (p->pos < p->lineEnd && p->text[p->pos] == '.') {
        if (p->pos + 1 >= p->lineEnd || !isdigit((unsigned char)p->text[p->pos + 1])) {
            reportError(p, p->pos);
            return makeInt(0);
        }
        p->pos++;
        while (p->pos < p->lineEnd && isdigit((unsigned char)p->text[p->pos])) {
            p->pos++;
        }
        
        char *token = copyText(p->text + start, p->pos - start);
        double number = strtod(token, NULL);
        free(token);
        return makeFloat(number);
    }

    unsigned long long number = 0;
    for (size_t i = start; i < p->pos; i++) {
        number = number * 10 + (unsigned long long)(p->text[i] - '0');
    }
    return makeInt((long long)number);
}

// An operand starts with a number or '('. Anything else is a syntax error and is repored
// right there : a second operator (7 + * 3), a unary minus (-5), an invalid character (2 + & 5)
// or the end of the line (3 * then \n), which is the \n's position. A missing ')' is
//found the same way as the parser expects ')' but reaches the end of the line.
static Value parsePrimary(Parser *p) {
    skipSpaces(p);
    if (p->pos < p->lineEnd && isdigit((unsigned char)p->text[p->pos])) {
        return parseNumber(p);
    }
    if (p->pos < p->lineEnd && p->text[p->pos] == '(') {
        p->pos++;
        Value inside = parseExpr(p);
        if (hasFailed(p)) {
            return inside;
        }
        skipSpaces(p);
        if (p->pos >= p->lineEnd || p->text[p->pos] != ')') {
            reportError(p, p->pos);
            return inside;
        }
        p->pos++;
        return inside;
    }
    reportError(p, p->pos);
    return makeInt(0);
}

// The grammar rule for ** is recursive but one recursive call per ** can
// overflow the stack on a very long line & lines have no length limit. So the
// chain is read into a stack & combined from right to left with a loop which
// gives the same right associative result and the same errors.
static Value parsePower(Parser *p) {
    Value first = parsePrimary(p);
    if (hasFailed(p)) {
        return first;
    }
    skipSpaces(p);
    if (!atPowerOperator(p)) {
        return first;
    }

    // Parentheses inside the chain reuse the stack but always pop what they
    // push so everything from bottom up belongs to this chain.
    size_t bottom = p->chainCount;
    chainPush(p, first, 0, 0);
    while (atPowerOperator(p)) {
        size_t opIndex = p->pos;
        p->pos += 2;
        skipSpaces(p);
        size_t start = p->pos;
        Value next = parsePrimary(p);
        if (hasFailed(p)) {
            p->chainCount = bottom;
            return next;
        }
        chainPush(p, next, start, opIndex);
        skipSpaces(p);
    }

    // From Right to left the exponent of step k starts where operand k + 1 starts.
    Value result = p->chain[p->chainCount - 1].value;
    for (size_t k = p->chainCount - 1; k > bottom && !hasFailed(p); k--) {
        result = applyPower(p, p->chain[k - 1].value, result, p->chain[k].opIndex, p->chain[k].start);
    }
    p->chainCount = bottom;
    return result;
}

// A '*' here is always a single '*'
static Value parseTerm(Parser *p) {
    Value value = parsePower(p);
    while (!hasFailed(p)) {
        skipSpaces(p);
        if (p->pos >= p->lineEnd || (p->text[p->pos] != '*' && p->text[p->pos] != '/')) {
            break;
        }
        char op = p->text[p->pos];
        size_t opIndex = p->pos++;
        skipSpaces(p);
        size_t rightStart = p->pos;
        Value right = parsePower(p);
        if (hasFailed(p)) {
            break;
        }
        value = applyOperator(p, op, value, right, opIndex, rightStart);
    }
    return value;
}

static Value parseExpr(Parser *p) {
    Value value = parseTerm(p);
    while (!hasFailed(p)) {
        skipSpaces(p);
        if (p->pos >= p->lineEnd || (p->text[p->pos] != '+' && p->text[p->pos] != '-')) {
            break;
        }
        char op = p->text[p->pos];
        size_t opIndex = p->pos++;
        skipSpaces(p);
        size_t rightStart = p->pos;
        Value right = parseTerm(p);
        if (hasFailed(p)) {
            break;
        }
        value = applyOperator(p, op, value, right, opIndex, rightStart);
    }
    return value;
}
// Evaluating a whole file
static void appendValue(Buffer *out, Value v) {
    char line[64];
    int count = v.isFloat ? snprintf(line, sizeof line, "%.15g\n", v.real)
                          : snprintf(line, sizeof line, "%lld\n", v.whole);
    bufferAppend(out, line, (size_t)count);
}

// Store results first so that any error replaces them with a single ERROR line.
static void evaluateText(const char *text, size_t length, Buffer *out) {
    Parser p = { text, 0, 0, 0, NULL, 0, 0 };

    while (p.pos < length) {
        // Find the end of the current line.
        const char *newline = memchr(text + p.pos, '\n', length - p.pos);
        p.lineEnd = newline ? (size_t)(newline - text) : length;

        skipSpaces(&p);
        // Blank lines & lines starting with # give no output. A # anywhere else
        // reaches the parser & is an invalid character.
        if (p.pos < p.lineEnd && text[p.pos] != '#') {
            Value value = parseExpr(&p);
            skipSpaces(&p);
            // Something left after a complete expression is an extra ')' or an
            // invalid character for example the last ) in (1 + 2)) or the 3 in 2 3.
            if (!hasFailed(&p) && p.pos < p.lineEnd) {
                reportError(&p, p.pos);
            }
            if (hasFailed(&p)) {
                break;
            }
            appendValue(out, value);
        }
        p.pos = p.lineEnd + 1;
    }

    if (hasFailed(&p)) {
        char line[48];
        int count = snprintf(line, sizeof line, "ERROR:%zu\n", p.errorPos);
        out->length = 0;
        bufferAppend(out, line, (size_t)count);
    }
    free(p.chain);
}

// Files and folders

static int endsWithTxt(const char *name) {
    size_t count = strlen(name);
    return count >= 4 && strcmp(name + count - 4, ".txt") == 0;
}

// Gets the last part of a path ignoring any trailing slashes.
// For example "labs/task1.txt" becomes "task1.txt". The caller basically frees it.
static char *lastPart(const char *path) {
    size_t end = strlen(path);
    while (end > 1 && path[end - 1] == '/') {
        end--;
    }
    size_t start = end;
    while (start > 0 && path[start - 1] != '/') {
        start--;
    }
    return copyText(path + start, end - start);
}

// "labs/task1.txt" becomes "task1"
static char *inputBase(const char *path) {
    char *name = lastPart(path);
    if (endsWithTxt(name)) {
        name[strlen(name) - 4] = '\0';
    }
    return name;
}

static char *joinPath(const char *folder, const char *name) {
    size_t size = strlen(folder) + strlen(name) + 2;
    char *path = checkedRealloc(NULL, size);
    snprintf(path, size, "%s/%s", folder, name);
    return path;
}

static int makeOutputDir(const char *path) {
    if (mkdir(path, 0755) != 0 && errno != EEXIST) {
        fprintf(stderr, "calc: cannot create folder %s: %s\n", path, strerror(errno));
        return 1;
    }
    struct stat info;
    if (stat(path, &info) != 0 || !S_ISDIR(info.st_mode)) {
        fprintf(stderr, "calc: %s is not a folder\n", path);
        return 1;
    }
    return 0;
}

// This writes the result to the required output filename.
// Evaluation errors are output normally while file errors return 1.
static int processFile(const char *inputPath, const char *outDir) {
    Buffer input = { 0 };
    if (readFile(inputPath, &input) != 0) {
        fprintf(stderr, "calc: cannot read %s: %s\n", inputPath, strerror(errno));
        free(input.data);
        return 1;
    }

    Buffer output = { 0 };
    evaluateText(input.data, input.length, &output);
    free(input.data);

    char *base = inputBase(inputPath);
    size_t nameSize = strlen(base) + strlen(firstName) + strlen(lastName) + strlen(studentId) + 8;
    char *fileName = checkedRealloc(NULL, nameSize);
    snprintf(fileName, nameSize, "%s_%s_%s_%s.txt", base, firstName, lastName, studentId);
    char *outPath = joinPath(outDir, fileName);

    int status = 0;
    FILE *file = fopen(outPath, "wb");
    if (!file) {
        fprintf(stderr, "calc: cannot write %s: %s\n", outPath, strerror(errno));
        status = 1;
    } else {
        size_t written = output.length ? fwrite(output.data, 1, output.length, file) : 0;
        if (fclose(file) != 0 || written != output.length) {
            fprintf(stderr, "calc: cannot write %s\n", outPath);
            status = 1;
        }
    }

    free(outPath);
    free(fileName);
    free(base);
    free(output.data);
    return status;
}

static int compareNames(const void *a, const void *b) {
    return strcmp(*(char *const *)a, *(char *const *)b);
}

// Every *.txt file directly inside the folder & subfolders are not entered. Names
// are collected before processing so result files written into the same folder
// cannot appear in the listing while it is being read.
static int processFolder(const char *folder, const char *outDir) {
    DIR *dir = opendir(folder);
    if (!dir) {
        fprintf(stderr, "calc: cannot open folder %s: %s\n", folder, strerror(errno));
        return 1;
    }

    char **names = NULL;
    size_t count = 0;
    size_t capacity = 0;
    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        if (!endsWithTxt(entry->d_name)) {
            continue;
        }
        char *path = joinPath(folder, entry->d_name);
        struct stat info;
        // This Skips anything that is not a regular file
        if (stat(path, &info) == 0 && S_ISREG(info.st_mode)) {
            if (count == capacity) {
                capacity = capacity ? capacity * 2 : 16;
                names = checkedRealloc(names, capacity * sizeof *names);
            }
            names[count++] = path;
        } else {
            free(path);
        }
    }
    closedir(dir);

    if (count > 1) {
        qsort(names, count, sizeof *names, compareNames);
    }
    int status = 0;
    for (size_t i = 0; i < count; i++) {
        if (processFile(names[i], outDir) != 0) {
            status = 1;
        }
        free(names[i]);
    }
    free(names);
    return status;
}

static int usageError(const char *message, const char *detail) {
    fprintf(stderr, "calc: %s%s\n", message, detail);
    fprintf(stderr, "usage: calc [-d DIR | --dir DIR] [-o OUTDIR | --output-dir OUTDIR] [input.txt]\n");
    return 1;
}

int main(int argc, char **argv) {
    const char *folder = NULL;
    const char *outDir = NULL;
    const char *inputPath = NULL;

    for (int i = 1; i < argc; i++) {
        const char *arg = argv[i];
        int isDir = strcmp(arg, "-d") == 0 || strcmp(arg, "--dir") == 0;
        int isOut = strcmp(arg, "-o") == 0 || strcmp(arg, "--output-dir") == 0;
        if (isDir || isOut) {
            if (i + 1 >= argc) {
                return usageError("missing folder name after ", arg);
            }
            if (isDir) {
                folder = argv[++i];
            } else {
                outDir = argv[++i];
            }
        } else if (arg[0] == '-' && arg[1] != '\0') {
            return usageError("unknown option ", arg);
        } else if (inputPath) {
            return usageError("only one input file can be given", "");
        } else {
            inputPath = arg;
        }
    }

    if (!folder && !inputPath) {
        return usageError("no input file or folder given", "");
    }
    if (folder && inputPath) {
        return usageError("give either -d or an input file, not both", "");
    }

    //Checking the input before creating the output directory.
    const char *source = folder ? folder : inputPath;
    struct stat info;
    if (stat(source, &info) != 0) {
        fprintf(stderr, "calc: cannot open %s: %s\n", source, strerror(errno));
        return 1;
    }
    if (folder ? !S_ISDIR(info.st_mode) : !S_ISREG(info.st_mode)) {
        fprintf(stderr, "calc: %s is not a %s\n", source, folder ? "folder" : "file");
        return 1;
    }

    // Use a default output folder based on the input filename or folder name.
    // For files, remove the .txt extension first.
    char *defaultDir = NULL;
    if (!outDir) {
        char *base = folder ? lastPart(folder) : inputBase(inputPath);
        size_t size = strlen(base) + strlen(firstName) + strlen(studentId) + 3;
        defaultDir = checkedRealloc(NULL, size);
        snprintf(defaultDir, size, "%s_%s_%s", base, firstName, studentId);
        free(base);
        outDir = defaultDir;
    }

    int status = makeOutputDir(outDir);
    if (status == 0) {
        status = folder ? processFolder(folder, outDir) : processFile(inputPath, outDir);
    }
    free(defaultDir);
    return status;
}

