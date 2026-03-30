--- server/file.c.orig	2026-03-20 13:33:36.000000000 -0700
+++ server/file.c	2026-03-29 23:58:41.939676000 -0700
@@ -619,6 +619,14 @@ void file_set_error(void)
     case ELOOP:     set_error( STATUS_REPARSE_POINT_NOT_RESOLVED ); break;
 #ifdef EOVERFLOW
     case EOVERFLOW: set_error( STATUS_INVALID_PARAMETER ); break;
+#endif
+#if defined(__FreeBSD__) || defined(__FreeBSD_kernel__)
+    case ENOMEM:
+        /* On FreeBSD, /proc/pid/mem returns ENOMEM for unmapped gaps.
+         * STATUS_ACCESS_VIOLATION is the correct mapping: zero bytes were
+         * transferred. */
+        set_error( STATUS_ACCESS_VIOLATION );
+        break;
 #endif
     default:
         perror("wineserver: file_set_error() can't map error");
