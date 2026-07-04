--- dlls/ntdll/unix/virtual.c.orig	2026-03-20 13:33:36.000000000 -0700
+++ dlls/ntdll/unix/virtual.c	2026-03-25 16:06:57.933543000 -0700
@@ -2223,6 +2223,12 @@ failed:
     {
         ERR( "out of memory for %p-%p\n", base, (char *)base + size );
         status = STATUS_NO_MEMORY;
+            if ((uintptr_t)base == 0x400000)
+            {
+                char buf[100];
+                snprintf(buf, sizeof(buf), "procstat -v %d", getpid());
+                system(buf);
+            }
     }
     else if (errno == EEXIST) status = STATUS_CONFLICTING_ADDRESSES;
     else
