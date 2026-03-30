--- dlls/ntdll/unix/unix_private.h.orig	2026-03-20 13:33:36.000000000 -0700
+++ dlls/ntdll/unix/unix_private.h	2026-03-27 21:53:28.137540000 -0700
@@ -274,6 +274,11 @@ extern NTSTATUS system_time_precise( void *args );
                                              data_size_t *ret_len );
 extern NTSTATUS system_time_precise( void *args );
 
+#if defined(__FreeBSD__) || defined(__FreeBSD_kernel__)
+extern NTSTATUS set_thread_syscall_dispatcher(void *start, size_t len, BYTE *selector);
+extern NTSTATUS clear_thread_syscall_dispatcher(void);
+#endif
+
 extern void *anon_mmap_fixed( void *start, size_t size, int prot, int flags );
 extern void *anon_mmap_alloc( size_t size, int prot );
 extern void virtual_init(void);
