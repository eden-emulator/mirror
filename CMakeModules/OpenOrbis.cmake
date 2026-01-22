# SPDX-FileCopyrightText: Copyright 2025 Eden Emulator Project
# SPDX-License-Identifier: GPL-3.0-or-later

function(create_ps4_eboot project target content_id)
    set(sce_sys_dir sce_sys)
    set(sce_sys_param ${sce_sys_dir}/param.sfo)
    add_custom_command(
        OUTPUT "${target}.pkg"
        COMMAND ${CMAKE_SYSROOT}/bin/create-fself -in=bin/${target} -out=${target}.oelf --eboot ${target}_eboot.bin
        VERBATIM
        WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
        DEPENDS ${project}
    )
    add_custom_target(${project}_pkg ALL DEPENDS "${target}.pkg")
endfunction()

function(create_ps4_pkg project target content_id)
    set(sce_sys_dir sce_sys)
    set(sce_sys_param ${sce_sys_dir}/param.sfo)
    add_custom_command(
        OUTPUT "${target}.pkg"
        COMMAND ${CMAKE_SYSROOT}/bin/create-fself -in=bin/${target} -out=${target}.oelf --eboot ${target}_eboot.bin
        COMMAND mkdir -p ${sce_sys_dir}
        COMMAND ${CMAKE_SYSROOT}/bin/PkgTool.Core sfo_new ${sce_sys_param}
        COMMAND ${CMAKE_SYSROOT}/bin/PkgTool.Core sfo_setentry ${sce_sys_param} APP_TYPE --type Integer --maxsize 4 --value 1
        COMMAND ${CMAKE_SYSROOT}/bin/PkgTool.Core sfo_setentry ${sce_sys_param} APP_VER --type Utf8 --maxsize 8 --value 1.03
        COMMAND ${CMAKE_SYSROOT}/bin/PkgTool.Core sfo_setentry ${sce_sys_param} ATTRIBUTE --type Integer --maxsize 4 --value 0
        COMMAND ${CMAKE_SYSROOT}/bin/PkgTool.Core sfo_setentry ${sce_sys_param} CATEGORY --type Utf8 --maxsize 4 --value gd
        COMMAND ${CMAKE_SYSROOT}/bin/PkgTool.Core sfo_setentry ${sce_sys_param} CONTENT_ID --type Utf8 --maxsize 48 --value ${content_id}
        COMMAND ${CMAKE_SYSROOT}/bin/PkgTool.Core sfo_setentry ${sce_sys_param} DOWNLOAD_DATA_SIZE --type Integer --maxsize 4 --value 0
        COMMAND ${CMAKE_SYSROOT}/bin/PkgTool.Core sfo_setentry ${sce_sys_param} SYSTEM_VER --type Integer --maxsize 4 --value 0
        COMMAND ${CMAKE_SYSROOT}/bin/PkgTool.Core sfo_setentry ${sce_sys_param} TITLE --type Utf8 --maxsize 128 --value ${target}
        COMMAND ${CMAKE_SYSROOT}/bin/PkgTool.Core sfo_setentry ${sce_sys_param} TITLE_ID --type Utf8 --maxsize 12 --value BREW00090
        COMMAND ${CMAKE_SYSROOT}/bin/PkgTool.Core sfo_setentry ${sce_sys_param} VERSION --type Utf8 --maxsize 8 --value 1.03
        COMMAND ${CMAKE_SYSROOT}/bin/create-gp4 -out ${target}.gp4 --content-id=${content_id} --files "${target}_eboot.bin ${sce_sys_param} sce_module/libc.prx sce_module/libSceFios2.prx"
        COMMAND ${CMAKE_SYSROOT}/bin/PkgTool.Core pkg_build ${target}.gp4 .
        VERBATIM
        WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
        DEPENDS ${project}
    )
    add_custom_target(${project}_pkg ALL DEPENDS "${target}.pkg")
endfunction()

if (NOT DEFINED ENV{OO_PS4_TOOLCHAIN})
    set(ENV{OO_PS4_TOOLCHAIN} ${CMAKE_SYSROOT})
endif ()
