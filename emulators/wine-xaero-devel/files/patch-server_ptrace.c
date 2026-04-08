--- server/ptrace.c.orig	2026-04-02 13:52:59.679045000 -0700
+++ server/ptrace.c	2026-04-02 14:13:41.023137000 -0700
@@ -400,6 +438,16 @@ int read_process_memory( struct process *process, clie
                     len = 0;
                     goto done;
                 }
+#if defined(__FreeBSD__) || defined(__FreeBSD_kernel__)
+                /* On FreeBSD, /proc/pid/mem throws ENOMEM on unmapped gaps. 
+                   Map this to a partial copy so Windows DRM scanners don't panic. */
+                if (ret == -1 && errno == ENOMEM)
+                {
+                    set_error( STATUS_PARTIAL_COPY );
+                    resume_after_ptrace( thread );
+                    return 0;
+                }
+#endif
             }
         }
 
