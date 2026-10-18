--- dlls/winebus.sys/bus_udev.c.orig
+++ dlls/winebus.sys/bus_udev.c
@@ -1408,9 +1408,36 @@ static void udev_add_device(struct udev_
 
     get_device_subsystem_info(dev, "hid", &desc, &bus);
     get_device_subsystem_info(dev, "input", &desc, &bus);
+
+#if defined(HAVE_LINUX_HIDRAW_H) && defined(__FreeBSD__)
+    /* On FreeBSD, libudev-devd does not expose the Linux-style sysfs parent
+     * hierarchy for hidraw devices, so get_device_subsystem_info() leaves
+     * vid/pid at zero. Read them directly from the open hidraw fd via
+     * HIDIOCGRAWINFO. FreeBSD's hidraw header defines the ioctl with its own
+     * 'U' type number; struct hidraw_devinfo layout matches Linux. */
+    if ((!desc.vid || !desc.pid || !bus) && !strcmp(subsystem, "hidraw"))
+    {
+        struct hidraw_devinfo hdi = {0};
+        if (ioctl(fd, HIDIOCGRAWINFO, &hdi) == 0)
+        {
+            if (!bus)      bus      = hdi.bustype;
+            if (!desc.vid) desc.vid = (unsigned short)hdi.vendor;
+            if (!desc.pid) desc.pid = (unsigned short)hdi.product;
+        }
+    }
+#endif
+
     if (bus == BUS_BLUETOOTH) desc.bus_type = BUS_TYPE_BLUETOOTH;
     else if (bus == BUS_USB) desc.bus_type = BUS_TYPE_USB;
 
+#ifdef __FreeBSD__
+    /* No sysfs USB parents under libudev-devd: skip rather than log an ERR per device. */
+    if (desc.bus_type == BUS_TYPE_USB
+            && (!udev_device_get_parent_with_subsystem_devtype(dev, "usb", "usb_device")
+            || !udev_device_get_parent_with_subsystem_devtype(dev, "usb", "usb_interface")))
+        TRACE("no USB parents for %s, skipping USB info\n", debugstr_a(devnode));
+    else
+#endif
     if (desc.bus_type == BUS_TYPE_USB) get_device_usb_info(dev, &desc);
 
     if ((desc.is_hidraw = !strcmp(subsystem, "hidraw")) && !hidraw_device_create(dev, fd, devnode, desc)) return;
