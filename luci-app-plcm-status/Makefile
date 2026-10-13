include $(TOPDIR)/rules.mk

LUCI_TITLE:=LuCI support for the PLCM status display
LUCI_DEPENDS:=+plcm-status
LUCI_PKGARCH:=all

PKG_LICENSE:=Apache-2.0

define Build/Prepare/luci-app-plcm-status
	$(FIND) $(PKG_BUILD_DIR)/htdocs $(PKG_BUILD_DIR)/root -type d -exec chmod 0755 {} +
	$(FIND) $(PKG_BUILD_DIR)/htdocs $(PKG_BUILD_DIR)/root -type f -exec chmod 0644 {} +
endef

include $(TOPDIR)/feeds/luci/luci.mk

# call BuildPackage - OpenWrt buildroot signature