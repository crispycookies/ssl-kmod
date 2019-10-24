DESCRIPTION = "libfpgaregion"
SECTION = "base"
LICENSE = "CLOSED"
FILESEXTRAPATHS_prepend := "${THISDIR}/files/:"

SRC_URI = "git://gitlab.fh-ooe.at/smart_systems_lab/debug_service.git;protocol=https;user=gitlab+deploy-token-9:vh-CHWx5PRx8Bu1Sf-4a;branch=master"
SRCREV = "${AUTOREV}"

PACKAGES = "${PN} ${PN}-dev ${PN}-dbg ${PN}-staticdev"
# static lib => no dependencies
RDEPENDS_${PN}-staticdev = ""
RDEPENDS_${PN}-dev = ""
RDEPENDS_${PN}-dbg = ""

GIT = "${WORKDIR}/git/"
S = "${GIT}/libfpgaregion/"

MAKEFILECONFIG = "YOCTO"
EXTRA_OEMAKE += "'CONFIG=${MAKEFILECONFIG}'"

do_compile () {
    oe_runmake
}

do_install () {
    # install static lib libfpgaregion-staticdev
    install -d ${D}${libdir}
    oe_libinstall -C ${S}/Release/ -a libfpgaregion ${D}${libdir}

    # install header libfpgaregion-dev
    install -d ${D}${includedir}
    install -m 644 ${S}/libfpgaregion.h ${D}${includedir}
}