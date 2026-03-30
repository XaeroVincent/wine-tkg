--- server/mutex.c.orig	2026-02-20 12:50:32.000000000 -0800
+++ server/mutex.c	2026-03-10 00:48:08.926439000 -0700
@@ -21,9 +21,11 @@
 #include "config.h"
 
 #include <assert.h>
+#include <errno.h>
 #include <stdio.h>
 #include <stdlib.h>
 #include <stdarg.h>
+#include <sys/ioctl.h>
 #include <sys/types.h>
 
 #include "ntstatus.h"
@@ -36,6 +38,18 @@
 #include "request.h"
 #include "security.h"
 
+/*
+ * FreeBSD ntsync helpers.  These call through to the kernel ntsync driver for
+ * inproc-backed mutex sync objects.  get_inproc_obj_fd() is provided by the
+ * FreeBSD ntsync wineserver module (server/inproc_sync.c).  It
+ * returns the raw unix fd for the given inproc sync object, or -1 if the
+ * object is not an inproc sync.  The fd is owned by the object; do not close.
+ */
+#ifdef __FreeBSD__
+# include <linux/ntsync.h>
+extern int get_inproc_obj_fd( struct object *obj );
+#endif /* __FreeBSD__ */
+
 static const WCHAR mutex_name[] = {'M','u','t','a','n','t'};
 
 struct type_descr mutex_type =
@@ -153,11 +167,19 @@
     mutex->abandoned = 0;
 }
 
-static struct object *create_mutex_sync( int owned )
+static struct object *create_mutex_sync( int owned, unsigned int owner_tid )
 {
     struct mutex_sync *mutex;
 
-    if (get_inproc_device_fd() >= 0) return (struct object *)create_inproc_mutex_sync( owned ? current->id : 0, owned ? 1 : 0 );
+    /* Pass owner_tid explicitly rather than always defaulting to current->id.
+     * current->id is correct for the NtCreateMutant handler (the requesting
+     * thread is the owner), but any call site that creates an initially-owned
+     * mutex on behalf of a different thread must supply that thread's NT TID.
+     * Storing the wrong tid causes every subsequent NTSYNC_IOC_MUTEX_UNLOCK
+     * from the real owner to return EPERM, cascading into the fallback path in
+     * DECL_HANDLER(release_mutex) which previously hit a fatal assertion. */
+    if (get_inproc_device_fd() >= 0)
+        return (struct object *)create_inproc_mutex_sync( owned ? owner_tid : 0, owned ? 1 : 0 );
 
     if (!(mutex = alloc_object( &mutex_sync_ops ))) return NULL;
     mutex->count = 0;
@@ -216,7 +238,8 @@
             /* initialize it if it didn't already exist */
             mutex->sync = NULL;
 
-            if (!(mutex->sync = create_mutex_sync( owned )))
+            /* current->id: the requesting thread IS the intended owner. */
+            if (!(mutex->sync = create_mutex_sync( owned, current->id )))
             {
                 release_object( mutex );
                 return NULL;
@@ -260,8 +283,28 @@
     struct mutex *mutex = (struct mutex *)obj;
     assert( obj->ops == &mutex_ops );
 
-    assert( mutex->sync->ops == &mutex_sync_ops ); /* never called with inproc syncs */
     assert( signal == -1 ); /* always called from signal_object */
+
+#ifdef __FreeBSD__
+    if (mutex->sync->ops != &mutex_sync_ops)
+    {
+        /* Inproc ntsync mutex.  NtSignalAndWaitForSingleObject releases the
+         * signal handle before blocking; push that release through the driver. */
+        if (!(access & SYNCHRONIZE))
+        {
+            set_error( STATUS_ACCESS_DENIED );
+            return 0;
+        }
+        struct ntsync_mutex_args args = { .owner = (unsigned int)current->id, .count = 0 };
+        int fd = get_inproc_obj_fd( mutex->sync );
+        if (fd < 0 || ioctl( fd, NTSYNC_IOC_MUTEX_UNLOCK, &args ) != 0)
+        {
+            set_error( errno == EPERM ? STATUS_MUTANT_NOT_OWNED : STATUS_UNSUCCESSFUL );
+            return 0;
+        }
+        return 1;
+    }
+#endif /* __FreeBSD__ */
 
     if (!(access & SYNCHRONIZE))
     {
@@ -319,11 +362,34 @@
     if ((mutex = (struct mutex *)get_handle_obj( current->process, req->handle,
                                                  0, &mutex_ops )))
     {
-        struct mutex_sync *sync = (struct mutex_sync *)mutex->sync;
-        assert( mutex->sync->ops == &mutex_sync_ops ); /* never called with inproc syncs */
-
-        reply->prev_count = sync->count;
-        do_release( sync, current, 1 );
+#ifdef __FreeBSD__
+        if (mutex->sync->ops != &mutex_sync_ops)
+        {
+            /* Inproc ntsync mutex: ntdll fell back to the server because
+             * NTSYNC_IOC_MUTEX_UNLOCK returned EPERM (owner mismatch).  Push
+             * the release through the driver from here using current->id,
+             * which is the NT TID of the ntdll thread that called
+             * NtReleaseMutant — i.e. the rightful owner.
+             *
+             * NTSYNC_IOC_MUTEX_UNLOCK is _IOWR: on success the kernel writes
+             * the atomically-captured previous count into args.count before
+             * returning.  Use that value directly — issuing a separate
+             * NTSYNC_IOC_MUTEX_READ first would introduce a TOCTOU race and
+             * an unnecessary extra ioctl round-trip. */
+            struct ntsync_mutex_args args = { .owner = (unsigned int)current->id, .count = 0 };
+            int fd = get_inproc_obj_fd( mutex->sync );
+            if (fd < 0 || ioctl( fd, NTSYNC_IOC_MUTEX_UNLOCK, &args ) != 0)
+                set_error( errno == EPERM ? STATUS_MUTANT_NOT_OWNED : STATUS_UNSUCCESSFUL );
+            else
+                reply->prev_count = args.count;
+        }
+        else
+#endif /* __FreeBSD__ */
+        {
+            struct mutex_sync *sync = (struct mutex_sync *)mutex->sync;
+            reply->prev_count = sync->count;
+            do_release( sync, current, 1 );
+        }
         release_object( mutex );
     }
 }
@@ -336,13 +402,39 @@
     if ((mutex = (struct mutex *)get_handle_obj( current->process, req->handle,
                                                  MUTANT_QUERY_STATE, &mutex_ops )))
     {
-        struct mutex_sync *sync = (struct mutex_sync *)mutex->sync;
-        assert( mutex->sync->ops == &mutex_sync_ops ); /* never called with inproc syncs */
-
-        reply->count = sync->count;
-        reply->owned = (sync->owner == current);
-        reply->abandoned = sync->abandoned;
-
+#ifdef __FreeBSD__
+        if (mutex->sync->ops != &mutex_sync_ops)
+        {
+            /* Inproc ntsync mutex: read state directly from the kernel object.
+             * Check fd first so errno is only inspected after a genuine failed
+             * ioctl, not after the get_inproc_obj_fd call itself. */
+            struct ntsync_mutex_args args = { 0 };
+            int fd = get_inproc_obj_fd( mutex->sync );
+            if (fd < 0)
+                set_error( STATUS_UNSUCCESSFUL );
+            else if (ioctl( fd, NTSYNC_IOC_MUTEX_READ, &args ) == 0)
+            {
+                reply->count     = args.count;
+                reply->owned     = (args.owner == (unsigned int)current->id);
+                reply->abandoned = 0;
+            }
+            else if (errno == EOWNERDEAD)
+            {
+                /* Owner thread died without releasing; kernel cleared owner. */
+                reply->count     = 0;
+                reply->owned     = 0;
+                reply->abandoned = 1;
+            }
+            else set_error( STATUS_UNSUCCESSFUL );
+        }
+        else
+#endif /* __FreeBSD__ */
+        {
+            struct mutex_sync *sync = (struct mutex_sync *)mutex->sync;
+            reply->count     = sync->count;
+            reply->owned     = (sync->owner == current);
+            reply->abandoned = sync->abandoned;
+        }
         release_object( mutex );
     }
 }
