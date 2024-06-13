TEMPLATE = lib
TARGET = ololord

VERSION = 0.1.0
VER_MAJ = 0
VER_MIN = 1
VER_PAT = 0

CONFIG += release

QT = gui xml
BEQT = core sql

include(../../prefix.pri)

DEFINES += OLOLORD_BUILD_LIB

SOURCES += \
    cache.cpp \
    controller.cpp \
    database.cpp \
    markup.cpp \
    ololordapplication.cpp \
    search.cpp \
    settingslocker.cpp \
    tools.cpp \
    transaction.cpp \
    translator.cpp

HEADERS += \
    cache.h \
    controller.h \
    database.h \
    global.h \
    markup.h \
    ololordapplication.h \
    search.h \
    settingslocker.h \
    tools.h \
    transaction.h \
    translator.h

include(ajax/ajax.pri)
include(board/board.pri)
include(captcha/captcha.pri)
include(controller/controller.pri)
include(plugin/plugin.pri)
include(route/route.pri)
include(stored/stored.pri)

#Processing CppCMS templates
CPPCMS_PROCESSING_COMMAND=$${CPPCMS_PREFIX}/bin/cppcms_tmpl_cc
mac|unix {
    CPPCMS_TEMPLATES=$$files($${PWD}/template/*)
} else:win32 {
    CPPCMS_TEMPLATES=$$files($${PWD}\\template\\*)
}

for(CPPCMS_TEMPLATE, CPPCMS_TEMPLATES) {
    CPPCMS_TEMPLATES_STRING=$${CPPCMS_TEMPLATES_STRING} \"$${CPPCMS_TEMPLATE}\"
}

CPPCMS_PROCESSING_COMMAND=$${CPPCMS_PROCESSING_COMMAND} $${CPPCMS_TEMPLATES_STRING} -o \"$${PWD}/compiled_templates.cpp\"

win32:CPPCMS_PROCESSING_COMMAND=$$replace(CPPCMS_PROCESSING_COMMAND, "/", "\\")

system(python $${CPPCMS_PROCESSING_COMMAND})

SOURCES += compiled_templates.cpp
#end processing

contains(LORD_CONFIG, builtin_resources) {
    DEFINES += OLOLORD_BUILTIN_RESOURCES
    RESOURCES += \
        ololord_res.qrc \
        ololord_static.qrc \
        ololord_static_css.qrc \
        ololord_static_img.qrc \
        ololord_static_js.qrc \
        ololord_static_video.qrc
}

##############################################################################
################################ Installing ##################################
##############################################################################

!contains(LORD_CONFIG, no_install) {

contains(LORD_CONFIG, libs) {

##############################################################################
################################ Libs ########################################
##############################################################################

target.path = $${LORD_LIBS_INSTALLS_PATH}
INSTALLS = target

}

} #end !contains(LORD_CONFIG, no_install)
