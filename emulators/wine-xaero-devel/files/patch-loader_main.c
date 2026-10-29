--- loader/main.c.orig
+++ loader/main.c
@@ -39,6 +39,7 @@
 #endif
 
 #include "main.h"
+#include "mmap32.h"
 
 #if defined(__APPLE__) && defined(__x86_64__) && !defined(HAVE_WINE_PRELOADER)
 
@@ -177,6 +178,7 @@
         (handle = try_dlopen( argv[0] )))
     {
         void (*init_func)(int, char **) = dlsym( handle, "__wine_main" );
+        mmap32_set_ntdll_handle( handle );
         if (init_func) init_func( argc, argv );
         fprintf( stderr, "wine: __wine_main function not found in ntdll.so\n" );
         exit(1);
