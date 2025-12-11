// dawn_support.h - Compiler support macros

#ifndef DAWN_SUPPORT_H
#define DAWN_SUPPORT_H

// C23 enum underlying type support (N3030 - Enhanced Enumerations)
// Requires GCC 13+ or Clang 20+
#if defined(__clang__) && __clang_major__ >= 20
    #define DAWN_ENUM(type) typedef enum : type
#elif defined(__GNUC__) && !defined(__clang__) && __GNUC__ >= 13
    #define DAWN_ENUM(type) typedef enum : type
#else
    #define DAWN_ENUM(type) typedef enum
#endif

#endif // DAWN_SUPPORT_H
