#ifndef __DEFINE_H__
#define __DEFINE_H__


// Import export routines
#ifndef CDAPI
# if defined(EXPORT_DLL)
#  if defined(_WIN32) || defined(__CYGWIN__)
#   if defined(EXPORT_DLL_BUILD)
#    if defined(__GNUC__)
#     define CDAPI __attribute__ ((dllexport)) 
#    else
#     define CDAPI __declspec(dllexport) 
#    endif
#   else
#    if defined(__GNUC__)
#     define CDAPI __attribute__ ((dllimport)) 
#    else
#     define CDAPI __declspec(dllimport) 
#    endif
#   endif
#  elif defined(__GNUC__) && defined(EXPORT_DLL_BUILD)
#   define CDAPI __attribute__ ((visibility ("default"))) 
#  else
#   define CDAPI
#  endif
# else
#  define CDAPI
# endif
#endif



#endif // __DEFINE_H__