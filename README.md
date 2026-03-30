# wine-xaero-devel

**AI Disclosure:** _AI agents were used to assist with development._

FreeBSD Wine Devel with paches to workaround the Windows Steam client not displaying a program window

**Other optionally supported features:**
* Wine NTSync (requires FreeBSD NTSync kernel driver)
* Syscall User Dispatch (requires FreeBSD Gaming kernel)
* Both new & old WoW64 modes ("make config" build option) 

Based on code mainly written by: Alex (shkhln), Thibault Payet (monwarez), and Gerald Pfeifer (gerald)

NTSync support requires a kernel driver:
https://github.com/XaeroVincent/FreeBSD-NTSync

Syscall User Dispatch requires a patched kernel:
https://github.com/XaeroVincent/FreeBSD-Gaming-Kernel
