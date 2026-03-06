--- dlls/ntdll/unix/sync.c.orig	2026-03-05 04:16:35.322227378 +0000
+++ dlls/ntdll/unix/sync.c	2026-03-05 04:16:35.323227378 +0000
@@ -2335,6 +2335,21 @@
     select_op.wait.op = type == WaitAll ? SELECT_WAIT_ALL : SELECT_WAIT;
     for (i = 0; i < count; i++) select_op.wait.handles[i] = wine_server_obj_handle( handles[i] );
     ret = server_wait( &select_op, offsetof( union select_op, wait.handles[count] ), flags, timeout );
+    /* STATUS_OBJECT_TYPE_MISMATCH from the server select reply is not a real
+     * error.  It means every handle in this wait has an inproc ntsync fd and
+     * the wineserver cannot perform the kernel wait itself.  By this point
+     * get_inproc_sync (called inside the first inproc_wait attempt above) has
+     * already populated the fd cache, so a second call to inproc_wait will
+     * hit the cache and dispatch NTSYNC_IOC_WAIT_ANY/ALL without a server
+     * round-trip.  This path is taken on FreeBSD when the first inproc_wait
+     * call returned STATUS_NOT_IMPLEMENTED (e.g. inproc_device_fd not yet
+     * open), but the wineserver has since sent the fd via get_inproc_sync_fd
+     * in the course of handling the select request. */
+    if (ret == STATUS_OBJECT_TYPE_MISMATCH)
+    {
+        if ((ret = inproc_wait( count, handles, type, alertable, timeout )) == STATUS_NOT_IMPLEMENTED)
+            ret = STATUS_OBJECT_TYPE_MISMATCH;
+    }
     TRACE( "-> %#x\n", ret );
     return ret;
 }
@@ -2361,6 +2376,21 @@
     select_op.wait.op = SELECT_WAIT;
     select_op.wait.handles[0] = wine_server_obj_handle( handle );
     ret = server_wait( &select_op, offsetof( union select_op, wait.handles[1] ), flags, timeout );
+    /* STATUS_OBJECT_TYPE_MISMATCH means the wineserver is telling ntdll to
+     * handle this wait itself via the inproc ntsync fd.  The fd is already in
+     * the cache from the get_inproc_sync_fd exchange the wineserver performed
+     * while processing the select request.  Re-enter inproc_wait so that
+     * linux_wait_objs issues NTSYNC_IOC_WAIT_ANY against that fd directly.
+     *
+     * Root cause seen in wineboot log:
+     *   get_inproc_sync_fd(handle=0010) = 0 {type=1, fd=54}
+     *   select() = OBJECT_TYPE_MISMATCH {signaled=1}
+     *   NtWaitForSingleObject -> 0xc0000024   <- wrong; must become STATUS_WAIT_0 */
+    if (ret == STATUS_OBJECT_TYPE_MISMATCH)
+    {
+        if ((ret = inproc_wait( 1, &handle, WaitAny, alertable, timeout )) == STATUS_NOT_IMPLEMENTED)
+            ret = STATUS_OBJECT_TYPE_MISMATCH;
+    }
     TRACE( "-> %#x\n", ret );
     return ret;
 }
