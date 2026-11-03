#ifndef VERSION_H
#define VERSION_H

#define APP_VERSION "0.1.0"

#define _STR(x) #x
#define STR(x) _STR(x)

#ifdef GIT_COMMIT
  #ifdef GIT_DIRTY
    #define APP_VERSION_FULL APP_VERSION "+" STR(GIT_COMMIT) "-dirty"
  #else
    #define APP_VERSION_FULL APP_VERSION "+" STR(GIT_COMMIT)
  #endif
#else
  #define APP_VERSION_FULL APP_VERSION
#endif

#endif
