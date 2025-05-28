################################################################################
#
# DOSBOX-PURE
#
################################################################################
LIBRETRO_DOSBOXPURE_VERSION = d96443ea4b020c2c23944ccd3540b7febc58e4c7
LIBRETRO_DOSBOXPURE_SITE = $(call github,libretro,dosbox-pure,$(LIBRETRO_DOSBOXPURE_VERSION))

ifeq ($(BR2_arm),y)
        LIBRETRO_DOSBOXPURE_CONF += WITH_DYNAREC=arm
endif

ifeq ($(BR2_aarch64),y)
        LIBRETRO_DOSBOXPURE_CONF += WITH_DYNAREC=arm64
endif

define LIBRETRO_DOSBOXPURE_BUILD_CMDS
	CFLAGS="$(TARGET_CFLAGS)" CXXFLAGS="$(TARGET_CXXFLAGS)" \
	       LDFLAGS="$(TARGET_LDFLAGS)" \
	       $(MAKE) -C $(@D)/ \
	       CC="$(TARGET_CC)" CXX="$(TARGET_CXX)" LD="$(TARGET_CC)" \
	       RANLIB="$(TARGET_RANLIB)" AR="$(TARGET_AR)" \
	       platform="unix" $(LIBRETRO_DOSBOXPURE_CONF)
endef

define LIBRETRO_DOSBOXPURE_INSTALL_TARGET_CMDS
	$(INSTALL) -D $(@D)/dosbox_pure_libretro.so \
		$(TARGET_DIR)/usr/lib/libretro/dosbox_pure_libretro.so
endef

$(eval $(generic-package))
