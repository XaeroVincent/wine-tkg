--- dlls/ntdll/unix/process.c.orig	2026-03-20 13:33:36.000000000 -0700
+++ dlls/ntdll/unix/process.c	2026-03-30 00:05:12.670903000 -0700
@@ -1545,10 +1545,18 @@ NTSTATUS WINAPI NtQueryInformationProcess( HANDLE hand
             else
             {
                 PROCESS_CYCLE_TIME_INFORMATION cycles;
+                /* Static counter to simulate advancing CPU cycles */
+                static ULONG64 fake_cycle_time = 10000;
 
                 FIXME( "ProcessCycleTime (%p,%p,0x%08x,%p) stub\n", handle, info, size, ret_len );
-                cycles.AccumulatedCycles = 0;
-                cycles.CurrentCycleCount = 0;
+
+                /* Atomically increment by 10,000 to trick DRM/Steam polling.
+                 * CurrentCycleCount is taken from the same fetch result to
+                 * avoid a TOCTOU window where a concurrent caller could make
+                 * CurrentCycleCount > AccumulatedCycles — a physical impossibility
+                 * that DRM cycle-consistency checks treat as an integrity violation. */
+                cycles.AccumulatedCycles = __atomic_add_fetch(&fake_cycle_time, 10000, __ATOMIC_SEQ_CST);
+                cycles.CurrentCycleCount = cycles.AccumulatedCycles;
 
                 memcpy(info, &cycles, sizeof(PROCESS_CYCLE_TIME_INFORMATION));
             }
