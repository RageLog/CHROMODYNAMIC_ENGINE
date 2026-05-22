#ifndef CD_DEFINITIONS_HPP_INCLUDED
#define CD_DEFINITIONS_HPP_INCLUDED

#if !defined(__cplusplus)
    #error "cd_definitions.hpp is C++-only; do not include from a C translation unit."
#endif

/* ========================================================================== */
/* SECTION 00 - HEADER VERSION                                                */
/* ========================================================================== */
#define CD_VERSION_MAJOR 3
#define CD_VERSION_MINOR 1
#define CD_VERSION_PATCH 0
#define CD_VERSION       ((CD_VERSION_MAJOR * 10000) + (CD_VERSION_MINOR * 100) + (CD_VERSION_PATCH))

/* ========================================================================== */
/* SECTION 01 - STRINGIFY / CONCAT / UNIQUE NAMES                             */
/* ========================================================================== */
#define CD_STRINGIFY_IMPL(x) #x
#define CD_STRINGIFY(x)      CD_STRINGIFY_IMPL(x)

#define CD_CONCAT_IMPL(a, b)      a##b
#define CD_CONCAT(a, b)           CD_CONCAT_IMPL(a, b)
#define CD_CONCAT3(a, b, c)       CD_CONCAT(CD_CONCAT(a, b), c)
#define CD_CONCAT4(a, b, c, d)    CD_CONCAT(CD_CONCAT3(a, b, c), d)
#define CD_CONCAT5(a, b, c, d, e) CD_CONCAT(CD_CONCAT4(a, b, c, d), e)

#if defined(__COUNTER__)
    #define CD_UNIQUE_NAME(prefix) CD_CONCAT(prefix, __COUNTER__)
#else
    #define CD_UNIQUE_NAME(prefix) CD_CONCAT(prefix, __LINE__)
#endif

/* Portable cast helpers. Idiomatic C++ casts; suppresses -Wold-style-cast   */
/* and MSVC C26493 wherever the macros expand inside user code.              */
#define CD_CAST(type, expr)  static_cast<type>(expr)
#define CD_RCAST(type, expr) reinterpret_cast<type>(expr)
#define CD_CCAST(type, expr) const_cast<type>(expr)

/* ========================================================================== */
/* SECTION 02 - COMPILER DETECTION                                            */
/*                                                                            */
/*   Order matters. Derivatives that masquerade as their parent compiler are  */
/*   tested first.                                                            */
/* ========================================================================== */
#define CD_COMPILER_MSVC        0
#define CD_COMPILER_GCC         0
#define CD_COMPILER_CLANG       0
#define CD_COMPILER_APPLE_CLANG 0
#define CD_COMPILER_MINGW       0
#define CD_COMPILER_CYGWIN      0
#define CD_COMPILER_ICC         0
#define CD_COMPILER_ICX         0
#define CD_COMPILER_NVCC        0
#define CD_COMPILER_NVHPC       0
#define CD_COMPILER_PGI         0
#define CD_COMPILER_IBMXL       0
#define CD_COMPILER_IBMXLCLANG  0
#define CD_COMPILER_SUNCC       0
#define CD_COMPILER_HPACC       0
#define CD_COMPILER_ARMCC       0
#define CD_COMPILER_ARMCLANG    0
#define CD_COMPILER_IAR         0
#define CD_COMPILER_GHS         0
#define CD_COMPILER_DIAB        0
#define CD_COMPILER_CD_CGT      0 /* Texas Instruments code generator   */
#define CD_COMPILER_TASKING     0
#define CD_COMPILER_EDG         0
#define CD_COMPILER_BORLAND     0
#define CD_COMPILER_WATCOM      0
#define CD_COMPILER_TINYCC      0
#define CD_COMPILER_EMSCRIPTEN  0

#if defined(__INTEL_LLVM_COMPILER)
    #undef CD_COMPILER_ICX
    #define CD_COMPILER_ICX  1
    #define CD_COMPILER_NAME "Intel oneAPI ICX/DPC++"
#elif defined(__INTEL_COMPILER) || defined(__ICC) || defined(__ICL)
    #undef CD_COMPILER_ICC
    #define CD_COMPILER_ICC  1
    #define CD_COMPILER_NAME "Intel Classic ICC"
#elif defined(__NVCC__) || defined(__CUDACC__)
    #undef CD_COMPILER_NVCC
    #define CD_COMPILER_NVCC 1
    #define CD_COMPILER_NAME "NVIDIA NVCC"
#elif defined(__NVCOMPILER)
    #undef CD_COMPILER_NVHPC
    #define CD_COMPILER_NVHPC 1
    #define CD_COMPILER_NAME  "NVIDIA HPC SDK"
#elif defined(__PGI)
    #undef CD_COMPILER_PGI
    #define CD_COMPILER_PGI  1
    #define CD_COMPILER_NAME "Portland Group"
#elif defined(__ibmxl__)
    #undef CD_COMPILER_IBMXLCLANG
    #define CD_COMPILER_IBMXLCLANG 1
    #define CD_COMPILER_NAME       "IBM Open XL"
#elif defined(__IBMCPP__) || defined(__xlC__)
    #undef CD_COMPILER_IBMXL
    #define CD_COMPILER_IBMXL 1
    #define CD_COMPILER_NAME  "IBM XL Classic"
#elif defined(__SUNPRO_CC) || defined(__SUNPRO_C)
    #undef CD_COMPILER_SUNCC
    #define CD_COMPILER_SUNCC 1
    #define CD_COMPILER_NAME  "Oracle Solaris Studio"
#elif defined(__HP_aCC) || defined(__HP_cc)
    #undef CD_COMPILER_HPACC
    #define CD_COMPILER_HPACC 1
    #define CD_COMPILER_NAME  "HP aCC"
#elif defined(__ARMCC_VERSION) && !defined(__clang__)
    #undef CD_COMPILER_ARMCC
    #define CD_COMPILER_ARMCC 1
    #define CD_COMPILER_NAME  "ARM Compiler 5 (armcc)"
#elif defined(__ARMCC_VERSION) && defined(__clang__)
    #undef CD_COMPILER_ARMCLANG
    #define CD_COMPILER_ARMCLANG 1
    #define CD_COMPILER_NAME     "ARM Compiler 6 (armclang)"
#elif defined(__IAR_SYSTEMS_ICC__) || defined(__IAR_SYSTEMS_ICC)
    #undef CD_COMPILER_IAR
    #define CD_COMPILER_IAR  1
    #define CD_COMPILER_NAME "IAR Embedded Workbench"
#elif defined(__ghs__) || defined(__GHS__)
    #undef CD_COMPILER_GHS
    #define CD_COMPILER_GHS  1
    #define CD_COMPILER_NAME "Green Hills MULTI"
#elif defined(__DCC__)
    #undef CD_COMPILER_DIAB
    #define CD_COMPILER_DIAB 1
    #define CD_COMPILER_NAME "Wind River Diab"
#elif defined(__TI_COMPILER_VERSION__) || defined(_TMS320C6X) || defined(__TMS470__)
    #undef CD_COMPILER_CD_CGT
    #define CD_COMPILER_CD_CGT 1
    #define CD_COMPILER_NAME   "Texas Instruments CGT"
#elif defined(__TASKING__)
    #undef CD_COMPILER_TASKING
    #define CD_COMPILER_TASKING 1
    #define CD_COMPILER_NAME    "TASKING VX-Toolset"
#elif defined(__EDG__)
    #undef CD_COMPILER_EDG
    #define CD_COMPILER_EDG  1
    #define CD_COMPILER_NAME "EDG front-end"
#elif defined(__BORLANDC__) || defined(__CODEGEARC__)
    #undef CD_COMPILER_BORLAND
    #define CD_COMPILER_BORLAND 1
    #define CD_COMPILER_NAME    "Borland/Embarcadero"
#elif defined(__WATCOMC__)
    #undef CD_COMPILER_WATCOM
    #define CD_COMPILER_WATCOM 1
    #define CD_COMPILER_NAME   "Open Watcom"
#elif defined(__TINYC__)
    #undef CD_COMPILER_TINYCC
    #define CD_COMPILER_TINYCC 1
    #define CD_COMPILER_NAME   "TinyCC"
#elif defined(__EMSCRIPTEN__)
    #undef CD_COMPILER_EMSCRIPTEN
    #define CD_COMPILER_EMSCRIPTEN 1
    #define CD_COMPILER_NAME       "Emscripten"
#elif defined(__clang__)
    #undef CD_COMPILER_CLANG
    #define CD_COMPILER_CLANG 1
    #if defined(__apple_build_version__)
        #undef CD_COMPILER_APPLE_CLANG
        #define CD_COMPILER_APPLE_CLANG 1
        #define CD_COMPILER_NAME        "Apple Clang"
    #else
        #define CD_COMPILER_NAME "Clang/LLVM"
    #endif
#elif defined(__GNUC__)
    #undef CD_COMPILER_GCC
    #define CD_COMPILER_GCC 1
    #if defined(__MINGW32__) || defined(__MINGW64__)
        #undef CD_COMPILER_MINGW
        #define CD_COMPILER_MINGW 1
        #define CD_COMPILER_NAME  "MinGW GCC"
    #elif defined(__CYGWIN__)
        #undef CD_COMPILER_CYGWIN
        #define CD_COMPILER_CYGWIN 1
        #define CD_COMPILER_NAME   "Cygwin GCC"
    #else
        #define CD_COMPILER_NAME "GCC"
    #endif
#elif defined(_MSC_VER)
    #undef CD_COMPILER_MSVC
    #define CD_COMPILER_MSVC 1
    #define CD_COMPILER_NAME "Microsoft Visual C++"
#else
    #define CD_COMPILER_NAME "Unknown"
#endif

/* GCC-compatible front-ends share most __builtin_xxx and __attribute__ surface. */
#define CD_COMPILER_GCC_COMPAT                                                                                     \
    (CD_COMPILER_GCC || CD_COMPILER_CLANG || CD_COMPILER_APPLE_CLANG || CD_COMPILER_MINGW || CD_COMPILER_CYGWIN || \
     CD_COMPILER_ICC || CD_COMPILER_ICX || CD_COMPILER_NVHPC || CD_COMPILER_ARMCLANG || CD_COMPILER_IBMXLCLANG ||  \
     CD_COMPILER_EMSCRIPTEN)

/* ========================================================================== */
/* SECTION 03 - COMPILER VERSION + COMPARISON HELPERS                         */
/* ========================================================================== */
#if CD_COMPILER_MSVC
    #define CD_COMPILER_VERSION (_MSC_FULL_VER)
#elif CD_COMPILER_ICX
    #define CD_COMPILER_VERSION (__INTEL_LLVM_COMPILER)
#elif CD_COMPILER_ICC
    #define CD_COMPILER_VERSION (__INTEL_COMPILER)
#elif CD_COMPILER_CLANG || CD_COMPILER_APPLE_CLANG || CD_COMPILER_ARMCLANG || CD_COMPILER_IBMXLCLANG
    #define CD_COMPILER_VERSION ((__clang_major__ * 10000) + (__clang_minor__ * 100) + (__clang_patchlevel__))
#elif CD_COMPILER_GCC || CD_COMPILER_MINGW || CD_COMPILER_CYGWIN
    #define CD_COMPILER_VERSION ((__GNUC__ * 10000) + (__GNUC_MINOR__ * 100) + (__GNUC_PATCHLEVEL__))
#elif CD_COMPILER_NVHPC
    #define CD_COMPILER_VERSION \
        ((__NVCOMPILER_MAJOR__ * 10000) + (__NVCOMPILER_MINOR__ * 100) + (__NVCOMPILER_PATCHLEVEL__))
#elif CD_COMPILER_PGI
    #define CD_COMPILER_VERSION ((__PGIC__ * 10000) + (__PGIC_MINOR__ * 100) + (__PGIC_PATCHLEVEL__))
#elif CD_COMPILER_IBMXL
    #define CD_COMPILER_VERSION (__xlC__)
#elif CD_COMPILER_SUNCC
    #define CD_COMPILER_VERSION (__SUNPRO_CC)
#elif CD_COMPILER_ARMCC
    #define CD_COMPILER_VERSION (__ARMCC_VERSION)
#elif CD_COMPILER_IAR
    #define CD_COMPILER_VERSION (__VER__)
#elif CD_COMPILER_CD_CGT
    #define CD_COMPILER_VERSION (__TI_COMPILER_VERSION__)
#elif CD_COMPILER_BORLAND
    #define CD_COMPILER_VERSION (__BORLANDC__)
#elif CD_COMPILER_WATCOM
    #define CD_COMPILER_VERSION (__WATCOMC__)
#else
    #define CD_COMPILER_VERSION (0)
#endif

#define CD_VERSION_ENCODE(maj, min, pat) ((maj) * 10000 + (min) * 100 + (pat))

#define CD_GCC_VERSION_AT_LEAST(maj, min, pat)                       \
    ((CD_COMPILER_GCC || CD_COMPILER_MINGW || CD_COMPILER_CYGWIN) && \
     CD_COMPILER_VERSION >= CD_VERSION_ENCODE(maj, min, pat))

#define CD_CLANG_VERSION_AT_LEAST(maj, min, pat)                                                         \
    ((CD_COMPILER_CLANG || CD_COMPILER_APPLE_CLANG || CD_COMPILER_ARMCLANG || CD_COMPILER_IBMXLCLANG) && \
     CD_COMPILER_VERSION >= CD_VERSION_ENCODE(maj, min, pat))

#define CD_MSVC_VERSION_AT_LEAST(major) (CD_COMPILER_MSVC && _MSC_VER >= (major))

/* ========================================================================== */
/* SECTION 04 - OPERATING-SYSTEM DETECTION                                    */
/* ========================================================================== */
#define CD_OS_WINDOWS    0
#define CD_OS_WIN32      0
#define CD_OS_WIN64      0
#define CD_OS_WINUWP     0
#define CD_OS_CYGWIN     0
#define CD_OS_LINUX      0
#define CD_OS_ANDROID    0
#define CD_OS_DARWIN     0
#define CD_OS_MACOS      0
#define CD_OS_IOS        0
#define CD_OS_TVOS       0
#define CD_OS_WATCHOS    0
#define CD_OS_FREEBSD    0
#define CD_OS_NETBSD     0
#define CD_OS_OPENBSD    0
#define CD_OS_DRAGONFLY  0
#define CD_OS_BSD        0
#define CD_OS_AIX        0
#define CD_OS_HPUX       0
#define CD_OS_SOLARIS    0
#define CD_OS_HAIKU      0
#define CD_OS_BEOS       0
#define CD_OS_OS2        0
#define CD_OS_DOS        0
#define CD_OS_ZOS        0
#define CD_OS_PLAN9      0
#define CD_OS_HURD       0
#define CD_OS_EMSCRIPTEN 0
#define CD_OS_VXWORKS    0
#define CD_OS_QNX        0
#define CD_OS_INTEGRITY  0
#define CD_OS_THREADX    0
#define CD_OS_FREERTOS   0
#define CD_OS_NUCLEUS    0
#define CD_OS_MQX        0
#define CD_OS_ECOS       0
#define CD_OS_RTEMS      0
#define CD_OS_LYNXOS     0
#define CD_OS_OS9        0
#define CD_OS_NUTTX      0
#define CD_OS_ZEPHYR     0

#if defined(_WIN32) || defined(_WIN64) || defined(__WIN32__) || defined(__TOS_WIN__) || defined(__WINDOWS__)
    #undef CD_OS_WINDOWS
    #define CD_OS_WINDOWS 1
    #if defined(_WIN64)
        #undef CD_OS_WIN64
        #define CD_OS_WIN64 1
    #else
        #undef CD_OS_WIN32
        #define CD_OS_WIN32 1
    #endif
    #if defined(WINAPI_FAMILY) && defined(WINAPI_FAMILY_PARTITION)
        #if !WINAPI_FAMILY_PARTITION(WINAPI_PARTITION_DESKTOP)
            #undef CD_OS_WINUWP
            #define CD_OS_WINUWP 1
        #endif
    #endif
    #define CD_OS_NAME "Windows"
#elif defined(__CYGWIN__)
    #undef CD_OS_CYGWIN
    #define CD_OS_CYGWIN 1
    #define CD_OS_NAME   "Cygwin"
#elif defined(__ANDROID__)
    #undef CD_OS_ANDROID
    #define CD_OS_ANDROID 1
    #undef CD_OS_LINUX
    #define CD_OS_LINUX 1
    #define CD_OS_NAME  "Android"
#elif defined(__linux__) || defined(__linux) || defined(linux)
    #undef CD_OS_LINUX
    #define CD_OS_LINUX 1
    #define CD_OS_NAME  "Linux"
#elif defined(__APPLE__) && defined(__MACH__)
    #undef CD_OS_DARWIN
    #define CD_OS_DARWIN 1
    #if defined(__has_include)
        #if __has_include(<TargetConditionals.h>)
            #include <TargetConditionals.h>
        #endif
    #endif
    #if defined(TARGET_OS_WATCH) && TARGET_OS_WATCH
        #undef CD_OS_WATCHOS
        #define CD_OS_WATCHOS 1
        #define CD_OS_NAME    "watchOS"
    #elif defined(TARGET_OS_TV) && TARGET_OS_TV
        #undef CD_OS_TVOS
        #define CD_OS_TVOS 1
        #define CD_OS_NAME "tvOS"
    #elif defined(TARGET_OS_IPHONE) && TARGET_OS_IPHONE
        #undef CD_OS_IOS
        #define CD_OS_IOS  1
        #define CD_OS_NAME "iOS"
    #elif defined(TARGET_OS_MAC) && TARGET_OS_MAC
        #undef CD_OS_MACOS
        #define CD_OS_MACOS 1
        #define CD_OS_NAME  "macOS"
    #else
        #undef CD_OS_MACOS
        #define CD_OS_MACOS 1
        #define CD_OS_NAME  "Darwin"
    #endif
#elif defined(__FreeBSD__)
    #undef CD_OS_FREEBSD
    #define CD_OS_FREEBSD 1
    #undef CD_OS_BSD
    #define CD_OS_BSD  1
    #define CD_OS_NAME "FreeBSD"
#elif defined(__NetBSD__)
    #undef CD_OS_NETBSD
    #define CD_OS_NETBSD 1
    #undef CD_OS_BSD
    #define CD_OS_BSD  1
    #define CD_OS_NAME "NetBSD"
#elif defined(__OpenBSD__)
    #undef CD_OS_OPENBSD
    #define CD_OS_OPENBSD 1
    #undef CD_OS_BSD
    #define CD_OS_BSD  1
    #define CD_OS_NAME "OpenBSD"
#elif defined(__DragonFly__)
    #undef CD_OS_DRAGONFLY
    #define CD_OS_DRAGONFLY 1
    #undef CD_OS_BSD
    #define CD_OS_BSD  1
    #define CD_OS_NAME "DragonFly BSD"
#elif defined(_AIX) || defined(__TOS_AICD__)
    #undef CD_OS_AIX
    #define CD_OS_AIX  1
    #define CD_OS_NAME "AIX"
#elif defined(__hpux) || defined(_HPUCD_SOURCE)
    #undef CD_OS_HPUX
    #define CD_OS_HPUX 1
    #define CD_OS_NAME "HP-UX"
#elif defined(__sun) && defined(__SVR4)
    #undef CD_OS_SOLARIS
    #define CD_OS_SOLARIS 1
    #define CD_OS_NAME    "Solaris/Illumos"
#elif defined(__HAIKU__)
    #undef CD_OS_HAIKU
    #define CD_OS_HAIKU 1
    #define CD_OS_NAME  "Haiku"
#elif defined(__BEOS__)
    #undef CD_OS_BEOS
    #define CD_OS_BEOS 1
    #define CD_OS_NAME "BeOS"
#elif defined(OS2) || defined(_OS2) || defined(__OS2__) || defined(__TOS_OS2__)
    #undef CD_OS_OS2
    #define CD_OS_OS2  1
    #define CD_OS_NAME "OS/2"
#elif defined(MSDOS) || defined(__MSDOS__) || defined(__DOS__)
    #undef CD_OS_DOS
    #define CD_OS_DOS  1
    #define CD_OS_NAME "MS-DOS"
#elif defined(__MVS__) || defined(__HOS_MVS__) || defined(__TOS_MVS__)
    #undef CD_OS_ZOS
    #define CD_OS_ZOS  1
    #define CD_OS_NAME "z/OS"
#elif defined(EPLAN9)
    #undef CD_OS_PLAN9
    #define CD_OS_PLAN9 1
    #define CD_OS_NAME  "Plan 9"
#elif defined(__GNU__) || defined(__gnu_hurd__)
    #undef CD_OS_HURD
    #define CD_OS_HURD 1
    #define CD_OS_NAME "GNU Hurd"
#elif defined(__EMSCRIPTEN__)
    #undef CD_OS_EMSCRIPTEN
    #define CD_OS_EMSCRIPTEN 1
    #define CD_OS_NAME       "Emscripten"
#elif defined(__VXWORKS__) || defined(__vxworks)
    #undef CD_OS_VXWORKS
    #define CD_OS_VXWORKS 1
    #define CD_OS_NAME    "VxWorks"
#elif defined(__QNCD__) || defined(__QNXNTO__)
    #undef CD_OS_QNX
    #define CD_OS_QNX  1
    #define CD_OS_NAME "QNX Neutrino"
#elif defined(__INTEGRITY)
    #undef CD_OS_INTEGRITY
    #define CD_OS_INTEGRITY 1
    #define CD_OS_NAME      "Green Hills INTEGRITY"
#elif defined(__ThreadCD__) || defined(__TCD__) || defined(__AZURE_RTOS_THREADCD__)
    #undef CD_OS_THREADX
    #define CD_OS_THREADX 1
    #define CD_OS_NAME    "ThreadX/Azure RTOS"
#elif defined(FREERTOS) || defined(INC_FREERTOS_H) || defined(__FreeRTOS__)
    #undef CD_OS_FREERTOS
    #define CD_OS_FREERTOS 1
    #define CD_OS_NAME     "FreeRTOS"
#elif defined(NUCLEUS_PLUS) || defined(__nucleus__)
    #undef CD_OS_NUCLEUS
    #define CD_OS_NUCLEUS 1
    #define CD_OS_NAME    "Mentor Nucleus"
#elif defined(__MQCD__)
    #undef CD_OS_MQX
    #define CD_OS_MQX  1
    #define CD_OS_NAME "Synopsys MQX"
#elif defined(__ECOS) || defined(__ECOS__)
    #undef CD_OS_ECOS
    #define CD_OS_ECOS 1
    #define CD_OS_NAME "eCos"
#elif defined(__rtems__)
    #undef CD_OS_RTEMS
    #define CD_OS_RTEMS 1
    #define CD_OS_NAME  "RTEMS"
#elif defined(__Lynx__) || defined(__LYNCD__)
    #undef CD_OS_LYNXOS
    #define CD_OS_LYNXOS 1
    #define CD_OS_NAME   "LynxOS"
#elif defined(__OS9000) || defined(_OSK)
    #undef CD_OS_OS9
    #define CD_OS_OS9  1
    #define CD_OS_NAME "OS-9"
#elif defined(__NuttCD__) || defined(__NUTTCD__)
    #undef CD_OS_NUTTX
    #define CD_OS_NUTTX 1
    #define CD_OS_NAME  "NuttX"
#elif defined(__ZEPHYR__)
    #undef CD_OS_ZEPHYR
    #define CD_OS_ZEPHYR 1
    #define CD_OS_NAME   "Zephyr"
#else
    #define CD_OS_NAME "Unknown"
#endif

/* ========================================================================== */
/* SECTION 05 - OS AGGREGATES                                                 */
/* ========================================================================== */
#define CD_OS_APPLE (CD_OS_DARWIN || CD_OS_MACOS || CD_OS_IOS || CD_OS_TVOS || CD_OS_WATCHOS)

/* CD_OS_UNIX / CD_OS_POSIX are evaluated eagerly because they reference   */
/* defined(...). Using defined() inside an object-like macro that is then  */
/* used in an #if / #elif is undefined behaviour per the C/C++ standards.  */
#if defined(__unix__) || defined(__unix) || CD_OS_LINUX || CD_OS_DARWIN || CD_OS_BSD || CD_OS_AIX || CD_OS_HPUX || \
    CD_OS_SOLARIS || CD_OS_QNX || CD_OS_HAIKU || CD_OS_HURD
    #define CD_OS_UNIX 1
#else
    #define CD_OS_UNIX 0
#endif

#if defined(_POSICD_VERSION) || CD_OS_UNIX || CD_OS_CYGWIN || CD_OS_VXWORKS || CD_OS_RTEMS || CD_OS_QNX
    #define CD_OS_POSIX 1
#else
    #define CD_OS_POSIX 0
#endif
#define CD_OS_RTOS                                                                                                     \
    (CD_OS_VXWORKS || CD_OS_QNX || CD_OS_INTEGRITY || CD_OS_THREADX || CD_OS_FREERTOS || CD_OS_NUCLEUS || CD_OS_MQX || \
     CD_OS_ECOS || CD_OS_RTEMS || CD_OS_LYNXOS || CD_OS_OS9 || CD_OS_NUTTX || CD_OS_ZEPHYR)
#define CD_OS_DESKTOP (CD_OS_WINDOWS || CD_OS_MACOS || CD_OS_LINUX || CD_OS_BSD)
#define CD_OS_MOBILE  (CD_OS_ANDROID || CD_OS_IOS || CD_OS_TVOS || CD_OS_WATCHOS)

/* ========================================================================== */
/* SECTION 06 - CPU ARCHITECTURE DETECTION                                    */
/* ========================================================================== */
#define CD_ARCH_X86       0
#define CD_ARCH_X86_64    0
#define CD_ARCH_IA64      0
#define CD_ARCH_ARM       0
#define CD_ARCH_ARM64     0
#define CD_ARCH_MIPS      0
#define CD_ARCH_MIPS64    0
#define CD_ARCH_POWERPC   0
#define CD_ARCH_POWERPC64 0
#define CD_ARCH_SPARC     0
#define CD_ARCH_SPARC64   0
#define CD_ARCH_RISCV     0
#define CD_ARCH_RISCV64   0
#define CD_ARCH_S390      0
#define CD_ARCH_S390X     0
#define CD_ARCH_ALPHA     0
#define CD_ARCH_M68K      0
#define CD_ARCH_SUPERH    0
#define CD_ARCH_AVR       0
#define CD_ARCH_E2K       0
#define CD_ARCH_LOONGARCH 0
#define CD_ARCH_XTENSA    0
#define CD_ARCH_HEXAGON   0
#define CD_ARCH_WASM      0

#if defined(__x86_64__) || defined(_M_X64) || defined(__amd64__) || defined(__amd64)
    #undef CD_ARCH_X86_64
    #define CD_ARCH_X86_64 1
    #define CD_ARCH_NAME   "x86_64"
#elif defined(__i386__) || defined(_M_IX86) || defined(__i386) || defined(__I86__) || defined(_X86_)
    #undef CD_ARCH_X86
    #define CD_ARCH_X86  1
    #define CD_ARCH_NAME "x86"
#elif defined(__ia64__) || defined(_M_IA64) || defined(__itanium__)
    #undef CD_ARCH_IA64
    #define CD_ARCH_IA64 1
    #define CD_ARCH_NAME "IA-64"
#elif defined(__aarch64__) || defined(_M_ARM64) || defined(_M_ARM64EC)
    #undef CD_ARCH_ARM64
    #define CD_ARCH_ARM64 1
    #define CD_ARCH_NAME  "AArch64"
#elif defined(__arm__) || defined(_M_ARM) || defined(__thumb__) || defined(__TARGET_ARCH_ARM)
    #undef CD_ARCH_ARM
    #define CD_ARCH_ARM  1
    #define CD_ARCH_NAME "ARM"
#elif defined(__mips64) || defined(__mips64__)
    #undef CD_ARCH_MIPS64
    #define CD_ARCH_MIPS64 1
    #define CD_ARCH_NAME   "MIPS64"
#elif defined(__mips__) || defined(__mips) || defined(_R3000) || defined(_R4000)
    #undef CD_ARCH_MIPS
    #define CD_ARCH_MIPS 1
    #define CD_ARCH_NAME "MIPS"
#elif defined(__powerpc64__) || defined(__ppc64__) || defined(__PPC64__)
    #undef CD_ARCH_POWERPC64
    #define CD_ARCH_POWERPC64 1
    #define CD_ARCH_NAME      "PowerPC64"
#elif defined(__powerpc__) || defined(__powerpc) || defined(__ppc__) || defined(_M_PPC) || defined(_ARCH_PPC)
    #undef CD_ARCH_POWERPC
    #define CD_ARCH_POWERPC 1
    #define CD_ARCH_NAME    "PowerPC"
#elif defined(__sparc64__) || defined(__sparcv9)
    #undef CD_ARCH_SPARC64
    #define CD_ARCH_SPARC64 1
    #define CD_ARCH_NAME    "SPARC64"
#elif defined(__sparc__) || defined(__sparc)
    #undef CD_ARCH_SPARC
    #define CD_ARCH_SPARC 1
    #define CD_ARCH_NAME  "SPARC"
#elif defined(__riscv) && (__riscv_xlen == 64)
    #undef CD_ARCH_RISCV64
    #define CD_ARCH_RISCV64 1
    #define CD_ARCH_NAME    "RISC-V 64"
#elif defined(__riscv)
    #undef CD_ARCH_RISCV
    #define CD_ARCH_RISCV 1
    #define CD_ARCH_NAME  "RISC-V"
#elif defined(__s390x__)
    #undef CD_ARCH_S390X
    #define CD_ARCH_S390X 1
    #define CD_ARCH_NAME  "s390x"
#elif defined(__s390__) || defined(__zarch__)
    #undef CD_ARCH_S390
    #define CD_ARCH_S390 1
    #define CD_ARCH_NAME "s390"
#elif defined(__alpha__) || defined(_M_ALPHA)
    #undef CD_ARCH_ALPHA
    #define CD_ARCH_ALPHA 1
    #define CD_ARCH_NAME  "Alpha"
#elif defined(__m68k__) || defined(M68000) || defined(__MC68K__)
    #undef CD_ARCH_M68K
    #define CD_ARCH_M68K 1
    #define CD_ARCH_NAME "M68K"
#elif defined(__sh__) || defined(__SH4__) || defined(__SH3__)
    #undef CD_ARCH_SUPERH
    #define CD_ARCH_SUPERH 1
    #define CD_ARCH_NAME   "SuperH"
#elif defined(__AVR__) || defined(__AVR)
    #undef CD_ARCH_AVR
    #define CD_ARCH_AVR  1
    #define CD_ARCH_NAME "AVR"
#elif defined(__e2k__)
    #undef CD_ARCH_E2K
    #define CD_ARCH_E2K  1
    #define CD_ARCH_NAME "Elbrus 2000"
#elif defined(__loongarch__) || defined(__loongarch64)
    #undef CD_ARCH_LOONGARCH
    #define CD_ARCH_LOONGARCH 1
    #define CD_ARCH_NAME      "LoongArch"
#elif defined(__xtensa__)
    #undef CD_ARCH_XTENSA
    #define CD_ARCH_XTENSA 1
    #define CD_ARCH_NAME   "Xtensa"
#elif defined(__hexagon__)
    #undef CD_ARCH_HEXAGON
    #define CD_ARCH_HEXAGON 1
    #define CD_ARCH_NAME    "Hexagon"
#elif defined(__wasm__) || defined(__wasm32__) || defined(__wasm64__)
    #undef CD_ARCH_WASM
    #define CD_ARCH_WASM 1
    #define CD_ARCH_NAME "WebAssembly"
#else
    #define CD_ARCH_NAME "Unknown"
#endif

/* ========================================================================== */
/* SECTION 07 - SIMD / VECTOR ISA DETECTION                                   */
/* ========================================================================== */
#define CD_SIMD_SSE        0
#define CD_SIMD_SSE2       0
#define CD_SIMD_SSE3       0
#define CD_SIMD_SSSE3      0
#define CD_SIMD_SSE4_1     0
#define CD_SIMD_SSE4_2     0
#define CD_SIMD_AVX        0
#define CD_SIMD_AVX2       0
#define CD_SIMD_AVX512F    0
#define CD_SIMD_FMA        0
#define CD_SIMD_BMI1       0
#define CD_SIMD_BMI2       0
#define CD_SIMD_F16C       0
#define CD_SIMD_AES        0
#define CD_SIMD_SHA        0
#define CD_SIMD_NEON       0
#define CD_SIMD_SVE        0
#define CD_SIMD_SVE2       0
#define CD_SIMD_ARM_CRC32  0
#define CD_SIMD_ARM_CRYPTO 0
#define CD_SIMD_ALTIVEC    0
#define CD_SIMD_VSX        0
#define CD_SIMD_MIPS_MSA   0
#define CD_SIMD_RVV        0
#define CD_SIMD_WASM_SIMD  0

#if defined(__SSE__) || (defined(_M_IX86_FP) && _M_IX86_FP >= 1) || CD_ARCH_X86_64
    #undef CD_SIMD_SSE
    #define CD_SIMD_SSE 1
#endif
#if defined(__SSE2__) || (defined(_M_IX86_FP) && _M_IX86_FP >= 2) || CD_ARCH_X86_64
    #undef CD_SIMD_SSE2
    #define CD_SIMD_SSE2 1
#endif
#if defined(__SSE3__)
    #undef CD_SIMD_SSE3
    #define CD_SIMD_SSE3 1
#endif
#if defined(__SSSE3__)
    #undef CD_SIMD_SSSE3
    #define CD_SIMD_SSSE3 1
#endif
#if defined(__SSE4_1__)
    #undef CD_SIMD_SSE4_1
    #define CD_SIMD_SSE4_1 1
#endif
#if defined(__SSE4_2__)
    #undef CD_SIMD_SSE4_2
    #define CD_SIMD_SSE4_2 1
#endif
#if defined(__AVCD__) || defined(__AVX2__) || defined(__AVX512F__)
    #undef CD_SIMD_AVX
    #define CD_SIMD_AVX 1
#endif
#if defined(__AVX2__) || defined(__AVX512F__)
    #undef CD_SIMD_AVX2
    #define CD_SIMD_AVX2 1
#endif
#if defined(__AVX512F__)
    #undef CD_SIMD_AVX512F
    #define CD_SIMD_AVX512F 1
#endif
#if defined(__FMA__) || defined(__AVX512F__)
    #undef CD_SIMD_FMA
    #define CD_SIMD_FMA 1
#endif
#if defined(__BMI__)
    #undef CD_SIMD_BMI1
    #define CD_SIMD_BMI1 1
#endif
#if defined(__BMI2__)
    #undef CD_SIMD_BMI2
    #define CD_SIMD_BMI2 1
#endif
#if defined(__F16C__)
    #undef CD_SIMD_F16C
    #define CD_SIMD_F16C 1
#endif
#if defined(__AES__)
    #undef CD_SIMD_AES
    #define CD_SIMD_AES 1
#endif
#if defined(__SHA__)
    #undef CD_SIMD_SHA
    #define CD_SIMD_SHA 1
#endif
#if defined(__ARM_NEON) || defined(__ARM_NEON__) || (CD_ARCH_ARM64 && (CD_OS_WINDOWS || CD_OS_APPLE))
    #undef CD_SIMD_NEON
    #define CD_SIMD_NEON 1
#endif
#if defined(__ARM_FEATURE_SVE)
    #undef CD_SIMD_SVE
    #define CD_SIMD_SVE 1
#endif
#if defined(__ARM_FEATURE_SVE2)
    #undef CD_SIMD_SVE2
    #define CD_SIMD_SVE2 1
#endif
#if defined(__ARM_FEATURE_CRC32)
    #undef CD_SIMD_ARM_CRC32
    #define CD_SIMD_ARM_CRC32 1
#endif
#if defined(__ARM_FEATURE_CRYPTO)
    #undef CD_SIMD_ARM_CRYPTO
    #define CD_SIMD_ARM_CRYPTO 1
#endif
#if defined(__ALTIVEC__)
    #undef CD_SIMD_ALTIVEC
    #define CD_SIMD_ALTIVEC 1
#endif
#if defined(__VSCD__)
    #undef CD_SIMD_VSX
    #define CD_SIMD_VSX 1
#endif
#if defined(__mips_msa)
    #undef CD_SIMD_MIPS_MSA
    #define CD_SIMD_MIPS_MSA 1
#endif
#if defined(__riscv_vector)
    #undef CD_SIMD_RVV
    #define CD_SIMD_RVV 1
#endif
#if defined(__wasm_simd128__)
    #undef CD_SIMD_WASM_SIMD
    #define CD_SIMD_WASM_SIMD 1
#endif

/* ========================================================================== */
/* SECTION 08 - POINTER / WORD SIZE / CHAR-BIT                                */
/* ========================================================================== */
#if defined(__SIZEOF_POINTER__)
    #define CD_PTR_SIZE __SIZEOF_POINTER__
#elif defined(_WIN64) || CD_ARCH_X86_64 || CD_ARCH_ARM64 || CD_ARCH_IA64 || CD_ARCH_MIPS64 || CD_ARCH_POWERPC64 || \
    CD_ARCH_SPARC64 || CD_ARCH_RISCV64 || CD_ARCH_S390X || CD_ARCH_LOONGARCH
    #define CD_PTR_SIZE 8
#elif defined(_WIN32) || CD_ARCH_X86 || CD_ARCH_ARM || CD_ARCH_MIPS || CD_ARCH_POWERPC || CD_ARCH_SPARC || \
    CD_ARCH_RISCV || CD_ARCH_S390 || CD_ARCH_M68K || CD_ARCH_SUPERH || CD_ARCH_XTENSA
    #define CD_PTR_SIZE 4
#elif CD_ARCH_AVR
    #define CD_PTR_SIZE 2
#else
    #define CD_PTR_SIZE 4
#endif

#define CD_64BIT (CD_PTR_SIZE == 8)
#define CD_32BIT (CD_PTR_SIZE == 4)
#define CD_16BIT (CD_PTR_SIZE == 2)

#if defined(__CHAR_BIT__)
    #define CD_CHAR_BIT __CHAR_BIT__
#else
    #define CD_CHAR_BIT 8 /* virtually every modern target */
#endif

#if defined(__SIZEOF_INT__)
    #define CD_INT_SIZE __SIZEOF_INT__
#else
    #define CD_INT_SIZE 4
#endif
#if defined(__SIZEOF_LONG__)
    #define CD_LONG_SIZE __SIZEOF_LONG__
#elif CD_OS_WINDOWS
    #define CD_LONG_SIZE 4
#else
    #define CD_LONG_SIZE CD_PTR_SIZE
#endif
#if defined(__SIZEOF_LONG_LONG__)
    #define CD_LLONG_SIZE __SIZEOF_LONG_LONG__
#else
    #define CD_LLONG_SIZE 8
#endif

/* ========================================================================== */
/* SECTION 09 - ENDIANNESS                                                    */
/* ========================================================================== */
#define CD_BYTE_ORDER_LITTLE 1234
#define CD_BYTE_ORDER_BIG    4321
#define CD_BYTE_ORDER_PDP    3412

#if defined(__BYTE_ORDER__) && defined(__ORDER_LITTLE_ENDIAN__) && defined(__ORDER_BIG_ENDIAN__)
    #if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
        #define CD_BYTE_ORDER CD_BYTE_ORDER_LITTLE
    #elif __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
        #define CD_BYTE_ORDER CD_BYTE_ORDER_BIG
    #elif defined(__ORDER_PDP_ENDIAN__) && __BYTE_ORDER__ == __ORDER_PDP_ENDIAN__
        #define CD_BYTE_ORDER CD_BYTE_ORDER_PDP
    #endif
#elif defined(_LITTLE_ENDIAN) || defined(__LITTLE_ENDIAN__) || defined(_M_IX86) || defined(_M_X64) ||        \
    defined(_M_ARM) || defined(_M_ARM64) || CD_ARCH_X86 || CD_ARCH_X86_64 || CD_ARCH_ARM || CD_ARCH_ARM64 || \
    CD_ARCH_IA64 || CD_ARCH_RISCV || CD_ARCH_RISCV64 || CD_ARCH_LOONGARCH || CD_ARCH_WASM
    #define CD_BYTE_ORDER CD_BYTE_ORDER_LITTLE
#elif defined(_BIG_ENDIAN) || defined(__BIG_ENDIAN__) || CD_ARCH_S390 || CD_ARCH_S390X || CD_ARCH_SPARC || \
    CD_ARCH_SPARC64
    #define CD_BYTE_ORDER CD_BYTE_ORDER_BIG
#else
    #define CD_BYTE_ORDER CD_BYTE_ORDER_LITTLE
#endif

#define CD_LITTLE_ENDIAN (CD_BYTE_ORDER == CD_BYTE_ORDER_LITTLE)
#define CD_BIG_ENDIAN    (CD_BYTE_ORDER == CD_BYTE_ORDER_BIG)
#define CD_PDP_ENDIAN    (CD_BYTE_ORDER == CD_BYTE_ORDER_PDP)

/* ========================================================================== */
/* SECTION 10 - C / C++ LANGUAGE STANDARD DETECTION                           */
/* ========================================================================== */
#if defined(_MSVC_LANG)
    #define CD_CXX_LANG _MSVC_LANG
#else
    #define CD_CXX_LANG __cplusplus
#endif

#define CD_CXX98 (CD_CXX_LANG >= 199711L)
#define CD_CXX03 (CD_CXX_LANG >= 199711L)
#define CD_CXX11 (CD_CXX_LANG >= 201103L)
#define CD_CXX14 (CD_CXX_LANG >= 201402L)
#define CD_CXX17 (CD_CXX_LANG >= 201703L)
#define CD_CXX20 (CD_CXX_LANG >= 202002L)
#define CD_CXX23 (CD_CXX_LANG >= 202302L)
#define CD_CXX26 (CD_CXX_LANG >= 202612L)

/* Hosted vs freestanding (per [lib.compliance]). */
#if defined(__STDC_HOSTED__)
    #define CD_HOSTED __STDC_HOSTED__
#else
    #define CD_HOSTED 1
#endif
#define CD_FREESTANDING (!CD_HOSTED)

/* RTTI / exceptions support detection. */
#if defined(__cpp_rtti) || defined(__GXX_RTTI) || defined(_CPPRTTI)
    #define CD_HAS_RTTI 1
#else
    #define CD_HAS_RTTI 0
#endif
#if defined(__cpp_exceptions) || defined(__EXCEPTIONS) || defined(_CPPUNWIND)
    #define CD_HAS_EXCEPTIONS 1
#else
    #define CD_HAS_EXCEPTIONS 0
#endif

/* PIC / PIE detection. */
#if defined(__PIC__) || defined(__pic__)
    #define CD_PIC 1
#else
    #define CD_PIC 0
#endif
#if defined(__PIE__) || defined(__pie__)
    #define CD_PIE 1
#else
    #define CD_PIE 0
#endif

/* ========================================================================== */
/* SECTION 11 - __has_xxx POLYFILLS                                           */
/* ========================================================================== */
#if defined(__has_attribute)
    #define CD_HAS_ATTRIBUTE(x) __has_attribute(x)
#else
    #define CD_HAS_ATTRIBUTE(x) 0
#endif
#if defined(__has_cpp_attribute)
    #define CD_HAS_CPP_ATTRIBUTE(x) __has_cpp_attribute(x)
#else
    #define CD_HAS_CPP_ATTRIBUTE(x) 0
#endif
#if defined(__has_builtin)
    #define CD_HAS_BUILTIN(x) __has_builtin(x)
#else
    #define CD_HAS_BUILTIN(x) 0
#endif
#if defined(__has_include)
    #define CD_HAS_INCLUDE(x)               __has_include(x)
#else
    #define CD_HAS_INCLUDE(x) 0
#endif
#if defined(__has_feature)
    #define CD_HAS_FEATURE(x) __has_feature(x)
#else
    #define CD_HAS_FEATURE(x) 0
#endif
#if defined(__has_extension)
    #define CD_HAS_EXTENSION(x) __has_extension(x)
#else
    #define CD_HAS_EXTENSION(x) CD_HAS_FEATURE(x)
#endif
#if defined(__has_warning)
    #define CD_HAS_WARNING(x) __has_warning(x)
#else
    #define CD_HAS_WARNING(x) 0
#endif

/* ========================================================================== */
/* SECTION 12 - VARIADIC MACROS / __VA_OPT__                                  */
/* ========================================================================== */
#if CD_CXX11 || CD_COMPILER_MSVC
    #define CD_HAS_VARIADIC_MACROS 1
#else
    #define CD_HAS_VARIADIC_MACROS 0
#endif

/* __VA_OPT__ became standard in C++20. It can only appear inside a         */
/* variadic macro's expansion, so we only expose the availability flag and  */
/* let the user invoke __VA_OPT__ directly in their own variadic macros.    */
#if CD_CXX20 || CD_GCC_VERSION_AT_LEAST(8, 0, 0) || CD_CLANG_VERSION_AT_LEAST(9, 0, 0)
    #define CD_HAS_VA_OPT 1
#else
    #define CD_HAS_VA_OPT 0
#endif

/* ========================================================================== */
/* SECTION 13 - C++ LANGUAGE / LIBRARY FEATURE TEST MACROS                    */
/* ========================================================================== */
#if defined(__cpp_constexpr) && __cpp_constexpr >= 200704L
    #define CD_HAS_CONSTEXPR 1
#else
    #define CD_HAS_CONSTEXPR CD_CXX11
#endif
#if defined(__cpp_constexpr) && __cpp_constexpr >= 201304L
    #define CD_HAS_RELAXED_CONSTEXPR 1
#else
    #define CD_HAS_RELAXED_CONSTEXPR CD_CXX14
#endif
#if defined(__cpp_consteval)
    #define CD_HAS_CONSTEVAL 1
#else
    #define CD_HAS_CONSTEVAL 0
#endif
#if defined(__cpp_constinit)
    #define CD_HAS_CONSTINIT 1
#else
    #define CD_HAS_CONSTINIT 0
#endif
#if defined(__cpp_if_constexpr) || CD_CXX17
    #define CD_HAS_IF_CONSTEXPR 1
#else
    #define CD_HAS_IF_CONSTEXPR 0
#endif
#if defined(__cpp_concepts)
    #define CD_HAS_CONCEPTS 1
#else
    #define CD_HAS_CONCEPTS 0
#endif
#if defined(__cpp_lib_concepts)
    #define CD_HAS_LIB_CONCEPTS 1
#else
    #define CD_HAS_LIB_CONCEPTS 0
#endif
#if defined(__cpp_impl_coroutine) || defined(__cpp_coroutines)
    #define CD_HAS_COROUTINES 1
#else
    #define CD_HAS_COROUTINES 0
#endif
#if defined(__cpp_lib_coroutine)
    #define CD_HAS_LIB_COROUTINE 1
#else
    #define CD_HAS_LIB_COROUTINE 0
#endif
#if defined(__cpp_modules)
    #define CD_HAS_MODULES 1
#else
    #define CD_HAS_MODULES 0
#endif
#if defined(__cpp_lib_format)
    #define CD_HAS_LIB_FORMAT 1
#else
    #define CD_HAS_LIB_FORMAT 0
#endif
#if defined(__cpp_lib_print)
    #define CD_HAS_LIB_PRINT 1
#else
    #define CD_HAS_LIB_PRINT 0
#endif
#if defined(__cpp_lib_ranges)
    #define CD_HAS_LIB_RANGES 1
#else
    #define CD_HAS_LIB_RANGES 0
#endif
#if defined(__cpp_lib_span)
    #define CD_HAS_LIB_SPAN 1
#else
    #define CD_HAS_LIB_SPAN 0
#endif
#if defined(__cpp_lib_mdspan)
    #define CD_HAS_LIB_MDSPAN 1
#else
    #define CD_HAS_LIB_MDSPAN 0
#endif
#if defined(__cpp_lib_expected)
    #define CD_HAS_LIB_EXPECTED 1
#else
    #define CD_HAS_LIB_EXPECTED 0
#endif
#if defined(__cpp_lib_optional)
    #define CD_HAS_LIB_OPTIONAL 1
#else
    #define CD_HAS_LIB_OPTIONAL CD_CXX17
#endif
#if defined(__cpp_lib_variant)
    #define CD_HAS_LIB_VARIANT 1
#else
    #define CD_HAS_LIB_VARIANT CD_CXX17
#endif
#if defined(__cpp_lib_filesystem)
    #define CD_HAS_LIB_FILESYSTEM 1
#else
    #define CD_HAS_LIB_FILESYSTEM CD_CXX17
#endif
#if defined(__cpp_lib_source_location)
    #define CD_HAS_LIB_SOURCE_LOCATION 1
#else
    #define CD_HAS_LIB_SOURCE_LOCATION 0
#endif
#if defined(__cpp_lib_three_way_comparison)
    #define CD_HAS_LIB_THREE_WAY 1
#else
    #define CD_HAS_LIB_THREE_WAY 0
#endif
#if defined(__cpp_lib_jthread)
    #define CD_HAS_LIB_JTHREAD 1
#else
    #define CD_HAS_LIB_JTHREAD 0
#endif
#if defined(__cpp_lib_atomic_wait)
    #define CD_HAS_LIB_ATOMIC_WAIT 1
#else
    #define CD_HAS_LIB_ATOMIC_WAIT 0
#endif
#if defined(__cpp_lib_bit_cast)
    #define CD_HAS_LIB_BIT_CAST 1
#else
    #define CD_HAS_LIB_BIT_CAST 0
#endif
#if defined(__cpp_char8_t)
    #define CD_HAS_CHAR8_T 1
#else
    #define CD_HAS_CHAR8_T 0
#endif
#if defined(__cpp_lib_string_view)
    #define CD_HAS_LIB_STRING_VIEW 1
#else
    #define CD_HAS_LIB_STRING_VIEW CD_CXX17
#endif
#if defined(__cpp_lib_byteswap)
    #define CD_HAS_LIB_BYTESWAP 1
#else
    #define CD_HAS_LIB_BYTESWAP 0
#endif
#if defined(__cpp_lib_endian)
    #define CD_HAS_LIB_ENDIAN 1
#else
    #define CD_HAS_LIB_ENDIAN 0
#endif

/* ========================================================================== */
/* SECTION 14 - EXTENDED FLOATING-POINT TYPES                                 */
/* ========================================================================== */
#if defined(__SIZEOF_FLOAT128__) || defined(__FLOAT128__)
    #define CD_HAS_FLOAT128 1
#else
    #define CD_HAS_FLOAT128 0
#endif
#if defined(__FLT16_MACD__) || defined(__ARM_FP16_FORMAT_IEEE)
    #define CD_HAS_FLOAT16 1
#else
    #define CD_HAS_FLOAT16 0
#endif
#if defined(__BFLT16_MACD__) || defined(__ARM_BF16_FORMAT_ALTERNATIVE)
    #define CD_HAS_BFLOAT16 1
#else
    #define CD_HAS_BFLOAT16 0
#endif

/* ========================================================================== */
/* SECTION 15 - PRAGMA & DIAGNOSTIC HELPERS                                   */
/* ========================================================================== */
#if CD_COMPILER_MSVC
    #define CD_PRAGMA(x) __pragma(x)
#else
    #define CD_PRAGMA(x) _Pragma(CD_STRINGIFY(x))
#endif

#if CD_COMPILER_MSVC
    #define CD_DIAG_PUSH             CD_PRAGMA(warning(push))
    #define CD_DIAG_POP              CD_PRAGMA(warning(pop))
    #define CD_DIAG_IGNORE_MSVC(num) CD_PRAGMA(warning(disable : num))
    #define CD_DIAG_IGNORE_GCC(name)
    #define CD_DIAG_IGNORE_CLANG(name)
#elif CD_COMPILER_CLANG || CD_COMPILER_APPLE_CLANG || CD_COMPILER_ICX || CD_COMPILER_ARMCLANG || CD_COMPILER_IBMXLCLANG
    #define CD_DIAG_PUSH CD_PRAGMA(clang diagnostic push)
    #define CD_DIAG_POP  CD_PRAGMA(clang diagnostic pop)
    #define CD_DIAG_IGNORE_MSVC(num)
    #define CD_DIAG_IGNORE_GCC(name)
    #define CD_DIAG_IGNORE_CLANG(name) CD_PRAGMA(clang diagnostic ignored name)
#elif CD_COMPILER_GCC || CD_COMPILER_MINGW || CD_COMPILER_CYGWIN
    #define CD_DIAG_PUSH CD_PRAGMA(GCC diagnostic push)
    #define CD_DIAG_POP  CD_PRAGMA(GCC diagnostic pop)
    #define CD_DIAG_IGNORE_MSVC(num)
    #define CD_DIAG_IGNORE_GCC(name) CD_PRAGMA(GCC diagnostic ignored name)
    #define CD_DIAG_IGNORE_CLANG(name)
#else
    #define CD_DIAG_PUSH
    #define CD_DIAG_POP
    #define CD_DIAG_IGNORE_MSVC(num)
    #define CD_DIAG_IGNORE_GCC(name)
    #define CD_DIAG_IGNORE_CLANG(name)
#endif

/* ========================================================================== */
/* SECTION 16 - CONSTEXPR FAMILY                                              */
/* ========================================================================== */
#if CD_CXX11
    #define CD_CONSTEXPR constexpr
#else
    #define CD_CONSTEXPR
#endif
#if CD_CXX14
    #define CD_CONSTEXPR14 constexpr
#else
    #define CD_CONSTEXPR14 inline
#endif
#if CD_CXX17
    #define CD_CONSTEXPR17 constexpr
#else
    #define CD_CONSTEXPR17 inline
#endif
#if CD_CXX20
    #define CD_CONSTEXPR20 constexpr
#else
    #define CD_CONSTEXPR20 inline
#endif
#if CD_CXX23
    #define CD_CONSTEXPR23 constexpr
#else
    #define CD_CONSTEXPR23 inline
#endif
#if CD_CXX26
    #define CD_CONSTEXPR26 constexpr
#else
    #define CD_CONSTEXPR26 inline
#endif
#if CD_HAS_CONSTEVAL
    #define CD_CONSTEVAL consteval
#else
    #define CD_CONSTEVAL CD_CONSTEXPR
#endif
#if CD_HAS_CONSTINIT
    #define CD_CONSTINIT constinit
#elif CD_HAS_CPP_ATTRIBUTE(clang::require_constant_initialization)
    #define CD_CONSTINIT [[clang::require_constant_initialization]]
#else
    #define CD_CONSTINIT
#endif
#if CD_CXX17
    #define CD_INLINE_VAR inline
#else
    #define CD_INLINE_VAR
#endif
#if CD_HAS_IF_CONSTEXPR
    #define CD_IF_CONSTEXPR if constexpr
#else
    #define CD_IF_CONSTEXPR if
#endif

/* ========================================================================== */
/* SECTION 17 - noexcept / explicit / override / final / nullptr              */
/* ========================================================================== */
#if CD_CXX11
    #define CD_NOEXCEPT            noexcept
    #define CD_NOEXCEPT_IF(expr)   noexcept(expr)
    #define CD_NOEXCEPT_EXPR(expr) noexcept(expr)
    #define CD_OVERRIDE            override
    #define CD_FINAL               final
    #define CD_NULLPTR             nullptr
    #define CD_EXPLICIT_CONV       explicit
#else
    #define CD_NOEXCEPT throw()
    #define CD_NOEXCEPT_IF(expr)
    #define CD_NOEXCEPT_EXPR(expr) false
    #define CD_OVERRIDE
    #define CD_FINAL
    #define CD_NULLPTR 0
    #define CD_EXPLICIT_CONV
#endif

/* ========================================================================== */
/* SECTION 18 - thread_local                                                  */
/* ========================================================================== */
#if CD_CXX11
    #define CD_THREAD_LOCAL thread_local
#elif CD_COMPILER_MSVC
    #define CD_THREAD_LOCAL __declspec(thread)
#elif CD_COMPILER_GCC_COMPAT
    #define CD_THREAD_LOCAL __thread
#else
    #define CD_THREAD_LOCAL
#endif

/* ========================================================================== */
/* SECTION 19 - alignas / alignof / cache line                                */
/* ========================================================================== */
#if CD_CXX11
    #define CD_ALIGNAS(n) alignas(n)
    #define CD_ALIGNOF(t) alignof(t)
#elif CD_COMPILER_MSVC
    #define CD_ALIGNAS(n) __declspec(align(n))
    #define CD_ALIGNOF(t) __alignof(t)
#elif CD_COMPILER_GCC_COMPAT
    #define CD_ALIGNAS(n) __attribute__((aligned(n)))
    #define CD_ALIGNOF(t) __alignof__(t)
#else
    #define CD_ALIGNAS(n)
    #define CD_ALIGNOF(t) sizeof(t)
#endif

#if !defined(CD_CACHELINE_SIZE)
    #if CD_ARCH_ARM64 && CD_OS_APPLE
        #define CD_CACHELINE_SIZE 128
    #elif CD_ARCH_POWERPC64
        #define CD_CACHELINE_SIZE 128
    #else
        #define CD_CACHELINE_SIZE 64
    #endif
#endif
#define CD_CACHE_ALIGNED CD_ALIGNAS(CD_CACHELINE_SIZE)

/* ========================================================================== */
/* SECTION 20 - static_assert (C++98 fallback via negative-array trick)       */
/* ========================================================================== */
#if CD_CXX17
    #define CD_STATIC_ASSERT(cond)          static_assert(cond)
    #define CD_STATIC_ASSERT_MSG(cond, msg) static_assert(cond, msg)
#elif CD_CXX11
    #define CD_STATIC_ASSERT(cond)          static_assert((cond), #cond)
    #define CD_STATIC_ASSERT_MSG(cond, msg) static_assert((cond), msg)
#else
    #define CD_STATIC_ASSERT(cond)          typedef char CD_UNIQUE_NAME(ti_static_assert_)[(cond) ? 1 : -1]
    #define CD_STATIC_ASSERT_MSG(cond, msg) CD_STATIC_ASSERT(cond)
#endif

/* ========================================================================== */
/* SECTION 21 - ATTRIBUTE POLYFILLS                                           */
/* ========================================================================== */
#if CD_CXX17 || CD_HAS_CPP_ATTRIBUTE(nodiscard)
    #define CD_NODISCARD [[nodiscard]]
    #if CD_CXX20 || (CD_HAS_CPP_ATTRIBUTE(nodiscard) >= 201907L)
        #define CD_NODISCARD_MSG(m) [[nodiscard(m)]]
    #else
        #define CD_NODISCARD_MSG(m) [[nodiscard]]
    #endif
#elif CD_HAS_ATTRIBUTE(warn_unused_result) || CD_COMPILER_GCC_COMPAT
    #define CD_NODISCARD        __attribute__((warn_unused_result))
    #define CD_NODISCARD_MSG(m) __attribute__((warn_unused_result))
#elif CD_MSVC_VERSION_AT_LEAST(1700)
    #define CD_NODISCARD        _Check_return_
    #define CD_NODISCARD_MSG(m) _Check_return_
#else
    #define CD_NODISCARD
    #define CD_NODISCARD_MSG(m)
#endif

#if CD_CXX14 || CD_HAS_CPP_ATTRIBUTE(deprecated)
    #define CD_DEPRECATED        [[deprecated]]
    #define CD_DEPRECATED_MSG(m) [[deprecated(m)]]
#elif CD_COMPILER_GCC_COMPAT
    #define CD_DEPRECATED        __attribute__((deprecated))
    #define CD_DEPRECATED_MSG(m) __attribute__((deprecated(m)))
#elif CD_COMPILER_MSVC
    #define CD_DEPRECATED        __declspec(deprecated)
    #define CD_DEPRECATED_MSG(m) __declspec(deprecated(m))
#else
    #define CD_DEPRECATED
    #define CD_DEPRECATED_MSG(m)
#endif

#if CD_CXX17 || CD_HAS_CPP_ATTRIBUTE(maybe_unused)
    #define CD_MAYBE_UNUSED [[maybe_unused]]
#elif CD_HAS_ATTRIBUTE(unused) || CD_COMPILER_GCC_COMPAT
    #define CD_MAYBE_UNUSED __attribute__((unused))
#else
    #define CD_MAYBE_UNUSED
#endif

#if CD_CXX17 || CD_HAS_CPP_ATTRIBUTE(fallthrough)
    #define CD_FALLTHROUGH [[fallthrough]]
#elif CD_HAS_ATTRIBUTE(fallthrough) || CD_GCC_VERSION_AT_LEAST(7, 0, 0)
    #define CD_FALLTHROUGH __attribute__((fallthrough))
#elif CD_COMPILER_CLANG && CD_HAS_CPP_ATTRIBUTE(clang::fallthrough)
    #define CD_FALLTHROUGH [[clang::fallthrough]]
#else
    #define CD_FALLTHROUGH ((void)0)
#endif

#if CD_CXX20 || CD_HAS_CPP_ATTRIBUTE(no_unique_address)
    #define CD_NO_UNIQUE_ADDRESS [[no_unique_address]]
#elif CD_MSVC_VERSION_AT_LEAST(1929) && CD_HAS_CPP_ATTRIBUTE(msvc::no_unique_address)
    #define CD_NO_UNIQUE_ADDRESS [[msvc::no_unique_address]]
#else
    #define CD_NO_UNIQUE_ADDRESS
#endif

/* ========================================================================== */
/* SECTION 22 - BRANCH PREDICTION                                             */
/* ========================================================================== */
#if CD_CXX20 || CD_HAS_CPP_ATTRIBUTE(likely)
    #define CD_ATTR_LIKELY   [[likely]]
    #define CD_ATTR_UNLIKELY [[unlikely]]
#else
    #define CD_ATTR_LIKELY
    #define CD_ATTR_UNLIKELY
#endif

#if CD_HAS_BUILTIN(__builtin_expect) || CD_COMPILER_GCC_COMPAT
    #define CD_LIKELY(x)   (__builtin_expect(!!(x), 1))
    #define CD_UNLIKELY(x) (__builtin_expect(!!(x), 0))
#else
    #define CD_LIKELY(x)   (!!(x))
    #define CD_UNLIKELY(x) (!!(x))
#endif

#if CD_HAS_BUILTIN(__builtin_expect_with_probability)
    #define CD_EXPECT(x, val, prob) (__builtin_expect_with_probability((x), (val), (prob)))
#else
    #define CD_EXPECT(x, val, prob) (x)
#endif

/* ========================================================================== */
/* SECTION 23 - noreturn / assume / unreachable                               */
/* ========================================================================== */
#if CD_CXX11 || CD_HAS_CPP_ATTRIBUTE(noreturn)
    #define CD_NORETURN [[noreturn]]
#elif CD_COMPILER_MSVC
    #define CD_NORETURN __declspec(noreturn)
#elif CD_COMPILER_GCC_COMPAT
    #define CD_NORETURN __attribute__((noreturn))
#else
    #define CD_NORETURN
#endif

#if defined(CD_SAFETY_CRITICAL) && CD_SAFETY_CRITICAL
    #define CD_ASSUME(cond) ((void)0)
#elif CD_CXX23 || CD_HAS_CPP_ATTRIBUTE(assume)
    #define CD_ASSUME(cond) [[assume(cond)]]
#elif CD_COMPILER_MSVC
    #define CD_ASSUME(cond) __assume(cond)
#elif CD_HAS_BUILTIN(__builtin_assume)
    #define CD_ASSUME(cond) __builtin_assume(cond)
#elif CD_GCC_VERSION_AT_LEAST(13, 0, 0)
    #define CD_ASSUME(cond) __attribute__((assume(cond)))
#elif CD_HAS_BUILTIN(__builtin_unreachable) || CD_COMPILER_GCC_COMPAT
    #define CD_ASSUME(cond)              \
        do                               \
        {                                \
            if (!(cond))                 \
                __builtin_unreachable(); \
        }                                \
        while (0)
#else
    #define CD_ASSUME(cond) ((void)0)
#endif

#if defined(CD_SAFETY_CRITICAL) && CD_SAFETY_CRITICAL
    #if CD_HAS_BUILTIN(__builtin_trap) || CD_COMPILER_GCC_COMPAT
        #define CD_UNREACHABLE() __builtin_trap()
    #elif CD_COMPILER_MSVC
        #define CD_UNREACHABLE() __debugbreak()
    #else
        #define CD_UNREACHABLE() ((void)0)
    #endif
#elif CD_HAS_BUILTIN(__builtin_unreachable) || CD_COMPILER_GCC_COMPAT
    #define CD_UNREACHABLE() __builtin_unreachable()
#elif CD_COMPILER_MSVC
    #define CD_UNREACHABLE() __assume(0)
#else
    #define CD_UNREACHABLE() ((void)0)
#endif

/* ========================================================================== */
/* SECTION 24 - INLINE HINTS                                                  */
/* ========================================================================== */
#define CD_INLINE inline

#if CD_COMPILER_MSVC
    #define CD_FORCE_INLINE __forceinline
    #define CD_NEVER_INLINE __declspec(noinline)
#elif CD_COMPILER_GCC_COMPAT
    #define CD_FORCE_INLINE inline __attribute__((always_inline))
    #define CD_NEVER_INLINE __attribute__((noinline))
#elif CD_COMPILER_IAR
    #define CD_FORCE_INLINE CD_PRAGMA(inline = forced) inline
    #define CD_NEVER_INLINE CD_PRAGMA(inline = never)
#else
    #define CD_FORCE_INLINE inline
    #define CD_NEVER_INLINE
#endif

/* ========================================================================== */
/* SECTION 25 - restrict                                                      */
/* ========================================================================== */
#if CD_COMPILER_MSVC
    #define CD_RESTRICT __restrict
#elif CD_COMPILER_GCC_COMPAT
    #define CD_RESTRICT __restrict__
#else
    #define CD_RESTRICT
#endif

/* ========================================================================== */
/* SECTION 26 - FUNCTION ATTRIBUTES                                           */
/* ========================================================================== */
#if CD_HAS_ATTRIBUTE(pure) || CD_COMPILER_GCC_COMPAT
    #define CD_FUNC_PURE __attribute__((pure))
#else
    #define CD_FUNC_PURE
#endif
#if CD_HAS_ATTRIBUTE(const) || CD_COMPILER_GCC_COMPAT
    #define CD_FUNC_CONST __attribute__((const))
#else
    #define CD_FUNC_CONST
#endif
#if CD_HAS_ATTRIBUTE(hot) || CD_COMPILER_GCC_COMPAT
    #define CD_FUNC_HOT __attribute__((hot))
#else
    #define CD_FUNC_HOT
#endif
#if CD_HAS_ATTRIBUTE(cold) || CD_COMPILER_GCC_COMPAT
    #define CD_FUNC_COLD __attribute__((cold))
#else
    #define CD_FUNC_COLD
#endif
#if CD_HAS_ATTRIBUTE(leaf)
    #define CD_FUNC_LEAF __attribute__((leaf))
#else
    #define CD_FUNC_LEAF
#endif
#if CD_HAS_ATTRIBUTE(flatten) || CD_COMPILER_GCC_COMPAT
    #define CD_FUNC_FLATTEN __attribute__((flatten))
#else
    #define CD_FUNC_FLATTEN
#endif
#if CD_HAS_ATTRIBUTE(used) || CD_COMPILER_GCC_COMPAT
    #define CD_FUNC_USED __attribute__((used))
#else
    #define CD_FUNC_USED
#endif
#if CD_HAS_ATTRIBUTE(returns_nonnull)
    #define CD_FUNC_RETURNS_NONNULL __attribute__((returns_nonnull))
#else
    #define CD_FUNC_RETURNS_NONNULL
#endif
#if CD_HAS_ATTRIBUTE(malloc)
    #define CD_FUNC_MALLOC __attribute__((malloc))
#else
    #define CD_FUNC_MALLOC
#endif
#if CD_HAS_ATTRIBUTE(format) || CD_COMPILER_GCC_COMPAT
    #define CD_FUNC_PRINTF(fmt_idx, args_idx) __attribute__((format(printf, fmt_idx, args_idx)))
    #define CD_FUNC_SCANF(fmt_idx, args_idx)  __attribute__((format(scanf, fmt_idx, args_idx)))
#else
    #define CD_FUNC_PRINTF(fmt_idx, args_idx)
    #define CD_FUNC_SCANF(fmt_idx, args_idx)
#endif
#if CD_HAS_ATTRIBUTE(nonnull) || CD_COMPILER_GCC_COMPAT
    #define CD_FUNC_NONNULL(...) __attribute__((nonnull(__VA_ARGS__)))
#else
    #define CD_FUNC_NONNULL(...)
#endif

/* ========================================================================== */
/* SECTION 27 - VISIBILITY / WEAK / SECTION                                   */
/* ========================================================================== */
#if CD_HAS_ATTRIBUTE(weak) || CD_COMPILER_GCC_COMPAT
    #define CD_WEAK __attribute__((weak))
#elif CD_COMPILER_MSVC
    #define CD_WEAK __declspec(selectany)
#else
    #define CD_WEAK
#endif

#if CD_HAS_ATTRIBUTE(section) || CD_COMPILER_GCC_COMPAT
    #define CD_SECTION(name) __attribute__((section(name)))
#elif CD_COMPILER_MSVC
    #define CD_SECTION(name) __declspec(allocate(name))
#else
    #define CD_SECTION(name)
#endif

#if CD_HAS_ATTRIBUTE(visibility) || CD_COMPILER_GCC_COMPAT
    #define CD_VISIBILITY_DEFAULT   __attribute__((visibility("default")))
    #define CD_VISIBILITY_HIDDEN    __attribute__((visibility("hidden")))
    #define CD_VISIBILITY_INTERNAL  __attribute__((visibility("internal")))
    #define CD_VISIBILITY_PROTECTED __attribute__((visibility("protected")))
#else
    #define CD_VISIBILITY_DEFAULT
    #define CD_VISIBILITY_HIDDEN
    #define CD_VISIBILITY_INTERNAL
    #define CD_VISIBILITY_PROTECTED
#endif

/* ========================================================================== */
/* SECTION 28 - CALLING CONVENTIONS                                           */
/* ========================================================================== */
#if CD_OS_WINDOWS && (CD_ARCH_X86 || CD_ARCH_X86_64)
    #define CD_CDECL __cdecl
    #if CD_ARCH_X86
        #define CD_STDCALL  __stdcall
        #define CD_FASTCALL __fastcall
        #define CD_THISCALL __thiscall
    #else
        #define CD_STDCALL
        #define CD_FASTCALL
        #define CD_THISCALL
    #endif
#elif CD_COMPILER_GCC_COMPAT && CD_ARCH_X86
    #define CD_CDECL    __attribute__((cdecl))
    #define CD_STDCALL  __attribute__((stdcall))
    #define CD_FASTCALL __attribute__((fastcall))
    #define CD_THISCALL __attribute__((thiscall))
#else
    #define CD_CDECL
    #define CD_STDCALL
    #define CD_FASTCALL
    #define CD_THISCALL
#endif

/* ========================================================================== */
/* SECTION 29 - DLL / API IMPORT-EXPORT                                       */
/*                                                                            */
/*   Build the shared library with -DCD_API_BUILD_SHARED=1.                   */
/*   Build a static library with  -DCD_API_STATIC=1.                          */
/* ========================================================================== */
#if defined(CD_API_STATIC) && CD_API_STATIC
    #define CD_API_EXPORT
    #define CD_API_IMPORT
    #define CD_API_LOCAL
#elif CD_OS_WINDOWS || CD_OS_CYGWIN
    #if CD_COMPILER_MSVC || CD_COMPILER_ICC || CD_COMPILER_ICX || CD_COMPILER_BORLAND
        #define CD_API_EXPORT __declspec(dllexport)
        #define CD_API_IMPORT __declspec(dllimport)
    #elif CD_COMPILER_GCC_COMPAT
        #define CD_API_EXPORT __attribute__((dllexport))
        #define CD_API_IMPORT __attribute__((dllimport))
    #else
        #define CD_API_EXPORT
        #define CD_API_IMPORT
    #endif
    #define CD_API_LOCAL
#else
    #if CD_HAS_ATTRIBUTE(visibility) || CD_COMPILER_GCC_COMPAT
        #define CD_API_EXPORT __attribute__((visibility("default")))
        #define CD_API_IMPORT __attribute__((visibility("default")))
        #define CD_API_LOCAL  __attribute__((visibility("hidden")))
    #else
        #define CD_API_EXPORT
        #define CD_API_IMPORT
        #define CD_API_LOCAL
    #endif
#endif

#if !defined(CD_API)
    #if defined(CD_API_BUILD_SHARED) && CD_API_BUILD_SHARED
        #define CD_API CD_API_EXPORT
    #else
        #define CD_API CD_API_IMPORT
    #endif
#endif

/* ========================================================================== */
/* SECTION 30 - FUNCTION-NAME INTROSPECTION                                   */
/* ========================================================================== */
#if CD_COMPILER_MSVC
    #define CD_FUNCTION       __FUNCSIG__
    #define CD_FUNCTION_SHORT __FUNCTION__
#elif CD_COMPILER_GCC_COMPAT
    #define CD_FUNCTION       __PRETTY_FUNCTION__
    #define CD_FUNCTION_SHORT __FUNCTION__
#elif defined(__FUNCTION__)
    #define CD_FUNCTION       __FUNCTION__
    #define CD_FUNCTION_SHORT __FUNCTION__
#elif CD_CXX11 || defined(__func__)
    #define CD_FUNCTION       __func__
    #define CD_FUNCTION_SHORT __func__
#else
    #define CD_FUNCTION       "<unknown>"
    #define CD_FUNCTION_SHORT "<unknown>"
#endif

/* ========================================================================== */
/* SECTION 31 - BUILD MODE                                                    */
/* ========================================================================== */
#if defined(CD_DEBUG) && CD_DEBUG
    /* user-forced */
#elif defined(NDEBUG)
    #define CD_RELEASE 1
#elif defined(_DEBUG) || defined(DEBUG)
    #define CD_DEBUG 1
#endif
#if !defined(CD_RELEASE)
    #define CD_RELEASE 0
#endif
#if !defined(CD_DEBUG)
    #define CD_DEBUG 0
#endif

/* ========================================================================== */
/* SECTION 32 - SANITIZER DETECTION + OPT-OUT                                 */
/* ========================================================================== */
#if defined(__SANITIZE_ADDRESS__) || CD_HAS_FEATURE(address_sanitizer)
    #define CD_HAS_ASAN 1
#else
    #define CD_HAS_ASAN 0
#endif
#if defined(__SANITIZE_THREAD__) || CD_HAS_FEATURE(thread_sanitizer)
    #define CD_HAS_TSAN 1
#else
    #define CD_HAS_TSAN 0
#endif
#if CD_HAS_FEATURE(memory_sanitizer)
    #define CD_HAS_MSAN 1
#else
    #define CD_HAS_MSAN 0
#endif
#if defined(__SANITIZE_UNDEFINED__) || CD_HAS_FEATURE(undefined_behavior_sanitizer)
    #define CD_HAS_UBSAN 1
#else
    #define CD_HAS_UBSAN 0
#endif

#if CD_HAS_ATTRIBUTE(no_sanitize) || CD_COMPILER_CLANG
    #define CD_NO_SANITIZE(what) __attribute__((no_sanitize(what)))
#elif CD_HAS_ATTRIBUTE(no_sanitize_address)
    #define CD_NO_SANITIZE(what) __attribute__((no_sanitize_address))
#else
    #define CD_NO_SANITIZE(what)
#endif

/* ========================================================================== */
/* SECTION 33 - MEMORY BARRIER (COMPILER FENCE ONLY)                          */
/* ========================================================================== */
#if CD_COMPILER_MSVC
    #define CD_COMPILER_FENCE() _ReadWriteBarrier()
#elif CD_COMPILER_GCC_COMPAT
    #define CD_COMPILER_FENCE() __asm__ __volatile__("" ::: "memory")
#else
    #define CD_COMPILER_FENCE() ((void)0)
#endif

/* ========================================================================== */
/* SECTION 34 - PREFETCH                                                      */
/* ========================================================================== */
#if CD_HAS_BUILTIN(__builtin_prefetch) || CD_COMPILER_GCC_COMPAT
    #define CD_PREFETCH_R(ptr) __builtin_prefetch((ptr), 0, 3)
    #define CD_PREFETCH_W(ptr) __builtin_prefetch((ptr), 1, 3)
#else
    #define CD_PREFETCH_R(ptr) ((void)(ptr))
    #define CD_PREFETCH_W(ptr) ((void)(ptr))
#endif

/* ========================================================================== */
/* SECTION 35 - DEBUG TRAP / BREAKPOINT                                       */
/* ========================================================================== */
#if CD_COMPILER_MSVC
    #define CD_DEBUG_BREAK() __debugbreak()
#elif CD_HAS_BUILTIN(__builtin_debugtrap)
    #define CD_DEBUG_BREAK() __builtin_debugtrap()
#elif CD_HAS_BUILTIN(__builtin_trap) || CD_COMPILER_GCC_COMPAT
    #define CD_DEBUG_BREAK() __builtin_trap()
#elif CD_ARCH_X86 || CD_ARCH_X86_64
    #define CD_DEBUG_BREAK() __asm__ __volatile__("int $0x03")
#elif CD_ARCH_ARM
    #define CD_DEBUG_BREAK() __asm__ __volatile__(".inst 0xE7F001F0")
#elif CD_ARCH_ARM64
    #define CD_DEBUG_BREAK() __asm__ __volatile__(".inst 0xD4200000")
#else
    #define CD_DEBUG_BREAK() ((void)0)
#endif

/* ========================================================================== */
/* SECTION 36 - LOOP HINTS                                                    */
/* ========================================================================== */
#if CD_COMPILER_CLANG || CD_COMPILER_APPLE_CLANG || CD_COMPILER_ICX
    #define CD_LOOP_UNROLL(n)    CD_PRAGMA(clang loop unroll_count(n))
    #define CD_LOOP_VECTORIZE    CD_PRAGMA(clang loop vectorize(enable))
    #define CD_LOOP_NO_VECTORIZE CD_PRAGMA(clang loop vectorize(disable))
#elif CD_GCC_VERSION_AT_LEAST(8, 0, 0)
    #define CD_LOOP_UNROLL(n)    CD_PRAGMA(GCC unroll n)
    #define CD_LOOP_VECTORIZE    CD_PRAGMA(GCC ivdep)
    #define CD_LOOP_NO_VECTORIZE CD_PRAGMA(GCC novector)
#elif CD_COMPILER_ICC
    #define CD_LOOP_UNROLL(n)    CD_PRAGMA(unroll(n))
    #define CD_LOOP_VECTORIZE    CD_PRAGMA(vector always)
    #define CD_LOOP_NO_VECTORIZE CD_PRAGMA(novector)
#elif CD_COMPILER_MSVC
    #define CD_LOOP_UNROLL(n)
    #define CD_LOOP_VECTORIZE    CD_PRAGMA(loop(ivdep))
    #define CD_LOOP_NO_VECTORIZE CD_PRAGMA(loop(no_vector))
#else
    #define CD_LOOP_UNROLL(n)
    #define CD_LOOP_VECTORIZE
    #define CD_LOOP_NO_VECTORIZE
#endif

/* ========================================================================== */
/* SECTION 37 - C++26 / EXPERIMENTAL ATTRIBUTE POLYFILLS                      */
/* ========================================================================== */
#if CD_HAS_CPP_ATTRIBUTE(indeterminate)
    #define CD_INDETERMINATE [[indeterminate]]
#else
    #define CD_INDETERMINATE
#endif
#if CD_HAS_CPP_ATTRIBUTE(trivially_relocatable)
    #define CD_TRIVIALLY_RELOCATABLE [[trivially_relocatable]]
#elif CD_HAS_CPP_ATTRIBUTE(clang::trivial_abi)
    #define CD_TRIVIALLY_RELOCATABLE [[clang::trivial_abi]]
#else
    #define CD_TRIVIALLY_RELOCATABLE
#endif
#if CD_HAS_CPP_ATTRIBUTE(lifetimebound)
    #define CD_LIFETIMEBOUND [[lifetimebound]]
#elif CD_HAS_CPP_ATTRIBUTE(clang::lifetimebound)
    #define CD_LIFETIMEBOUND [[clang::lifetimebound]]
#elif CD_HAS_CPP_ATTRIBUTE(msvc::lifetimebound)
    #define CD_LIFETIMEBOUND [[msvc::lifetimebound]]
#else
    #define CD_LIFETIMEBOUND
#endif

/* ========================================================================== */
/* SECTION 38 - typeof / offsetof / addressof / launder                       */
/* ========================================================================== */
#if CD_CXX11
    #define CD_TYPEOF(x) decltype(x)
#elif CD_HAS_BUILTIN(__typeof__) || CD_COMPILER_GCC_COMPAT
    #define CD_TYPEOF(x) __typeof__(x)
#else
    /* No portable fallback at preprocessor time. */
    #define CD_TYPEOF(x) /* unsupported */
#endif

#if CD_HAS_BUILTIN(__builtin_offsetof) || CD_COMPILER_GCC_COMPAT
    #define CD_OFFSETOF(t, m) __builtin_offsetof(t, m)
#else
    /* Fallback for ancient toolchains. Defers to the standard offsetof macro */
    /* (which is itself implementation-defined but warning-clean).            */
    #include <cstddef>
    #define CD_OFFSETOF(t, m) offsetof(t, m)
#endif

#if CD_CXX17
    #define CD_LAUNDER(p) __builtin_launder(p)
#elif CD_HAS_BUILTIN(__builtin_launder)
    #define CD_LAUNDER(p) __builtin_launder(p)
#else
    #define CD_LAUNDER(p) (p)
#endif

#if CD_HAS_BUILTIN(__builtin_addressof) || CD_COMPILER_GCC_COMPAT
    #define CD_ADDRESSOF(x) __builtin_addressof(x)
#elif CD_CXX11
    #define CD_ADDRESSOF(x) (&(x))
#else
    #define CD_ADDRESSOF(x) (&(x))
#endif

/* ========================================================================== */
/* SECTION 39 - BIT OPERATIONS / BSWAP / OVERFLOW                             */
/* ========================================================================== */
#if CD_HAS_BUILTIN(__builtin_clz) || CD_COMPILER_GCC_COMPAT
    #define CD_CLZ32(x)      __builtin_clz(CD_CAST(unsigned int, x))
    #define CD_CTZ32(x)      __builtin_ctz(CD_CAST(unsigned int, x))
    #define CD_POPCOUNT32(x) __builtin_popcount(CD_CAST(unsigned int, x))
    #define CD_CLZ64(x)      __builtin_clzll(CD_CAST(unsigned long long, x))
    #define CD_CTZ64(x)      __builtin_ctzll(CD_CAST(unsigned long long, x))
    #define CD_POPCOUNT64(x) __builtin_popcountll(CD_CAST(unsigned long long, x))
#else
    /* MSVC users include <intrin.h> manually and use _BitScanReverse,         */
    /* _BitScanForward and __popcnt — we deliberately do not include here to  */
    /* keep this header dependency-free.                                      */
    #define CD_CLZ32(x)      /* unsupported without <intrin.h> */
    #define CD_CTZ32(x)      /* unsupported */
    #define CD_POPCOUNT32(x) /* unsupported */
    #define CD_CLZ64(x)      /* unsupported */
    #define CD_CTZ64(x)      /* unsupported */
    #define CD_POPCOUNT64(x) /* unsupported */
#endif

#if CD_HAS_BUILTIN(__builtin_bswap16) || CD_GCC_VERSION_AT_LEAST(4, 8, 0)
    #define CD_BSWAP16(x) __builtin_bswap16(CD_CAST(unsigned short, x))
    #define CD_BSWAP32(x) __builtin_bswap32(CD_CAST(unsigned int, x))
    #define CD_BSWAP64(x) __builtin_bswap64(CD_CAST(unsigned long long, x))
#elif CD_COMPILER_MSVC
    /* Users include <stdlib.h> for these. */
    #define CD_BSWAP16(x) _byteswap_ushort(CD_CAST(unsigned short, x))
    #define CD_BSWAP32(x) _byteswap_ulong(CD_CAST(unsigned long, x))
    #define CD_BSWAP64(x) _byteswap_uint64(CD_CAST(unsigned __int64, x))
#else
    #define CD_BSWAP16(x) CD_CAST(unsigned short, ((((x) & 0xFF) << 8) | (((x) >> 8) & 0xFF)))
    #define CD_BSWAP32(x)                                                                            \
        CD_CAST(                                                                                     \
            unsigned int,                                                                            \
            ((((x) & 0x000000FFu) << 24) | (((x) & 0x0000FF00u) << 8) | (((x) & 0x00FF0000u) >> 8) | \
             (((x) & 0xFF000000u) >> 24))                                                            \
        )
    #define CD_BSWAP64(x)                                                       \
        CD_CAST(                                                                \
            unsigned long long,                                                 \
            ((CD_CAST(unsigned long long, x) << 56) |                           \
             ((CD_CAST(unsigned long long, x) & 0x000000000000FF00ULL) << 40) | \
             ((CD_CAST(unsigned long long, x) & 0x0000000000FF0000ULL) << 24) | \
             ((CD_CAST(unsigned long long, x) & 0x00000000FF000000ULL) << 8) |  \
             ((CD_CAST(unsigned long long, x) & 0x000000FF00000000ULL) >> 8) |  \
             ((CD_CAST(unsigned long long, x) & 0x0000FF0000000000ULL) >> 24) | \
             ((CD_CAST(unsigned long long, x) & 0x00FF000000000000ULL) >> 40) | \
             (CD_CAST(unsigned long long, x) >> 56))                            \
        )
#endif

/* Overflow-checked arithmetic. Returns 1 on overflow, 0 otherwise. */
#if CD_HAS_BUILTIN(__builtin_add_overflow) || CD_GCC_VERSION_AT_LEAST(5, 0, 0)
    #define CD_ADD_OVERFLOW(a, b, out) __builtin_add_overflow((a), (b), (out))
    #define CD_SUB_OVERFLOW(a, b, out) __builtin_sub_overflow((a), (b), (out))
    #define CD_MUL_OVERFLOW(a, b, out) __builtin_mul_overflow((a), (b), (out))
    #define CD_HAS_OVERFLOW_BUILTINS   1
#else
    #define CD_HAS_OVERFLOW_BUILTINS 0
#endif

/* ========================================================================== */
/* SECTION 40 - MISC HELPERS                                                  */
/* ========================================================================== */
#define CD_UNUSED(x)       ((void)(x))
#define CD_ARRAY_SIZE(arr) (sizeof(arr) / sizeof((arr)[0]))

#if CD_CXX11
    #define CD_NON_COPYABLE(T) \
        T(const T&) = delete;  \
        T& operator=(const T&) = delete
    #define CD_NON_MOVABLE(T) \
        T(T&&) = delete;      \
        T& operator=(T&&) = delete
    #define CD_DEFAULT_COPY(T) \
        T(const T&) = default; \
        T& operator=(const T&) = default
    #define CD_DEFAULT_MOVE(T)        \
        T(T&&) CD_NOEXCEPT = default; \
        T& operator=(T&&) CD_NOEXCEPT = default
#else
    #define CD_NON_COPYABLE(T) \
    private:                   \
        T(const T&);           \
        T& operator=(const T&)
    #define CD_NON_MOVABLE(T)
    #define CD_DEFAULT_COPY(T)
    #define CD_DEFAULT_MOVE(T)
#endif

/* ========================================================================== */
/* SECTION 41 - PACKED STRUCTS                                                */
/* ========================================================================== */
#if CD_COMPILER_MSVC
    #define CD_PACKED_BEGIN        CD_PRAGMA(pack(push, 1))
    #define CD_PACKED_END          CD_PRAGMA(pack(pop))
    #define CD_PACKED_STRUCT(name) struct name
#elif CD_COMPILER_GCC_COMPAT
    #define CD_PACKED_BEGIN
    #define CD_PACKED_END
    #define CD_PACKED_STRUCT(name) struct __attribute__((packed)) name
#else
    #define CD_PACKED_BEGIN
    #define CD_PACKED_END
    #define CD_PACKED_STRUCT(name) struct name
#endif

/* ========================================================================== */
/* SECTION 42 - NAMESPACE HELPERS                                             */
/* ========================================================================== */
#define CD_NAMESPACE_BEGIN(ns) \
    namespace ns               \
    {
#define CD_NAMESPACE_END(ns) }

#if CD_CXX17
    #define CD_NAMESPACE_BEGIN2(a, b) \
        namespace a::b                \
        {
    #define CD_NAMESPACE_END2(a, b) }
    #define CD_NAMESPACE_BEGIN3(a, b, c) \
        namespace a::b::c                \
        {
    #define CD_NAMESPACE_END3(a, b, c) }
#else
    #define CD_NAMESPACE_BEGIN2(a, b) \
        namespace a                   \
        {                             \
        namespace b                   \
        {
    #define CD_NAMESPACE_END2(a, b) \
        }                           \
        }
    #define CD_NAMESPACE_BEGIN3(a, b, c) \
        namespace a                      \
        {                                \
        namespace b                      \
        {                                \
        namespace c                      \
        {
    #define CD_NAMESPACE_END3(a, b, c) \
        }                              \
        }                              \
        }
#endif

/* ========================================================================== */
/* SECTION 43 - JNI EXPORT DECORATION                                         */
/* ========================================================================== */
#if CD_OS_WINDOWS
    #define CD_JNI_EXPORT __declspec(dllexport)
    #define CD_JNI_CALL   __stdcall
#elif CD_HAS_ATTRIBUTE(visibility) || CD_COMPILER_GCC_COMPAT
    #define CD_JNI_EXPORT __attribute__((visibility("default")))
    #define CD_JNI_CALL
#else
    #define CD_JNI_EXPORT
    #define CD_JNI_CALL
#endif

/* ========================================================================== */
/* SECTION 44 - SAFETY-CRITICAL (DO-178C) PROFILE                             */
/*                                                                            */
/*   Define CD_SAFETY_CRITICAL=1 in the build to:                             */
/*     * make CD_ASSUME() a no-op (no UB-driven optimization)                 */
/*     * make CD_UNREACHABLE() a deterministic trap (defined behaviour)       */
/* ========================================================================== */
#if !defined(CD_SAFETY_CRITICAL)
    #define CD_SAFETY_CRITICAL 0
#endif

/* ========================================================================== */
/* SECTION 45 - LANGUAGE LINKAGE (extern "C" / extern "C++")                  */
/* ========================================================================== */
#define CD_EXTERN_C   extern "C"
#define CD_EXTERN_CXX extern "C++"
#define CD_EXTERN_C_BEGIN \
    extern "C"            \
    {
#define CD_EXTERN_C_END }

/* ========================================================================== */
/* SECTION 46 - register / volatile / decltype / typeid                       */
/* ========================================================================== */
/* register: storage-class through C++14, deprecated in C++11, removed in    */
/* C++17. Empty in C++11+ to avoid -Wdeprecated-register noise.              */
#if CD_CXX11
    #define CD_REGISTER
#else
    #define CD_REGISTER register
#endif

/* volatile: provided as a token name for symmetry / generated code.         */
#define CD_VOLATILE volatile

/* decltype: native in C++11+, fall back to typeof on older GCC/Clang.        */
#if CD_CXX11
    #define CD_DECLTYPE(x) decltype(x)
#elif CD_HAS_BUILTIN(__typeof__) || CD_COMPILER_GCC_COMPAT
    #define CD_DECLTYPE(x) __typeof__(x)
#elif CD_MSVC_VERSION_AT_LEAST(1600)
    #define CD_DECLTYPE(x) decltype(x)
#else
    #define CD_DECLTYPE(x) /* unsupported on this toolchain */
#endif

/* typeid is available only when RTTI is enabled. */
#if CD_HAS_RTTI
    #define CD_TYPEID(x) typeid(x)
#else
    #define CD_TYPEID(x) ((void)(x))
#endif

/* ========================================================================== */
/* SECTION 47 - VOLATILE / MMIO ACCESS HELPERS                                */
/*    Deliberately *not* atomic. Use std::atomic / atomic_thread_fence for    */
/*    cross-CPU synchronisation; these helpers are for register-mapped IO    */
/*    or for forcing the compiler to honour a memory access.                 */
/* ========================================================================== */
#define CD_MMIO_READ8(addr)  (*CD_RCAST(volatile unsigned char*, addr))
#define CD_MMIO_READ16(addr) (*CD_RCAST(volatile unsigned short*, addr))
#define CD_MMIO_READ32(addr) (*CD_RCAST(volatile unsigned int*, addr))
#define CD_MMIO_READ64(addr) (*CD_RCAST(volatile unsigned long long*, addr))

#define CD_MMIO_WRITE8(addr, v)  ((*CD_RCAST(volatile unsigned char*, addr)) = CD_CAST(unsigned char, v))
#define CD_MMIO_WRITE16(addr, v) ((*CD_RCAST(volatile unsigned short*, addr)) = CD_CAST(unsigned short, v))
#define CD_MMIO_WRITE32(addr, v) ((*CD_RCAST(volatile unsigned int*, addr)) = CD_CAST(unsigned int, v))
#define CD_MMIO_WRITE64(addr, v) ((*CD_RCAST(volatile unsigned long long*, addr)) = CD_CAST(unsigned long long, v))

/* Type-explicit volatile read/write to avoid relying on CD_TYPEOF support.  */
#define CD_VOLATILE_READ_AS(type, ptr)       (*CD_RCAST(volatile type*, ptr))
#define CD_VOLATILE_WRITE_AS(type, ptr, val) ((*CD_RCAST(volatile type*, ptr)) = (val))

/* ========================================================================== */
/* SECTION 48 - EXCEPTION FALLBACK (try / catch / throw)                      */
/*                                                                            */
/*   When CD_HAS_EXCEPTIONS == 0 (e.g. -fno-exceptions or /EHs- builds):     */
/*     * CD_TRY  becomes  if (1)                                              */
/*     * CD_CATCH/CATCH_ALL become unreachable else-if blocks; the catch     */
/*       parameter declaration will not compile in this mode, so wrap the    */
/*       parameter site with #if CD_HAS_EXCEPTIONS for full portability.     */
/*     * CD_THROW / CD_RETHROW degrade to a deterministic debug-trap.        */
/* ========================================================================== */
#if CD_HAS_EXCEPTIONS
    #define CD_TRY         try
    #define CD_CATCH(decl) catch (decl)
    #define CD_CATCH_ALL   catch (...)
    #define CD_THROW(x)    throw(x)
    #define CD_RETHROW     throw
#else
    #define CD_TRY         if (1)
    #define CD_CATCH(decl) else if (0)
    #define CD_CATCH_ALL   else if (0)
    #define CD_THROW(x)       \
        do                    \
        {                     \
            (void)(x);        \
            CD_DEBUG_BREAK(); \
        }                     \
        while (0)
    #define CD_RETHROW CD_DEBUG_BREAK()
#endif

/* ========================================================================== */
/* SECTION 49 - COROUTINE KEYWORD POLYFILLS                                   */
/* ========================================================================== */
#if CD_HAS_COROUTINES
    #define CD_CO_AWAIT(x)    co_await (x)
    #define CD_CO_YIELD(x)    co_yield (x)
    #define CD_CO_RETURN(x)   co_return (x)
    #define CD_CO_RETURN_VOID co_return
#else
    #define CD_CO_AWAIT(x)    (x) /* coroutines unavailable */
    #define CD_CO_YIELD(x)    (x) /* coroutines unavailable */
    #define CD_CO_RETURN(x)   return (x)
    #define CD_CO_RETURN_VOID return
#endif

/* ========================================================================== */
/* SECTION 50 - CONCEPTS / REQUIRES POLYFILLS                                 */
/*    The fallbacks expand to nothing; concept/requires syntax requires the   */
/*    C++20 grammar, so for full portability bracket the declaration with    */
/*    #if CD_HAS_CONCEPTS.                                                   */
/* ========================================================================== */
#if CD_HAS_CONCEPTS
    #define CD_CONCEPT              concept
    #define CD_REQUIRES_CLAUSE(...) requires(__VA_ARGS__)
#else
    #define CD_CONCEPT              /* concepts unavailable */
    #define CD_REQUIRES_CLAUSE(...) /* requires-clause unavailable */
#endif

/* ========================================================================== */
/* SECTION 51 - INLINE ASSEMBLY                                               */
/* ========================================================================== */
#if CD_HAS_VARIADIC_MACROS
    #if CD_COMPILER_MSVC
        #if CD_ARCH_X86
            #define CD_INLINE_ASM(...) __asm { __VA_ARGS__ }
        #else
            #define CD_INLINE_ASM(...) /* MSVC has no inline-asm on this target */
        #endif
    #elif CD_COMPILER_GCC_COMPAT
        #define CD_INLINE_ASM(...) __asm__ __volatile__(__VA_ARGS__)
    #elif CD_COMPILER_IAR
        #define CD_INLINE_ASM(...) asm(__VA_ARGS__)
    #elif CD_COMPILER_CD_CGT
        #define CD_INLINE_ASM(...) asm(__VA_ARGS__)
    #else
        #define CD_INLINE_ASM(...) /* inline asm unsupported on this toolchain */
    #endif
#endif

/* ========================================================================== */
/* SECTION 52 - WCHAR / CHARACTER TYPE FLAGS                                  */
/* ========================================================================== */
#if defined(__SIZEOF_WCHAR_T__)
    #define CD_WCHAR_SIZE __SIZEOF_WCHAR_T__
#elif CD_OS_WINDOWS
    #define CD_WCHAR_SIZE 2
#else
    #define CD_WCHAR_SIZE 4
#endif

#define CD_HAS_WCHAR_T 1

#if CD_CXX11 || CD_HAS_FEATURE(cxx_unicode_literals)
    #define CD_HAS_CHAR16_T         1
    #define CD_HAS_CHAR32_T         1
    #define CD_HAS_UNICODE_LITERALS 1
#else
    #define CD_HAS_CHAR16_T         0
    #define CD_HAS_CHAR32_T         0
    #define CD_HAS_UNICODE_LITERALS 0
#endif

#if CD_CXX11 || CD_HAS_FEATURE(cxx_raw_string_literals)
    #define CD_HAS_RAW_STRING_LITERALS 1
#else
    #define CD_HAS_RAW_STRING_LITERALS 0
#endif

/* ========================================================================== */
/* SECTION 53 - C++11 LANGUAGE FEATURE FLAGS                                  */
/* ========================================================================== */
#if defined(__cpp_rvalue_references) || CD_CXX11
    #define CD_HAS_RVALUE_REFS 1
#else
    #define CD_HAS_RVALUE_REFS 0
#endif
#if defined(__cpp_lambdas) || CD_CXX11
    #define CD_HAS_LAMBDAS 1
#else
    #define CD_HAS_LAMBDAS 0
#endif
#if defined(__cpp_variadic_templates) || CD_CXX11
    #define CD_HAS_VARIADIC_TEMPLATES 1
#else
    #define CD_HAS_VARIADIC_TEMPLATES 0
#endif
#if defined(__cpp_range_based_for) || CD_CXX11
    #define CD_HAS_RANGE_FOR 1
#else
    #define CD_HAS_RANGE_FOR 0
#endif
#if defined(__cpp_initializer_lists) || CD_CXX11
    #define CD_HAS_INITIALIZER_LISTS 1
#else
    #define CD_HAS_INITIALIZER_LISTS 0
#endif
#if defined(__cpp_user_defined_literals) || CD_CXX11
    #define CD_HAS_UDL 1
#else
    #define CD_HAS_UDL 0
#endif
#if defined(__cpp_inheriting_constructors) || CD_CXX11
    #define CD_HAS_INHERITING_CTORS 1
#else
    #define CD_HAS_INHERITING_CTORS 0
#endif
#if defined(__cpp_attributes) || CD_CXX11
    #define CD_HAS_ATTRIBUTE_SYNTAX 1
#else
    #define CD_HAS_ATTRIBUTE_SYNTAX 0
#endif

#define CD_HAS_DELETED_FUNCTIONS       CD_CXX11
#define CD_HAS_DEFAULTED_FUNCTIONS     CD_CXX11
#define CD_HAS_DELEGATING_CONSTRUCTORS CD_CXX11
#define CD_HAS_TRAILING_RETURN_TYPE    CD_CXX11
#define CD_HAS_STRONG_ENUMS            CD_CXX11

/* ========================================================================== */
/* SECTION 54 - PORTABLE NULL POINTER LITERAL                                 */
/* ========================================================================== */
#if CD_CXX11
    #define CD_NULL nullptr
#else
    #define CD_NULL 0
#endif

/* ========================================================================== */
/* SECTION 55 - COMPILE-TIME SELF-TESTS                                       */
/* ========================================================================== */
CD_STATIC_ASSERT_MSG(CD_PTR_SIZE == 2 || CD_PTR_SIZE == 4 || CD_PTR_SIZE == 8, "CD_PTR_SIZE must be 2, 4 or 8");
CD_STATIC_ASSERT_MSG(
    CD_BYTE_ORDER == CD_BYTE_ORDER_LITTLE || CD_BYTE_ORDER == CD_BYTE_ORDER_BIG || CD_BYTE_ORDER == CD_BYTE_ORDER_PDP,
    "CD_BYTE_ORDER must be little, big or pdp"
);
CD_STATIC_ASSERT_MSG(CD_64BIT + CD_32BIT + CD_16BIT == 1, "exactly one of CD_{16,32,64}BIT must be 1");
CD_STATIC_ASSERT_MSG(CD_CHAR_BIT >= 8, "CD_CHAR_BIT must be >= 8");

/* ========================================================================== */
/* SECTION 56 - ENVIRONMENT SUMMARY (string banner)                           */
/* ========================================================================== */
#define CD_ENV_SUMMARY                                                                                    \
    "cd_definitions " CD_STRINGIFY(CD_VERSION_MAJOR) "." CD_STRINGIFY(CD_VERSION_MINOR) "." CD_STRINGIFY( \
        CD_VERSION_PATCH                                                                                  \
    ) " | " CD_COMPILER_NAME " | " CD_OS_NAME " | " CD_ARCH_NAME

#endif /* CD_DEFINITIONS_HPP_INCLUDED */
