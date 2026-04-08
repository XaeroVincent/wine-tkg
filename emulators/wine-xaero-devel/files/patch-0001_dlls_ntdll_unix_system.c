--- dlls/ntdll/unix/system.c.orig	2026-04-03 13:03:21.000000000 -0700
+++ dlls/ntdll/unix/system.c	2026-04-07 12:53:40.270824000 -0700
@@ -42,6 +42,36 @@
 #ifdef HAVE_SYS_SYSCTL_H
 # include <sys/sysctl.h>
 #endif
+
+#if defined(__FreeBSD__) || defined(__FreeBSD_kernel__)
+# include <sys/procctl.h>
+
+/* Fallback definitions for FreeBSD Syscall User Dispatch (SUD).
+ * These numeric values are only reached on a kernel carrying the
+ * FreeBSD-wine-SUD patch; on stock FreeBSD the procctl() call will
+ * return EINVAL (unknown command) and the runtime probe below will
+ * disable SUD gracefully without affecting Wine correctness. */
+#ifndef PROC_SUD_SET
+#define PROC_SUD_SET   25
+#define PROC_SUD_CLEAR 26
+
+#define SYSCALL_DISPATCH_FILTER_ALLOW 0
+#define SYSCALL_DISPATCH_FILTER_BLOCK 1
+
+struct proc_sud_ctl {
+    uintptr_t sud_start;
+    size_t    sud_len;
+    uint8_t  *sud_selector;
+};
+#endif
+
+/* Runtime flag: -1 = not yet probed, 0 = stock kernel (SUD absent),
+ *                1 = SUD kernel patch present and per-thread capable.
+ * Written once under a benign race: all racing threads probe identically
+ * and write the same value, so no mutex is needed. */
+static int fbsd_sud_kernel_available = -1;
+#endif
+
 #ifdef HAVE_SYS_UTSNAME_H
 # include <sys/utsname.h>
 #endif
@@ -1941,8 +1971,126 @@ static WORD append_smbios_boot_info( struct smbios_buf
     struct smbios_boot_info boot = { .hdr.type = SMBIOS_TYPE_BOOTINFO, .hdr.length = sizeof(boot) };
 
     return append_smbios( buf, &boot.hdr, NULL, 0 );
+}
+
+#if defined(__FreeBSD__) || defined(__FreeBSD_kernel__)
+
+/***********************************************************************
+ * probe_sud_kernel (internal)
+ *
+ * Called once to determine whether the running kernel carries the
+ * FreeBSD-wine-SUD patch.  We distinguish two EINVAL sources:
+ *
+ *   Stock FreeBSD:  procctl command 25 is unknown → EINVAL
+ *   SUD kernel:     command 25 is known but NULL arg is rejected → EFAULT
+ *
+ * Any result other than EFAULT is treated as "SUD not available" so
+ * that unknown future error codes also fall back safely.
+ *
+ * NOTE: On stock FreeBSD, procctl(P_PID, getpid()) for PROC_SUD_SET
+ * would be process-wide if the command existed.  The SUD kernel patch
+ * overrides this to be per-calling-thread.  This function must be
+ * called before any real PROC_SUD_SET attempt to guarantee we never
+ * accidentally apply a process-wide selector stomp on stock kernels.
+ */
+static int probe_sud_kernel(void)
+{
+    int available;
+
+    /* Pass NULL so the kernel rejects the args (EFAULT) rather than
+     * actually installing anything.  We only care whether the command
+     * is recognised at all, not whether it succeeds. */
+    errno = 0;
+    procctl(P_PID, getpid(), PROC_SUD_SET, NULL);
+    available = (errno == EFAULT) ? 1 : 0;
+
+    TRACE_(ntdll)("FreeBSD SUD kernel support: %s\n", available ? "yes" : "no");
+    __atomic_store_n(&fbsd_sud_kernel_available, available, __ATOMIC_RELEASE);
+    return available;
+}
+
+/* Return non-zero if the SUD kernel patch is present, probing on first call. */
+static inline int sud_kernel_available(void)
+{
+    int val = __atomic_load_n(&fbsd_sud_kernel_available, __ATOMIC_ACQUIRE);
+    if (__builtin_expect(val < 0, 0))
+        val = probe_sud_kernel();
+    return val;
 }
 
+/***********************************************************************
+ * set_thread_syscall_dispatcher
+ *
+ * Registers the syscall dispatcher allow-range for the calling thread,
+ * but only when running on the FreeBSD-wine-SUD kernel.  On a stock
+ * FreeBSD kernel this is a safe no-op: Wine falls back to the normal
+ * (non-SUD) syscall path and all applications continue to work.
+ */
+NTSTATUS set_thread_syscall_dispatcher(void *dispatcher_start, size_t dispatcher_len, BYTE *selector)
+{
+    struct proc_sud_ctl ctl = { 0 };
+
+    if (!dispatcher_start || !dispatcher_len || !selector)
+        return STATUS_INVALID_PARAMETER;
+
+    /* Bail out silently on stock kernels: SUD simply won't be active and
+     * Wine operates in its normal non-intercepted syscall mode. */
+    if (!sud_kernel_available())
+        return STATUS_SUCCESS;
+
+    /* On the SUD kernel, PROC_SUD_SET with P_PID/getpid() is per-thread
+     * (the kernel patch scopes it to td_tid, not td_proc).  This is safe
+     * because sud_kernel_available() guarantees we only reach here on that
+     * patched kernel, never on stock FreeBSD where the call would be
+     * process-wide and would stomp every other thread's selector pointer. */
+    ctl.sud_start    = (uintptr_t)dispatcher_start;
+    ctl.sud_len      = dispatcher_len;
+    ctl.sud_selector = (uint8_t *)selector;
+
+    if (procctl(P_PID, getpid(), PROC_SUD_SET, &ctl) == -1)
+    {
+        switch (errno)
+        {
+        case ENOMEM:
+            return STATUS_NO_MEMORY;
+        case EINVAL:
+            /* Genuine bad-argument rejection from the SUD kernel (not the
+             * "unknown command" EINVAL that probe_sud_kernel already filtered
+             * out).  The dispatcher range or selector pointer is invalid. */
+            return STATUS_INVALID_PARAMETER;
+        case EPERM:
+            return STATUS_ACCESS_DENIED;
+        default:
+            return STATUS_UNSUCCESSFUL;
+        }
+    }
+
+    return STATUS_SUCCESS;
+}
+
+/***********************************************************************
+ * clear_thread_syscall_dispatcher
+ *
+ * Disables SUD for the calling thread.  No-op on stock FreeBSD kernels.
+ */
+NTSTATUS clear_thread_syscall_dispatcher(void)
+{
+    if (!sud_kernel_available())
+        return STATUS_SUCCESS;
+
+    /* Per-thread on the SUD kernel for the same reason as PROC_SUD_SET. */
+    if (procctl(P_PID, getpid(), PROC_SUD_CLEAR, NULL) == -1)
+    {
+        if (errno == EPERM)
+            return STATUS_ACCESS_DENIED;
+        return STATUS_UNSUCCESSFUL;
+    }
+
+    return STATUS_SUCCESS;
+}
+
+#endif
+
 #ifdef __aarch64__
 #ifdef linux
 
