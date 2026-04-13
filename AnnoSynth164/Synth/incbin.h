/*
 * incbin.h - Embed one or more binary files as read-only data.
 *
 * Requires GCC or Clang (uses __asm__ with .incbin directive).
 *
 * Usage:
 *
 *   // Embed multiple files (up to 8) - in exactly ONE translation unit:
 *   INCBIN(icon, "play.png", "pause.png", "stop.png");
 *
 *   // Indexed access:
 *   icon[0].data                 // play.png bytes
 *   INCBIN_SIZE_AT(icon, 0)      // play.png byte count
 *   incbin_size(&icon[0])        // same, but safe with side effects
 *   icon[1].data                 // pause.png bytes
 *   INCBIN_COUNT(icon)           // 3  (compile-time constant)
 *
 *   // Single file works identically:
 *   INCBIN(shader, "frag.glsl");
 *   INCBIN_PTR(shader)           // shader[0].data
 *   INCBIN_SIZE(shader)          // byte count of first entry
 *
 *   // In OTHER translation units, declare with count:
 *   INCBIN_EXTERN(icon, 3);
 *   // Then use icon[i].data / INCBIN_SIZE_AT(icon, i) as above.
 *
 * Each embedded file receives a trailing NUL byte (not counted in size)
 * for convenience when the payload is text.
 *
 * INCBIN_ALIGNMENT (default 16) may be overridden before including
 * this header to control per-entry start alignment.
 *
 * File paths are resolved by the assembler - typically relative to the
 * source file's directory or -Wa,-I<dir> include paths.
 * Paths must not contain double-quote or backslash characters.
 *
 * Limitations:
 *   - INCBIN(name, ...) must appear at file scope, exactly once per name.
 *   - Do not use both INCBIN(name,...) and INCBIN_EXTERN(name,N) in the
 *     same translation unit.
 *   - Maximum 8 files per INCBIN() call.
 */

#ifndef INCBIN_H_
#define INCBIN_H_

#include <stdint.h>
#include <stddef.h>

/* ------------------------------------------------------------------ */
/*  Compiler gate                                                      */
/* ------------------------------------------------------------------ */

#if !defined(__GNUC__) && !defined(__clang__)
  #error "incbin.h requires GCC or Clang (__asm__ and .incbin directive)."
#endif

/* ------------------------------------------------------------------ */
/*  Public type                                                        */
/* ------------------------------------------------------------------ */

/*
 * Stores data and end pointers.  Both are valid address constants
 * for static initializers (ISO C).  Size is computed on access via
 * INCBIN_SIZE / INCBIN_SIZE_AT.
 */
typedef struct incbin_entry {
    const uint8_t *data;
    const uint8_t *end;
} incbin_entry;

/* ------------------------------------------------------------------ */
/*  Internal helpers                                                   */
/* ------------------------------------------------------------------ */

#define INCBIN_STR_(x)    #x
#define INCBIN_STR(x)     INCBIN_STR_(x)
#define INCBIN_CAT_(a, b) a ## b
#define INCBIN_CAT(a, b)  INCBIN_CAT_(a, b)

/* Symbol prefix - compiler-provided, correct for every target ABI. */
#ifdef __USER_LABEL_PREFIX__
  #define INCBIN_PREFIX_ INCBIN_STR(__USER_LABEL_PREFIX__)
#else
  #define INCBIN_PREFIX_ ""
#endif

/* Section directive */
#if defined(__APPLE__)
  #define INCBIN_SECTION_ ".section __TEXT,__const\n"
#elif defined(_WIN32)
  #define INCBIN_SECTION_ ".section .rdata,\"dr\"\n"
#else
  #define INCBIN_SECTION_ ".section .rodata\n"
#endif

/* Configurable alignment (bytes, must be a power of two and > 0). */
#ifndef INCBIN_ALIGNMENT
  #define INCBIN_ALIGNMENT 16
#endif

/* Compile-time validation of INCBIN_ALIGNMENT.
   A negative-sized array triggers a clear error on C99+. */
typedef char incbin_alignment_check_[
    (INCBIN_ALIGNMENT > 0 &&
     (INCBIN_ALIGNMENT & (INCBIN_ALIGNMENT - 1)) == 0) ? 1 : -1
];

/* C++ linkage - asm symbols are unmangled. */
#ifdef __cplusplus
  #define INCBIN_EXTERN_C_ extern "C"
#else
  #define INCBIN_EXTERN_C_ extern
#endif

/* ELF metadata per slot. */
#if defined(__ELF__)
  #define INCBIN_ELF_TYPE_(name, n) \
    ".type " INCBIN_PREFIX_ #name "_" #n "_start, @object\n"
  #define INCBIN_ELF_SIZE_(name, n) \
    ".size " INCBIN_PREFIX_ #name "_" #n "_start, " \
             INCBIN_PREFIX_ #name "_" #n "_end - " \
             INCBIN_PREFIX_ #name "_" #n "_start\n"
#else
  #define INCBIN_ELF_TYPE_(name, n)
  #define INCBIN_ELF_SIZE_(name, n)
#endif

/* Assembly block for one file slot */
#define INCBIN_ASM_(name, n, file) \
    ".balign " INCBIN_STR(INCBIN_ALIGNMENT) "\n" \
    INCBIN_ELF_TYPE_(name, n) \
    ".global " INCBIN_PREFIX_ #name "_" #n "_start\n" \
    INCBIN_PREFIX_ #name "_" #n "_start:\n" \
    ".incbin \"" file "\"\n" \
    ".global " INCBIN_PREFIX_ #name "_" #n "_end\n" \
    INCBIN_PREFIX_ #name "_" #n "_end:\n" \
    ".byte 0\n" \
    INCBIN_ELF_SIZE_(name, n)

/* C extern declaration for one slot */
#define INCBIN_DECL_(name, n) \
    INCBIN_EXTERN_C_ const uint8_t name##_##n##_start[]; \
    INCBIN_EXTERN_C_ const uint8_t name##_##n##_end[]

/* Table initializer for one slot (address constants only - ISO C) */
#define INCBIN_TBL_(name, n) \
    { name##_##n##_start, name##_##n##_end }

/* Variadic argument counting (1-16, supports overflow detection) */
#define INCBIN_NARGS_(                                                   \
    _1,_2,_3,_4,_5,_6,_7,_8,_9,_10,_11,_12,_13,_14,_15,_16, N, ...) N
#define INCBIN_NARGS(...) \
    INCBIN_NARGS_(__VA_ARGS__,16,15,14,13,12,11,10,9,8,7,6,5,4,3,2,1,0)

/* Clear error for >8 files (negative-sized array = compile error). */
#define INCBIN_LIMIT_ERROR_                                              \
    typedef char incbin_too_many_files_max_is_8_[-1]
#define INCBIN_9(name, ...)  INCBIN_LIMIT_ERROR_
#define INCBIN_10(name, ...) INCBIN_LIMIT_ERROR_
#define INCBIN_11(name, ...) INCBIN_LIMIT_ERROR_
#define INCBIN_12(name, ...) INCBIN_LIMIT_ERROR_
#define INCBIN_13(name, ...) INCBIN_LIMIT_ERROR_
#define INCBIN_14(name, ...) INCBIN_LIMIT_ERROR_
#define INCBIN_15(name, ...) INCBIN_LIMIT_ERROR_
#define INCBIN_16(name, ...) INCBIN_LIMIT_ERROR_

/* ------------------------------------------------------------------ */
/*  INCBIN_N - arity-specific definitions (1-8 files)                  */
/* ------------------------------------------------------------------ */

#define INCBIN_1(name, f0) \
    __asm__( \
        INCBIN_SECTION_ \
        INCBIN_ASM_(name, 0, f0) \
        ".previous\n" \
    ); \
    INCBIN_DECL_(name, 0); \
    static const incbin_entry __attribute__((unused)) name[] = { \
        INCBIN_TBL_(name, 0) \
    }; \
    enum { name##_count = 1 }

#define INCBIN_2(name, f0, f1) \
    __asm__( \
        INCBIN_SECTION_ \
        INCBIN_ASM_(name, 0, f0) \
        INCBIN_ASM_(name, 1, f1) \
        ".previous\n" \
    ); \
    INCBIN_DECL_(name, 0); \
    INCBIN_DECL_(name, 1); \
    static const incbin_entry __attribute__((unused)) name[] = { \
        INCBIN_TBL_(name, 0), \
        INCBIN_TBL_(name, 1) \
    }; \
    enum { name##_count = 2 }

#define INCBIN_3(name, f0, f1, f2) \
    __asm__( \
        INCBIN_SECTION_ \
        INCBIN_ASM_(name, 0, f0) \
        INCBIN_ASM_(name, 1, f1) \
        INCBIN_ASM_(name, 2, f2) \
        ".previous\n" \
    ); \
    INCBIN_DECL_(name, 0); \
    INCBIN_DECL_(name, 1); \
    INCBIN_DECL_(name, 2); \
    static const incbin_entry __attribute__((unused)) name[] = { \
        INCBIN_TBL_(name, 0), \
        INCBIN_TBL_(name, 1), \
        INCBIN_TBL_(name, 2) \
    }; \
    enum { name##_count = 3 }

#define INCBIN_4(name, f0, f1, f2, f3) \
    __asm__( \
        INCBIN_SECTION_ \
        INCBIN_ASM_(name, 0, f0) \
        INCBIN_ASM_(name, 1, f1) \
        INCBIN_ASM_(name, 2, f2) \
        INCBIN_ASM_(name, 3, f3) \
        ".previous\n" \
    ); \
    INCBIN_DECL_(name, 0); \
    INCBIN_DECL_(name, 1); \
    INCBIN_DECL_(name, 2); \
    INCBIN_DECL_(name, 3); \
    static const incbin_entry __attribute__((unused)) name[] = { \
        INCBIN_TBL_(name, 0), \
        INCBIN_TBL_(name, 1), \
        INCBIN_TBL_(name, 2), \
        INCBIN_TBL_(name, 3) \
    }; \
    enum { name##_count = 4 }

#define INCBIN_5(name, f0, f1, f2, f3, f4) \
    __asm__( \
        INCBIN_SECTION_ \
        INCBIN_ASM_(name, 0, f0) \
        INCBIN_ASM_(name, 1, f1) \
        INCBIN_ASM_(name, 2, f2) \
        INCBIN_ASM_(name, 3, f3) \
        INCBIN_ASM_(name, 4, f4) \
        ".previous\n" \
    ); \
    INCBIN_DECL_(name, 0); \
    INCBIN_DECL_(name, 1); \
    INCBIN_DECL_(name, 2); \
    INCBIN_DECL_(name, 3); \
    INCBIN_DECL_(name, 4); \
    static const incbin_entry __attribute__((unused)) name[] = { \
        INCBIN_TBL_(name, 0), \
        INCBIN_TBL_(name, 1), \
        INCBIN_TBL_(name, 2), \
        INCBIN_TBL_(name, 3), \
        INCBIN_TBL_(name, 4) \
    }; \
    enum { name##_count = 5 }

#define INCBIN_6(name, f0, f1, f2, f3, f4, f5) \
    __asm__( \
        INCBIN_SECTION_ \
        INCBIN_ASM_(name, 0, f0) \
        INCBIN_ASM_(name, 1, f1) \
        INCBIN_ASM_(name, 2, f2) \
        INCBIN_ASM_(name, 3, f3) \
        INCBIN_ASM_(name, 4, f4) \
        INCBIN_ASM_(name, 5, f5) \
        ".previous\n" \
    ); \
    INCBIN_DECL_(name, 0); \
    INCBIN_DECL_(name, 1); \
    INCBIN_DECL_(name, 2); \
    INCBIN_DECL_(name, 3); \
    INCBIN_DECL_(name, 4); \
    INCBIN_DECL_(name, 5); \
    static const incbin_entry __attribute__((unused)) name[] = { \
        INCBIN_TBL_(name, 0), \
        INCBIN_TBL_(name, 1), \
        INCBIN_TBL_(name, 2), \
        INCBIN_TBL_(name, 3), \
        INCBIN_TBL_(name, 4), \
        INCBIN_TBL_(name, 5) \
    }; \
    enum { name##_count = 6 }

#define INCBIN_7(name, f0, f1, f2, f3, f4, f5, f6) \
    __asm__( \
        INCBIN_SECTION_ \
        INCBIN_ASM_(name, 0, f0) \
        INCBIN_ASM_(name, 1, f1) \
        INCBIN_ASM_(name, 2, f2) \
        INCBIN_ASM_(name, 3, f3) \
        INCBIN_ASM_(name, 4, f4) \
        INCBIN_ASM_(name, 5, f5) \
        INCBIN_ASM_(name, 6, f6) \
        ".previous\n" \
    ); \
    INCBIN_DECL_(name, 0); \
    INCBIN_DECL_(name, 1); \
    INCBIN_DECL_(name, 2); \
    INCBIN_DECL_(name, 3); \
    INCBIN_DECL_(name, 4); \
    INCBIN_DECL_(name, 5); \
    INCBIN_DECL_(name, 6); \
    static const incbin_entry __attribute__((unused)) name[] = { \
        INCBIN_TBL_(name, 0), \
        INCBIN_TBL_(name, 1), \
        INCBIN_TBL_(name, 2), \
        INCBIN_TBL_(name, 3), \
        INCBIN_TBL_(name, 4), \
        INCBIN_TBL_(name, 5), \
        INCBIN_TBL_(name, 6) \
    }; \
    enum { name##_count = 7 }

#define INCBIN_8(name, f0, f1, f2, f3, f4, f5, f6, f7) \
    __asm__( \
        INCBIN_SECTION_ \
        INCBIN_ASM_(name, 0, f0) \
        INCBIN_ASM_(name, 1, f1) \
        INCBIN_ASM_(name, 2, f2) \
        INCBIN_ASM_(name, 3, f3) \
        INCBIN_ASM_(name, 4, f4) \
        INCBIN_ASM_(name, 5, f5) \
        INCBIN_ASM_(name, 6, f6) \
        INCBIN_ASM_(name, 7, f7) \
        ".previous\n" \
    ); \
    INCBIN_DECL_(name, 0); \
    INCBIN_DECL_(name, 1); \
    INCBIN_DECL_(name, 2); \
    INCBIN_DECL_(name, 3); \
    INCBIN_DECL_(name, 4); \
    INCBIN_DECL_(name, 5); \
    INCBIN_DECL_(name, 6); \
    INCBIN_DECL_(name, 7); \
    static const incbin_entry __attribute__((unused)) name[] = { \
        INCBIN_TBL_(name, 0), \
        INCBIN_TBL_(name, 1), \
        INCBIN_TBL_(name, 2), \
        INCBIN_TBL_(name, 3), \
        INCBIN_TBL_(name, 4), \
        INCBIN_TBL_(name, 5), \
        INCBIN_TBL_(name, 6), \
        INCBIN_TBL_(name, 7) \
    }; \
    enum { name##_count = 8 }

/* ------------------------------------------------------------------ */
/*  INCBIN - public variadic entry point                               */
/* ------------------------------------------------------------------ */

#define INCBIN(name, ...) \
    INCBIN_CAT(INCBIN_, INCBIN_NARGS(__VA_ARGS__))(name, __VA_ARGS__)

/* ------------------------------------------------------------------ */
/*  INCBIN_EXTERN - declare symbols from another TU (count required)   */
/* ------------------------------------------------------------------ */

#define INCBIN_EXTERN(name, count) \
    INCBIN_CAT(INCBIN_EXTERN_, count)(name)

#define INCBIN_EXTERN_1(name) \
    INCBIN_DECL_(name, 0); \
    static const incbin_entry __attribute__((unused)) name[] = { INCBIN_TBL_(name, 0) }; \
    enum { name##_count = 1 }

#define INCBIN_EXTERN_2(name) \
    INCBIN_DECL_(name, 0); INCBIN_DECL_(name, 1); \
    static const incbin_entry __attribute__((unused)) name[] = { \
        INCBIN_TBL_(name, 0), INCBIN_TBL_(name, 1) }; \
    enum { name##_count = 2 }

#define INCBIN_EXTERN_3(name) \
    INCBIN_DECL_(name, 0); INCBIN_DECL_(name, 1); INCBIN_DECL_(name, 2); \
    static const incbin_entry __attribute__((unused)) name[] = { \
        INCBIN_TBL_(name, 0), INCBIN_TBL_(name, 1), INCBIN_TBL_(name, 2) }; \
    enum { name##_count = 3 }

#define INCBIN_EXTERN_4(name) \
    INCBIN_DECL_(name, 0); INCBIN_DECL_(name, 1); \
    INCBIN_DECL_(name, 2); INCBIN_DECL_(name, 3); \
    static const incbin_entry __attribute__((unused)) name[] = { \
        INCBIN_TBL_(name, 0), INCBIN_TBL_(name, 1), \
        INCBIN_TBL_(name, 2), INCBIN_TBL_(name, 3) }; \
    enum { name##_count = 4 }

#define INCBIN_EXTERN_5(name) \
    INCBIN_DECL_(name, 0); INCBIN_DECL_(name, 1); \
    INCBIN_DECL_(name, 2); INCBIN_DECL_(name, 3); \
    INCBIN_DECL_(name, 4); \
    static const incbin_entry __attribute__((unused)) name[] = { \
        INCBIN_TBL_(name, 0), INCBIN_TBL_(name, 1), \
        INCBIN_TBL_(name, 2), INCBIN_TBL_(name, 3), \
        INCBIN_TBL_(name, 4) }; \
    enum { name##_count = 5 }

#define INCBIN_EXTERN_6(name) \
    INCBIN_DECL_(name, 0); INCBIN_DECL_(name, 1); \
    INCBIN_DECL_(name, 2); INCBIN_DECL_(name, 3); \
    INCBIN_DECL_(name, 4); INCBIN_DECL_(name, 5); \
    static const incbin_entry __attribute__((unused)) name[] = { \
        INCBIN_TBL_(name, 0), INCBIN_TBL_(name, 1), \
        INCBIN_TBL_(name, 2), INCBIN_TBL_(name, 3), \
        INCBIN_TBL_(name, 4), INCBIN_TBL_(name, 5) }; \
    enum { name##_count = 6 }

#define INCBIN_EXTERN_7(name) \
    INCBIN_DECL_(name, 0); INCBIN_DECL_(name, 1); \
    INCBIN_DECL_(name, 2); INCBIN_DECL_(name, 3); \
    INCBIN_DECL_(name, 4); INCBIN_DECL_(name, 5); \
    INCBIN_DECL_(name, 6); \
    static const incbin_entry __attribute__((unused)) name[] = { \
        INCBIN_TBL_(name, 0), INCBIN_TBL_(name, 1), \
        INCBIN_TBL_(name, 2), INCBIN_TBL_(name, 3), \
        INCBIN_TBL_(name, 4), INCBIN_TBL_(name, 5), \
        INCBIN_TBL_(name, 6) }; \
    enum { name##_count = 7 }

#define INCBIN_EXTERN_8(name) \
    INCBIN_DECL_(name, 0); INCBIN_DECL_(name, 1); \
    INCBIN_DECL_(name, 2); INCBIN_DECL_(name, 3); \
    INCBIN_DECL_(name, 4); INCBIN_DECL_(name, 5); \
    INCBIN_DECL_(name, 6); INCBIN_DECL_(name, 7); \
    static const incbin_entry __attribute__((unused)) name[] = { \
        INCBIN_TBL_(name, 0), INCBIN_TBL_(name, 1), \
        INCBIN_TBL_(name, 2), INCBIN_TBL_(name, 3), \
        INCBIN_TBL_(name, 4), INCBIN_TBL_(name, 5), \
        INCBIN_TBL_(name, 6), INCBIN_TBL_(name, 7) }; \
    enum { name##_count = 8 }

/* ------------------------------------------------------------------ */
/*  Convenience accessors                                              */
/* ------------------------------------------------------------------ */

/*
 * incbin_size() — compute entry size without double-evaluation risk.
 * Prefer this over INCBIN_SIZE_AT when the index is an expression
 * with side effects (e.g. i++).
 */
static inline size_t incbin_size(const incbin_entry *entry) {
    return (size_t)(entry->end - entry->data);
}

/** Data pointer of first entry (single-file shorthand). */
#define INCBIN_PTR(name)          ((name)[0].data)

/** Size of first entry in bytes (single-file shorthand). */
#define INCBIN_SIZE(name) \
    ((size_t)((name)[0].end - (name)[0].data))

/** Data pointer of entry i. */
#define INCBIN_PTR_AT(name, i)    ((name)[(i)].data)

/*
 * Size of entry i in bytes.
 * WARNING: evaluates 'name' and 'i' twice.  Do not pass expressions
 * with side effects (e.g. idx++).  Use incbin_size(&name[i]) instead.
 */
#define INCBIN_SIZE_AT(name, i) \
    ((size_t)((name)[(i)].end - (name)[(i)].data))

/** Number of embedded entries (compile-time constant). */
#define INCBIN_COUNT(name)        ((size_t)(name##_count))

#endif /* INCBIN_H_ */
