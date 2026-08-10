--- server/inproc_sync.c.orig	2026-03-05 04:43:32.062981335 +0000
+++ server/inproc_sync.c	2026-03-05 04:43:32.062981335 +0000
@@ -240,6 +240,15 @@
     return fd;
 }
 
+/* Return the raw kernel fd for any object whose sync is an inproc ntsync
+ * object, or -1 if it is not.  Used by server/mutex.c to push ioctl calls
+ * through the driver when ntdll falls back to the wineserver release path. */
+int get_inproc_obj_fd( struct object *obj )
+{
+    int type;
+    return get_obj_inproc_sync( obj, &type );
+}
+
 #else /* NTSYNC_IOC_EVENT_READ */
 
 int get_inproc_device_fd(void)
@@ -288,6 +297,11 @@
 {
     return -1;
 }
+
+int get_inproc_obj_fd( struct object *obj )
+{
+    return -1;
+}
 
 #endif /* NTSYNC_IOC_EVENT_READ */
 
