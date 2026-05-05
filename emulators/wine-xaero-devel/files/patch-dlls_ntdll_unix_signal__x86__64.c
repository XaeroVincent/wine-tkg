--- dlls/ntdll/unix/signal_x86_64.c.orig	2026-05-01 13:41:27.000000000 -0700
+++ dlls/ntdll/unix/signal_x86_64.c	2026-05-04 18:22:57.670872000 -0700
@@ -203,6 +203,9 @@ __ASM_GLOBAL_FUNC( modify_ldt,
 
 #elif defined(__FreeBSD__) || defined (__FreeBSD_kernel__)
 
+#include <machine/cpufunc.h>
+#include <machine/segments.h>
+#include <machine/specialreg.h>
 #include <machine/trap.h>
 
 #define RAX_sig(context)     ((context)->uc_mcontext.mc_rax)
@@ -232,7 +235,7 @@ __ASM_GLOBAL_FUNC( modify_ldt,
 #define TRAP_sig(context)    ((context)->uc_mcontext.mc_trapno)
 #define ERROR_sig(context)   ((context)->uc_mcontext.mc_err)
 #define FPU_sig(context)     ((void *)((context)->uc_mcontext.mc_fpstate))
-#define XState_sig(context)  NULL
+#define XState_sig(context)  ((context)->uc_mcontext.mc_xfpustate ? (XSAVE_AREA_HEADER *)((XMM_SAVE_AREA32 *)(uintptr_t)(context)->uc_mcontext.mc_xfpustate + 1) : NULL)
 
 #elif defined(__NetBSD__)
 
@@ -535,7 +538,110 @@ static LONG syscall_dispatch_enabled = TRUE;
 static UINT64 xstate_extended_features;
 static LONG syscall_dispatch_enabled = TRUE;
 
-#if defined(__linux__) || defined(__APPLE__)
+#if defined(__linux__) || defined(__APPLE__) || defined(__FreeBSD__)
+
+#ifdef __FreeBSD__
+#ifndef SIGSYS_DISPATCH
+#define SIGSYS_DISPATCH 1
+#endif
+
+/* Syscall Translation for FreeBSD SUD (Win10 22H2 - Build 19045) */
+static struct
+{
+    unsigned int win_syscall_nr;
+    unsigned int wine_syscall_nr;
+    void *function;
+}
+fbsd_syscall_nr_translation[] =
+{
+    /* Core Process & Thread Information */
+    {0x10, ~0u, NtQueryObject},
+    {0x19, ~0u, NtQueryInformationProcess},
+    {0x1c, ~0u, NtSetInformationProcess},
+    {0x25, ~0u, NtQueryInformationThread},
+    {0x0d, ~0u, NtSetInformationThread},
+    {0x36, ~0u, NtQuerySystemInformation},
+    {0x0f3, ~0u, NtGetContextThread},
+
+    /* Basic File & Memory I/O */
+    {0x33, ~0u, NtOpenFile},
+    {0x55, ~0u, NtCreateFile},
+    {0x4a, ~0u, NtCreateSection},
+    {0x28, ~0u, NtMapViewOfSection},
+    {0x2a, ~0u, NtUnmapViewOfSection},
+    {0x08, ~0u, NtWriteFile},
+    {0x06, ~0u, NtReadFile},
+    {0x0f, ~0u, NtClose},
+    {0x23, ~0u, NtQueryVirtualMemory},
+    {0x50, ~0u, NtProtectVirtualMemory},
+    {0x0a6, ~0u, NtCreateDebugObject},
+
+    /* Chromium IPC & Named Pipes Base */
+    {0x39, ~0u, NtFsControlFile},
+    {0x07, ~0u, NtDeviceIoControlFile},
+    {0x11, ~0u, NtQueryInformationFile},
+    {0x27, ~0u, NtSetInformationFile},
+    {0x04, ~0u, NtWaitForSingleObject},
+    {0x3c, ~0u, NtDuplicateObject},
+    /* NtWaitForMultipleObjects: 0x5b verified. NtQuerySystemTime stub starts with
+     * JMP (E9); its 0x5b pattern is dead code. Real owner is NtWaitForMultipleObjects. */
+    {0x05b, ~0u, NtWaitForMultipleObjects},
+
+    /* Pipeline Creation and Asynchronous I/O Cancellation */
+    {0x0b5, ~0u, NtCreateNamedPipeFile},
+    {0x0b3, ~0u, NtCreateMailslotFile},
+    {0x05d, ~0u, NtCancelIoFile},
+    {0x092, ~0u, NtCancelIoFileEx},
+
+    /* Foolproofing for pipe path verification and thread sleeping */
+    {0x3d, ~0u, NtQueryAttributesFile},
+    {0x34, ~0u, NtDelayExecution},
+
+    /* Memory Manipulation (Denuvo / VM Unpacking) */
+    {0x18, ~0u, NtAllocateVirtualMemory},
+    {0x1e, ~0u, NtFreeVirtualMemory},
+    {0x3f, ~0u, NtReadVirtualMemory},
+    {0x3a, ~0u, NtWriteVirtualMemory},
+
+    /* Thread State Manipulation & Hardware Breakpoints */
+    {0x18d, ~0u, NtSetContextThread},
+    {0x52, ~0u, NtResumeThread},
+    {0x1be, ~0u, NtSuspendThread},
+
+    /* Execution Timing and Evasion */
+    {0x046, ~0u, NtYieldExecution},
+
+    /* Anti-Debug & Process Monitoring */
+    {0x26, ~0u, NtOpenProcess},
+    {0x12f, ~0u, NtOpenThread},
+    {0x2c, ~0u, NtTerminateProcess},
+    {0x0f9, ~0u, NtGetNextThread},
+    {0xc2, ~0u, NtCreateThreadEx},
+    {0x0c9, ~0u, NtCreateUserProcess},
+    {0x1b8, ~0u, NtSignalAndWaitForSingleObject},
+
+    /* Timing Attacks & System Info */
+    {0x31, ~0u, NtQueryPerformanceCounter},
+    {0x163, ~0u, NtQueryTimerResolution},
+    {0x49, ~0u, NtQueryVolumeInformationFile},
+
+    /* Code Integrity & Unpacking */
+    {0x0e9, ~0u, NtFlushInstructionCache},
+    {0x51, ~0u, NtQuerySection},
+
+    /* Registry & Environment Sniffing */
+    {0x12, ~0u, NtOpenKey},
+    {0x017, ~0u, NtQueryValueKey},
+
+    /* Advanced Hijacking / Process Hollowing */
+    {0x45, ~0u, NtQueueApcThread},
+    {0x06e, ~0u, NtAlertResumeThread},
+    {0x06f, ~0u, NtAlertThread},
+};
+
+static int fbsd_sud_translate_syscalls = 0;
+#endif
+
 static inline struct thread_data *get_current_thread_data(void)
 {
     unsigned long rsp;
@@ -806,6 +912,11 @@ static inline struct thread_data *init_handler( void *
         if (CS_sig((ucontext_t *)sigcontext) == 0x07 /* SYSCALL_CS */)
             CS_sig((ucontext_t *)sigcontext) = cs64_sel;
     }
+#elif defined(__FreeBSD__) || defined(__FreeBSD_kernel__)
+    {
+        struct amd64_thread_data *thread_data = amd64_thread_data( data );
+        thread_data->syscall_dispatch = 0; /* SYSCALL_DISPATCH_FILTER_ALLOW */
+    }
 #endif
     return data;
 }
@@ -824,12 +935,28 @@ static inline void leave_handler( struct thread_data *
         thread_data->syscall_dispatch = 1;  /* SYSCALL_DISPATCH_FILTER_BLOCK */
         if (fs32_sel) __asm__ volatile( "movw %0,%%fs" :: "r" (fs32_sel) );
     }
+
 #elif defined __APPLE__
     if (!is_inside_signal_stack( data, (void *)RSP_sig(sigcontext )) &&
         !is_inside_syscall( data, RSP_sig(sigcontext )))
         _thread_set_tsd_base( (uint64_t)data->teb );
+
+#elif defined(__FreeBSD__) || defined(__FreeBSD_kernel__)
+    if (!is_inside_signal_stack( data, (void *)RSP_sig(sigcontext )) &&
+        !is_inside_syscall( data, RSP_sig(sigcontext )))
+    {
+        struct amd64_thread_data *thread_data = amd64_thread_data( data );
+
+        /* Restore FS selector if needed (FreeBSD does not use arch_prctl like Linux) */
+        if (thread_data->fs)
+        {
+            __asm__ volatile( "movw %w0,%%fs" :: "r" ((USHORT)thread_data->fs) );
+        }
+    }
 #endif
+
     if (is_16bit( sigcontext )) return;
+
 #ifdef DS_sig
     DS_sig(sigcontext) = ds64_sel;
 #else
@@ -892,7 +1019,7 @@ static void save_context( struct thread_data *data, st
         context->ContextFlags |= CONTEXT_FLOATING_POINT;
         memcpy( &context->FltSave, FPU_sig(sigcontext), sizeof(context->FltSave) );
         context->MxCsr = context->FltSave.MxCsr;
-        if (xstate_extended_features && (xs = XState_sig(FPU_sig(sigcontext))))
+        if (xstate_extended_features && (xs = XState_sig(sigcontext)))
         {
             /* xcontext and sigcontext are both on the signal stack, so we can
              * just reference sigcontext without overflowing 32 bit XState.Offset */
@@ -930,7 +1057,10 @@ static void fixup_frame_fpu_state( struct syscall_fram
     if (user_shared_data->XState.CompactionEnabled)
         frame->xstate.CompactionMask = 0x8000000000000000 | user_shared_data->XState.EnabledFeatures;
 
+#if !defined(__FreeBSD__) && !defined(__FreeBSD_kernel__) && !defined(__NetBSD__)
     if (!FPU_sig(sigcontext)) return;
+#endif
+
     memcpy( &xsave, FPU_sig(sigcontext), sizeof(xsave) );
     memcpy( &xsave.XmmRegisters[6], &frame->xsave.XmmRegisters[6], 10 * sizeof(*xsave.XmmRegisters) );
     xsave.MxCsr = frame->xsave.MxCsr;
@@ -1681,7 +1811,7 @@ __ASM_GLOBAL_FUNC( call_user_mode_callback,
                    "1:\tmovq %rdi,%rsp\n\t"    /* user_rsp */
                    "movq 0x98(%r14),%rbp\n\t"  /* prev_frame->rbp */
                    "ldmxcsr 0xd8(%r14)\n\t"    /* prev_frame->xsave.MxCsr */
-#ifdef __linux__
+#if defined(__linux__) || defined(__FreeBSD__) || defined(__FreeBSD_kernel__)
                    "movb $1,0x340(%r13)\n\t"   /* amd64_thread_data()->syscall_dispatch */
                    "movw 0x338(%r13),%ax\n"    /* amd64_thread_data()->fs */
                    "testw %ax,%ax\n\t"
@@ -1716,6 +1846,9 @@ __ASM_GLOBAL_FUNC( user_mode_callback_return,
 extern void DECLSPEC_NORETURN user_mode_callback_return( void *ret_ptr, ULONG ret_len,
                                                          NTSTATUS status, TEB *teb );
 __ASM_GLOBAL_FUNC( user_mode_callback_return,
+#if defined(__FreeBSD__) || defined(__FreeBSD_kernel__)
+                   "movb $0,0x340(%rcx)\n\t"       /* SUD: ALLOW (0) native Unix syscalls */
+#endif
                    "movq 0x378(%rcx),%r10\n\t" /* thread_data->syscall_frame */
                    "movq 0xa0(%r10),%r11\n\t"  /* frame->prev_frame */
                    "movq %r11,0x378(%rcx)\n\t" /* syscall_frame = prev_frame */
@@ -2576,12 +2709,11 @@ static void usr1_handler( int signal, siginfo_t *sigin
 }
 
 
-#if defined(__APPLE__) || defined(__linux__)
+#if defined(__APPLE__) || defined(__linux__) || defined(__FreeBSD__) || defined(__FreeBSD_kernel__)
 /**********************************************************************
  *		sigsys_handler
  *
  * Handler for SIGSYS, signals that a non-existent system call was invoked.
- * On Mac, this is only called on macOS 14 Sonoma and later.
  */
 static void sigsys_handler( int signal, siginfo_t *siginfo, void *_sigcontext )
 {
@@ -2589,9 +2721,36 @@ static void sigsys_handler( int signal, siginfo_t *sig
     ucontext_t *sigcontext = _sigcontext;
     struct thread_data *data = init_handler( sigcontext );
     struct syscall_frame *frame = get_syscall_frame( data );
+#if defined(__FreeBSD__) || defined(__FreeBSD_kernel__)
+    /* Arrived in BLOCK state (SUD intercepted Windows PE syscall stub).
+     * Set ALLOW immediately so any syscall in C code below is not re-intercepted,
+     * then restore pthread fsbase before any TLS-dependent code runs. */
+    {
+        struct amd64_thread_data *_amd64 =
+            (struct amd64_thread_data *)&NtCurrentTeb()->GdiTebBatch;
+        _amd64->syscall_dispatch = 0; /* SYSCALL_DISPATCH_FILTER_ALLOW */
+        if (_amd64->pthread_teb)
+        {
+            if (user_shared_data->ProcessorFeatures[PF_RDWRFSGSBASE_AVAILABLE])
+                __asm__ volatile ("wrfsbase %0" :: "r" (_amd64->pthread_teb));
+            else
+            {
+                void *_fsbase = _amd64->pthread_teb;
+                __asm__ volatile (
+                    "movq %0, %%rsi\n\t"
+                    "movq $0xa5, %%rax\n\t"
+                    "movq $0x81, %%rdi\n\t"
+                    "syscall\n\t"
+                    : : "r" (&_fsbase)
+                    : "rax","rcx","rdx","rsi","rdi","r8","r9","r10","r11","memory");
+            }
+        }
+    }
+#endif
 
-    TRACE_(seh)("SIGSYS, rax %#lx, rip %#lx.\n", (long)RAX_sig(sigcontext), (long)RIP_sig(sigcontext));
+   TRACE_(seh)("SIGSYS, rax %#lx, rip %#lx.\n", (unsigned long)RAX_sig(sigcontext),(unsigned long)RIP_sig(sigcontext));
 
+
 #ifdef __linux__
     if (!syscall_dispatch_enabled)
     {
@@ -2601,8 +2760,49 @@ static void sigsys_handler( int signal, siginfo_t *sig
     }
 #endif
 
+#ifdef __FreeBSD__
+    if (siginfo->si_code == SIGSYS_DISPATCH)
+    {
+        ULONG64 syscall_nr = siginfo->si_syscall;
+        if (fbsd_sud_translate_syscalls)
+        {
+            unsigned int i;
+            for (i = 0; i < ARRAY_SIZE(fbsd_syscall_nr_translation); ++i)
+            {
+                if (syscall_nr == fbsd_syscall_nr_translation[i].win_syscall_nr)
+                {
+                    /* Only substitute if the Wine number was resolved at init time.
+                     * If wine_syscall_nr is still ~0u, the function was not found in
+                     * KeServiceDescriptorTable (unimplemented stub, missing entry, etc).
+                     * Keep the original Windows number so Wine's dispatcher rejects
+                     * it cleanly via its normal unknown-syscall path, rather than
+                     * using 0xFFFFFFFF as an index off the end of ServiceTable. */
+                    if (fbsd_syscall_nr_translation[i].wine_syscall_nr != ~0u)
+                        syscall_nr = fbsd_syscall_nr_translation[i].wine_syscall_nr;
+                    break;
+                }
+            }
+        }
+        RAX_sig(sigcontext) = syscall_nr;
+
+        /* FreeBSD's kernel moves R10 to RCX on syscall entry. Because SUD
+         * interrupts this, the first argument is stuck in RCX. Restore it! */
+        R10_sig(sigcontext) = RCX_sig(sigcontext);
+
+        frame->r10 = R10_sig(sigcontext);
+        frame->r8  = R8_sig(sigcontext);
+        frame->r9  = R9_sig(sigcontext);
+        frame->rdx = RDX_sig(sigcontext);
+    }
+    else
+    {
+        leave_handler( data, sigcontext );
+        return;
+    }
+#endif
+
     frame->rip = RIP_sig(sigcontext) + 0xb;
-    frame->rcx = RIP_sig(sigcontext);
+    frame->rcx = RIP_sig(sigcontext) + 0xa; /* Windows stub: syscall insn ends at byte 10 */
     frame->eflags = EFL_sig(sigcontext);
     frame->restore_flags = 0;
     if (instrumentation_callback) frame->restore_flags |= RESTORE_FLAGS_INSTRUMENTATION;
@@ -2638,6 +2838,16 @@ void ldt_set_entry( WORD sel, LDT_ENTRY entry )
     if ((ret = modify_ldt( &ldt_info ))) ERR( "modify_ldt failed %d\n", ret );
 #elif defined(__APPLE__)
     if (i386_set_ldt(sel >> 3, (union ldt_entry *)&entry, 1) < 0) perror("i386_set_ldt");
+#elif defined(__FreeBSD__)
+    struct i386_ldt_args p;
+    p.start = sel >> 3;
+    p.descs = (struct user_segment_descriptor *)&entry;
+    p.num   = 1;
+    if (sysarch(I386_SET_LDT, &p) == -1)
+    {
+        perror("i386_set_ldt");
+        exit(1);
+    }
 #else
     fprintf( stderr, "No LDT support on this platform\n" );
     exit(1);
@@ -2752,6 +2962,94 @@ static int libc_addr_cb( struct dl_phdr_info *info, si
 }
 #endif
 
+#ifdef __FreeBSD__
+static __siginfohandler_t *libthr_signal_handlers[_SIG_MAXSIG];
+
+/* occasionally signals happen right between %fs reset to GUFS32_SEL and fsbase correction,
+   which results in fsbase being incorrect on handler entry; restore fsbase ourselves */
+#if defined(__GNUC__) || defined(__clang__)
+__attribute__((no_stack_protector))
+#endif
+static void libthr_sighandler_wrapper(int sig, siginfo_t *info, void *_ucp)
+{
+    struct amd64_thread_data *thread_data = (struct amd64_thread_data *)&NtCurrentTeb()->GdiTebBatch;
+    void *pthread_teb = thread_data->pthread_teb;
+    ucontext_t *uc = (ucontext_t *)_ucp;
+
+    if (pthread_teb)
+    {
+        /* Restore fsbase for the duration of this handler using the fastest available
+         * mechanism.  Avoid any libc call that might rely on a valid fsbase itself. */
+        if (user_shared_data->ProcessorFeatures[PF_RDWRFSGSBASE_AVAILABLE])
+            __asm__ volatile ("wrfsbase %0" :: "r" (pthread_teb));
+        else
+        {
+            /* On CPUs without FSGSBASE, sysarch(AMD64_SET_FSBASE) requires a raw
+             * syscall instruction.  If SUD is in BLOCK state when this signal
+             * arrived (thread was in Windows user mode), SUD will intercept the
+             * sysarch syscall and deliver SIGSYS.  The SIGSYS handler would then
+             * misinterpret this as a redirected Windows syscall and corrupt RIP
+             * by advancing it +0xb bytes into the middle of our inline asm.
+             *
+             * Fix: save the current selector, force ALLOW for the duration of
+             * the sysarch call, then restore exactly what was there before so
+             * the caller's SUD state is preserved on return. */
+            void *fsbase_val;
+            uint8_t saved_dispatch = thread_data->syscall_dispatch;
+            thread_data->syscall_dispatch = 0; /* ALLOW: let sysarch through SUD */
+            fsbase_val = pthread_teb;
+            __asm__ volatile (
+                "movq %0, %%rsi\n\t"         /* Manually load into %rsi to allow clobbering */
+                "movq $0xa5, %%rax\n\t"      /* SYS_sysarch */
+                "movq $0x81, %%rdi\n\t"      /* AMD64_SET_FSBASE */
+                "syscall\n\t"
+                :
+                : "r" (&fsbase_val)
+                : "rax", "rcx", "rdx", "rsi", "rdi", "r8", "r9", "r10", "r11", "memory"
+            );
+            thread_data->syscall_dispatch = saved_dispatch; /* restore BLOCK/ALLOW */
+        }
+
+        /* Also fix mc_fsbase in the saved signal context.  Without this, the kernel
+         * restores the stale (broken) fsbase on sigreturn, undoing our fix above and
+         * leaving the thread with a corrupt fsbase until the next syscall entry. */
+        uc->uc_mcontext.mc_fsbase = (register_t)pthread_teb;
+    }
+
+    libthr_signal_handlers[sig - 1](sig, info, _ucp);
+}
+
+extern int __sys_sigaction(int, const struct sigaction * restrict, struct sigaction * restrict);
+
+static int wrap_libthr_signal_handlers(void)
+{
+    struct sigaction act;
+    int sig;
+
+    for (sig = 1; sig <= _SIG_MAXSIG; sig++)
+    {
+        if (__sys_sigaction(sig, NULL, &act) == -1) return -1;
+        if ((act.sa_flags & SA_SIGINFO) && act.sa_sigaction != NULL
+            && act.sa_sigaction != libthr_sighandler_wrapper)
+        {
+            libthr_signal_handlers[sig - 1] = act.sa_sigaction;
+            act.sa_sigaction = libthr_sighandler_wrapper;
+            /* Strip SA_RESETHAND: if libthr installed a one-shot handler and
+             * the kernel de-registers it on first delivery, our wrapper slot
+             * in libthr_signal_handlers would become a dangling pointer that
+             * fires again on the next delivery of this signal with no handler.
+             * Clearing the flag keeps the wrapper resident so it can forward
+             * correctly every time.  The one-shot semantic is gone, but libthr
+             * never uses SA_RESETHAND for its own internal signals in practice. */
+            act.sa_flags &= ~SA_RESETHAND;
+            if (__sys_sigaction(sig, &act, NULL) == -1) return -1;
+        }
+    }
+
+    return 0;
+}
+#endif
+
 /**********************************************************************
  *		signal_init_process
  */
@@ -2780,6 +3078,11 @@ void signal_init_process(void)
         fs32_sel = alloc_fs_sel( -1, wow_teb );
 #elif defined(__APPLE__)
         cs32_sel = ldt_alloc_entry( ldt_make_cs32_entry() );
+#elif defined(__FreeBSD__)
+        /* GSEL(GUCODE32_SEL, SEL_UPL) = 0x23.  Per-thread LDT fs allocation is
+         * handled by signal_alloc_thread(); fsbase detection is done inline in
+         * the ASM dispatchers via user_shared_data->ProcessorFeatures. */
+        cs32_sel = GSEL(GUCODE32_SEL, SEL_UPL);
 #endif
     }
 
@@ -2813,9 +3116,32 @@ void signal_init_process(void)
     if (sigaction( SIGSEGV, &sig_act, NULL ) == -1) goto error;
     if (sigaction( SIGILL, &sig_act, NULL ) == -1) goto error;
     if (sigaction( SIGBUS, &sig_act, NULL ) == -1) goto error;
-#if defined(__APPLE__) || defined(__linux__)
+#if defined(__APPLE__) || defined(__linux__) || defined(__FreeBSD__) || defined(__FreeBSD_kernel__)
     sig_act.sa_sigaction = sigsys_handler;
     if (sigaction( SIGSYS, &sig_act, NULL ) == -1) goto error;
+#endif
+#ifdef __FreeBSD__
+    if (wrap_libthr_signal_handlers() == -1) goto error;
+
+
+    {
+        const char *sgi = getenv("SteamGameId");
+        if (sgi && (!strcmp(sgi, "1174180") || !strcmp(sgi, "1404210") || !strcmp(sgi, "1418100") || !strcmp(sgi, "2767030")
+                   || !strcmp(sgi, "2853730") || !strcmp( sgi, "298110" )))
+        {
+            unsigned int i, j;
+            fbsd_sud_translate_syscalls = 1;
+            for (i = 0; i < KeServiceDescriptorTable->ServiceLimit; ++i)
+            {
+                for (j = 0; j < ARRAY_SIZE(fbsd_syscall_nr_translation); ++j)
+                    if ((void *)KeServiceDescriptorTable->ServiceTable[i] == fbsd_syscall_nr_translation[j].function)
+                    {
+                        fbsd_syscall_nr_translation[j].wine_syscall_nr = i;
+                        break;
+                    }
+            }
+        }
+    }
 #endif
     return;
 
@@ -2852,6 +3178,13 @@ void init_syscall_frame( LPTHREAD_START_ROUTINE entry,
         WARN_(seh)( "could not enable syscall user dispatch\n" );
 #elif defined (__FreeBSD__) || defined (__FreeBSD_kernel__)
     amd64_set_gsbase( teb );
+    amd64_get_fsbase(&thread_data->pthread_teb);
+    {
+        extern void __wine_syscall_dispatcher_return(void);
+        size_t dispatcher_len = (const char *)__wine_syscall_dispatcher_return - (const char *)__wine_syscall_dispatcher;
+        thread_data->syscall_dispatch = 0; /* SYSCALL_DISPATCH_FILTER_ALLOW */
+        set_thread_syscall_dispatcher((void *)__wine_syscall_dispatcher, dispatcher_len, (BYTE *)&thread_data->syscall_dispatch);
+    }
 #elif defined(__NetBSD__)
     sysarch( X86_64_SET_GSBASE, &teb );
 #elif defined (__APPLE__)
@@ -2974,6 +3307,9 @@ __ASM_GLOBAL_FUNC( __wine_syscall_dispatcher,
                    __ASM_CFI(".cfi_adjust_cfa_offset -8\n\t")
                    "movl $0,0xb4(%rcx)\n\t"        /* frame->restore_flags */
                    __ASM_LOCAL_LABEL("__wine_syscall_dispatcher_prolog_end") ":\n\t"
+#if defined(__FreeBSD__) || defined(__FreeBSD_kernel__)
+                   "movb $0,%gs:0x340\n\t"       /* SUD: ALLOW (0) native Unix syscalls */
+#endif
                    "movq %rbx,0x08(%rcx)\n\t"
                    __ASM_CFI_REG_IS_AT1(rbx, rcx, 0x08)
                    "movq %rdx,0x18(%rcx)\n\t"
@@ -3070,8 +3406,25 @@ __ASM_GLOBAL_FUNC( __wine_syscall_dispatcher,
                    "movq 0x320(%r13),%rdi\n\t"     /* amd64_thread_data()->pthread_teb */
                    "xorl %esi,%esi\n\t"
                    "movl $0x3000003,%eax\n\t"      /* _thread_set_tsd_base */
+                   "syscall\n\t"
+                   "leaq -0x98(%rbp),%rcx\n"
+#elif defined(__FreeBSD__)
+                   /* Restore pthread fsbase and WOW32 %%fs selector on syscall entry. */
+                   "movw 0x338(%r13),%ax\n\t"      /* amd64_thread_data()->fs */
+                   "testw %ax,%ax\n\t"
+                   "jz 2f\n\t"
+                   "movq 0x320(%r13),%rsi\n\t"     /* amd64_thread_data()->pthread_teb */
+                   "cmpb $0,0x7ffe028a\n\t"        /* user_shared_data->ProcessorFeatures[PF_RDWRFSGSBASE_AVAILABLE] */
+                   "jz 1f\n\t"
+                   "wrfsbase %rsi\n\t"
+                   "jmp 2f\n"
+                   "1:\n\t"
+                   "leaq 0x320(%r13),%rsi\n\t"     /* sysarch requires a pointer to the value */
+                   "movq $0xa5,%rax\n\t"           /* sysarch */
+                   "movq $0x81,%rdi\n\t"           /* AMD64_SET_FSBASE */
                    "syscall\n\t"
                    "leaq -0x98(%rbp),%rcx\n"
+                   "2:\n\t"
 #endif
                    "ldmxcsr 0x33c(%r13)\n\t"       /* amd64_thread_data()->mxcsr */
                    "movl 0xb0(%rcx),%eax\n\t"      /* frame->syscall_id */
@@ -3128,6 +3481,21 @@ __ASM_GLOBAL_FUNC( __wine_syscall_dispatcher,
                    "syscall\n\t"
                    "movq %rdx,%rcx\n\t"
                    "movq %r8,%rax\n\t"
+#elif defined(__FreeBSD__)
+                   /* Restore WOW32 %%fs on syscall return and fix %%ss after sysret.
+                    * AMD CPUs leave %%ss in a speculative state after sysret that
+                    * causes a #GP on the next stack access if not corrected here.
+                    * %%edx is reloaded from frame->restore_flags immediately after. */
+                   "movq %gs:0x30,%r11\n\t"        /* 1. Fetch TEB safely into r11 */
+                   "movb $1,0x340(%r11)\n\t"       /* 2. SUD: BLOCK (1) Windows syscalls */
+                   "movw 0x338(%r11),%dx\n\t"      /* 3. amd64_thread_data()->fs */
+                   "testw %dx,%dx\n\t"
+                   "jz 1f\n\t"
+                   "movw %dx,%fs\n\t"
+                   "1:\n\t"
+                   "movw $0x3b,%dx\n\t"            /* GSEL(GUDATA_SEL, SEL_UPL) */
+                   "movw %dx,%ss\n"
+                   "movw %dx,0x90(%rcx)\n\t"       /* Fix frame->ss for iretq */
 #endif
                    "movl 0xb4(%rcx),%edx\n\t"      /* frame->restore_flags */
                    "testl $0x48,%edx\n\t"          /* CONTEXT_FLOATING_POINT | CONTEXT_XSTATE */
@@ -3299,6 +3667,9 @@ __ASM_GLOBAL_FUNC( __wine_unix_call_dispatcher,
                    __ASM_CFI_REG_IS_AT2(rip, rcx, 0xf0,0x00)
                    "movl $0x20000,0xb4(%rcx)\n\t"  /* frame->restore_flags <- RESTORE_FLAGS_INCOMPLETE_FRAME_CONTEXT */
                    __ASM_LOCAL_LABEL("__wine_unix_call_dispatcher_prolog_end") ":\n\t"
+#if defined(__FreeBSD__) || defined(__FreeBSD_kernel__)
+                   "movb $0,%gs:0x340\n\t"       /* SUD: ALLOW (0) native Unix syscalls */
+#endif
                    "movq %rbx,0x08(%rcx)\n\t"
                    __ASM_CFI_REG_IS_AT1(rbx, rcx, 0x08)
                    "movq %rsi,0x20(%rcx)\n\t"
@@ -3361,6 +3732,22 @@ __ASM_GLOBAL_FUNC( __wine_unix_call_dispatcher,
                    "xorl %esi,%esi\n\t"
                    "movl $0x3000003,%eax\n\t"      /* _thread_set_tsd_base */
                    "syscall\n\t"
+#elif defined(__FreeBSD__)
+                   /* unix call dispatcher entry: restore pthread fsbase and WOW32 %%fs. */
+                   "movw 0x338(%r13),%ax\n\t"      /* amd64_thread_data()->fs */
+                   "testw %ax,%ax\n\t"
+                   "jz 2f\n\t"
+                   "movq 0x320(%r13),%rsi\n\t"     /* amd64_thread_data()->pthread_teb */
+                   "cmpb $0,0x7ffe028a\n\t"        /* user_shared_data->ProcessorFeatures[PF_RDWRFSGSBASE_AVAILABLE] */
+                   "jz 1f\n\t"
+                   "wrfsbase %rsi\n\t"
+                   "jmp 2f\n"
+                   "1:\n\t"
+                   "leaq 0x320(%r13),%rsi\n\t"     /* sysarch requires a pointer to the value */
+                   "movq $0xa5,%rax\n\t"           /* sysarch */
+                   "movq $0x81,%rdi\n\t"           /* AMD64_SET_FSBASE */
+                   "syscall\n\t"
+                   "2:\n\t"
 #endif
                    "ldmxcsr 0x33c(%r13)\n\t"       /* amd64_thread_data()->mxcsr */
                    "movq %r8,%rdi\n\t"             /* args */
@@ -3399,6 +3786,20 @@ __ASM_GLOBAL_FUNC( __wine_unix_call_dispatcher,
                    "movq %r14,%rcx\n\t"
                    "movq %rdx,%rax\n\t"
                    "movq 0x60(%rcx),%r14\n\t"
+#elif defined(__FreeBSD__)
+                   /* unix call dispatcher return: restore WOW32 %%fs and fix %%ss after
+                    * sysret. %%rdx is volatile across Unix calls in the Windows ABI,
+                    * so it is 100% safe to use %%dx as a scratch register here. */
+                   "movq %gs:0x30,%r11\n\t"        /* 1. Fetch TEB safely into r11 */
+                   "movb $1,0x340(%r11)\n\t"       /* 2. SUD: BLOCK (1) Windows syscalls */
+                   "movw 0x338(%r11),%dx\n\t"      /* 3. amd64_thread_data()->fs */
+                   "testw %dx,%dx\n\t"
+                   "jz 1f\n\t"
+                   "movw %dx,%fs\n\t"
+                   "1:\n\t"
+                   "movw $0x3b,%dx\n\t"            /* GSEL(GUDATA_SEL, SEL_UPL) */
+                   "movw %dx,%ss\n"
+                   "movw %dx,0x90(%rcx)\n\t"       /* Fix frame->ss for iretq */
 #endif
                    "movq 0x58(%rcx),%r13\n\t"
                    "movq 0x28(%rcx),%rdi\n\t"
