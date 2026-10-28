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
//Checking memory allocations in one place & exit if anythingfails.
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

