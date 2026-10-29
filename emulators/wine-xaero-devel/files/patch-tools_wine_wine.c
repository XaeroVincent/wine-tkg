--- tools/wine/wine.c.orig
+++ tools/wine/wine.c
@@ -24,6 +24,8 @@
 
 #include <dlfcn.h>
 
+#include "../../loader/mmap32.h"
+
 static const char *bindir;
 static const char *libdir;
 
@@ -63,10 +65,13 @@
 int main( int argc, char *argv[] )
 {
     void (*init_func)(int, char **);
+    void *handle;
 
     bindir = get_bindir( argv[0] );
     libdir = get_libdir( bindir );
-    init_func = dlsym( load_ntdll(), "__wine_main" );
+    handle = load_ntdll();
+    mmap32_set_ntdll_handle( handle );
+    init_func = dlsym( handle, "__wine_main" );
     if (init_func) init_func( argc, argv );
 
     fprintf( stderr, "wine: __wine_main function not found in ntdll.so\n" );
