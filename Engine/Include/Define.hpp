#ifndef __DEFINE_H__
#define __DEFINE_H__



#ifndef CDAPI
    #ifdef EXPORT_DLL
        #if defined _WIN32 || defined __CYGWIN__
            #ifdef WIN_EXPORT
                // Exporting...
                #ifdef __GNUC__
                #define CDAPI __attribute__ ((dllexport))
                #else
                #define CDAPI __declspec(dllexport) // Note: actually gcc seems to also supports this syntax.
                #endif
            #else
                #ifdef __GNUC__
                #define CDAPI __attribute__ ((dllimport))
                #else
                #define CDAPI __declspec(dllimport) // Note: actually gcc seems to also supports this syntax.
                #endif
            #endif
        #else
            #if __GNUC__ >= 4
                #define CDAPI __attribute__ ((visibility ("default")))
            #else
                #define CDAPI
            #endif
        #endif
    #else
        #define CDAPI
    #endif
#endif

#endif // __DEFINE_H__