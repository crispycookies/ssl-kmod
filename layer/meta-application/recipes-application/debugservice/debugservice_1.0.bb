DESCRIPTION = "DebugService daemon"
SECTION = "base"
LICENSE = "CLOSED"
FILESEXTRAPATHS_prepend := "${THISDIR}/files/:"

SRC_URI = "git://gitlab.fh-ooe.at/smart_systems_lab/debug_service.git;protocol=https;user=gitlab+deploy-token-9:vh-CHWx5PRx8Bu1Sf-4a;branch=master"
SRC_URI += "file://debugservice.service"

SRCREV = "${AUTOREV}"

SYSTEMD_SERVICE_${PN} = "debugservice.service"

DEPENDS = "poco openssl libgpiod"
RDEPENDS_${PN} = "dtc poco libssl libcrypto libgpiod"

TARGET_CC_ARCH += "${LDFLAGS}"

inherit systemd

GIT = "${WORKDIR}/git/"
S = "${GIT}"

MAKEFILECONFIG = "YOCTO"
EXTRA_OEMAKE += "'CONFIG=${MAKEFILECONFIG}'"

do_compile () {
    oe_runmake
}

do_install() {
    # install service file
    install -d ${D}${systemd_unitdir}/system
    install -c -m 0644 ${WORKDIR}/debugservice.service ${D}${systemd_unitdir}/system

    # install binary
    install -d ${D}${bindir}
    install -c -m 0755 ${S}/Release/DebugService ${D}${bindir}/debugservice

    # install empty firmware dir if not existing to store fpga bitfile
    install -d -m 0755 ${D}${base_libdir}/firmware

    # install config
    install -d ${D}${sysconfdir}/debugservice
}

FILES_${PN} = "${base_libdir}/systemd/system/debugservice.service"
FILES_${PN} += "${bindir}/debugservice"
FILES_${PN} += "${sysconfdir}/debugservice"
FILES_${PN} += "${base_libdir}/firmware"

# As this package is tied to systemd, only build it when we're also building systemd.
python () {
    if not bb.utils.contains ('DISTRO_FEATURES', 'systemd', True, False, d):
        raise bb.parse.SkipPackage("'systemd' not in DISTRO_FEATURES")
}
