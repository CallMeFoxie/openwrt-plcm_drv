include $(TOPDIR)/rules.mk

PKG_NAME:=plcm-drv
PKG_RELEASE:=5
PKG_LICENSE:=GPL-2.0-or-later OR BSD-2-Clause

include $(INCLUDE_DIR)/kernel.mk
include $(INCLUDE_DIR)/package.mk

define KernelPackage/plcm-drv
	SUBMENU:=Other modules
	TITLE:=Lanner parallel LCM driver
	DEPENDS:=@TARGET_x86
	FILES:=$(PKG_BUILD_DIR)/plcm_drv.ko
	AUTOLOAD:=$(call AutoLoad,50,plcm_drv)
endef

define KernelPackage/plcm-drv/description
	Kernel driver for the parallel-port LCM used by Lanner appliances.
endef

define Package/plcm-status
	SECTION:=utils
	CATEGORY:=Utilities
	TITLE:=System status display for plcm-drv
	DEPENDS:=+kmod-plcm-drv
endef

define Package/plcm-status/description
	Displays CPU load, RAM usage, IPv4 address, and interface bandwidth on the
	Lanner parallel LCM. Configuration is provided through UCI and the daemon is
	supervised by procd.
endef

define Build/Prepare
	mkdir -p $(PKG_BUILD_DIR)
	$(CP) ./src/. $(PKG_BUILD_DIR)/
endef

define Build/Compile
	+$(KERNEL_MAKE) $(PKG_JOBS) M="$(PKG_BUILD_DIR)" modules
	$(TARGET_CC) $(TARGET_CPPFLAGS) $(TARGET_CFLAGS) -std=gnu11 -Wall -Wextra \
		$(TARGET_LDFLAGS) -o $(PKG_BUILD_DIR)/plcm-status \
		$(PKG_BUILD_DIR)/plcm_status.c
endef

define Package/plcm-status/install
	$(INSTALL_DIR) $(1)/usr/sbin
	$(INSTALL_BIN) $(PKG_BUILD_DIR)/plcm-status $(1)/usr/sbin/plcm-status
	$(INSTALL_DIR) $(1)/etc/init.d
	$(INSTALL_BIN) ./files/plcm-status.init $(1)/etc/init.d/plcm-status
	$(INSTALL_DIR) $(1)/etc/config
	$(INSTALL_CONF) ./files/plcm-status.config $(1)/etc/config/plcm-status
endef

$(eval $(call KernelPackage,plcm-drv))
$(eval $(call BuildPackage,plcm-status))
