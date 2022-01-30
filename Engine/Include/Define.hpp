#ifndef __DEFINE_H__
#define __DEFINE_H__

#include <utility>

namespace CD
{
    constexpr auto EngineVersion = ENGINE_VERSION;
    using byte = unsigned char;
}



/** @brief Platform dedecttion */ 
#if defined(__linux__) || defined(__gnu_linux__) //? Linux
    #define PLATFORM_LINUX 1
    #if defined(__ANDROID__)
        #define PLATFORM_ANDROID 1
    #endif
#elif defined(WIN32) || defined(_WIN32) || defined(__WIN32__) //? Windows
    #define PLATFORM_WINDOWS 1
    #ifndef _WIN64
        #error "64-bit is required on Windows!"
    #endif
#elif defined(__unix__) //? Unix
    #define PLATFORM_UNIX 1
#elif defined(_POSIX_VERSION) //? Posix
    #define PLATFORM_POSIX 1
#elif defined(__APPLE__) //? Apple
    #include <TargetConditionals.h>
    #define KPLATFORM_APPLE 1
    #if defined(IPHONE) //? Apple IOS
        #define PLATFORM_IOS 1
    #elif defined(__MACH__)
        #define PLATFORM_MAC 1 //? Apple MAC
    #endif
#else 
    #error "Unknown operating system"
#endif

/** @brief Import export routines*/
#ifndef CDAPI
    #if defined(EXPORT_DLL)
        #if defined(_WIN32) || defined(__CYGWIN__)
            #if defined(EXPORT_DLL_BUILD)
                #if defined(__GNUC__)
                    #define CDAPI __attribute__ ((dllexport)) 
                #else
                    #define CDAPI __declspec(dllexport) 
                #endif
            #else
                #if defined(__GNUC__)
                    #define CDAPI __attribute__ ((dllimport)) 
                #else
                    #define CDAPI __declspec(dllimport) 
                #endif
            #endif
        #elif defined(__GNUC__) && defined(EXPORT_DLL_BUILD)
            #define CDAPI __attribute__ ((visibility ("default"))) 
        #else
            #define CDAPI
        #endif
    #else
        #define CDAPI
    #endif
#endif


/** @brief Inline */
#if defined(__clang__) || defined(__gcc__)
    #define CD_INLINE __attribute__((always_inline)) inline
    #define CD_NOINLINE __attribute__((noinline))
#elif defined(_MSC_VER)
    #define CD_INLINE __forceinline
    #define CD_NOINLINE __declspec(noinline)
#else
    #define CD_INLINE static inline
    #define CD_NOINLINE
#endif

#ifdef PLATFORM_WINDOWS
    //#pragma warning(push)
    #pragma warning(disable:4251)
    //your declarations that cause 4251
    //#pragma warning(pop)
#endif


#endif // __DEFINE_H__