cmake_minimum_required(VERSION 3.16)
set(CMAKE_SYSTEM_NAME Generic)

set(CMAKE_C_COMPILER_WORKS TRUE)
set(CMAKE_ASM_COMPILER_WORKS TRUE)

set(DEFAULT_TOOLCHAIN_FOLDER "D:/APP/MRS/MounRiver_Studio2/resources/app/resources/win32/components/WCH/Toolchain/RISC-V Embedded GCC12")
if(DEFINED ENV{WCH_TOOLCHAIN_ROOT})
    set(DEFAULT_TOOLCHAIN_FOLDER "$ENV{WCH_TOOLCHAIN_ROOT}")
endif()
set(TOOLCHAIN_FOLDER "${DEFAULT_TOOLCHAIN_FOLDER}" CACHE PATH
    "WCH RISC-V Embedded GCC12 toolchain root")
set(TOOLCHAIN_PREFIX "riscv-wch-elf-")
set(CMAKE_C_COMPILER "${TOOLCHAIN_FOLDER}/bin/${TOOLCHAIN_PREFIX}gcc.exe")
set(CMAKE_ASM_COMPILER "${CMAKE_C_COMPILER}")
set(CMAKE_OBJCOPY "${TOOLCHAIN_FOLDER}/bin/${TOOLCHAIN_PREFIX}objcopy.exe")
set(CMAKE_SIZE "${TOOLCHAIN_FOLDER}/bin/${TOOLCHAIN_PREFIX}size.exe")

project(ch32h417_jump_app LANGUAGES C ASM)

set(APP_ALIAS "0x00004000" CACHE STRING "Zero-based execution alias")
option(JUMP_APP_ENTER_DFU "Write the boot magic word before system reset" OFF)
option(JUMP_APP_HANG "Stay in a loop to exercise WLINK recovery" OFF)
if(JUMP_APP_ENTER_DFU AND JUMP_APP_HANG)
    message(FATAL_ERROR "JUMP_APP_ENTER_DFU and JUMP_APP_HANG are mutually exclusive")
endif()
if(NOT APP_ALIAS MATCHES "^0x000(04000|20000)$")
    message(FATAL_ERROR "APP_ALIAS must be 0x00004000 or 0x00020000")
endif()

get_filename_component(ROOT_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)
set(WCH_SDK "${ROOT_DIR}/Common" CACHE PATH "CH32H417 WCH SDK root")
set(WCH_PERIPH "${WCH_SDK}/Peripheral")
set(WCH_CORE "${WCH_SDK}/Core")

file(READ "${ROOT_DIR}/Ld/Link.ld" LINKER_TEXT)
string(REPLACE
    "FLASH (rx)   : ORIGIN = 0x00000000, LENGTH = 16K"
    "FLASH (rx)   : ORIGIN = ${APP_ALIAS}, LENGTH = 0x000F0000 - ${APP_ALIAS}"
    LINKER_TEXT "${LINKER_TEXT}")
set(APP_LINKER "${CMAKE_CURRENT_BINARY_DIR}/jump_app.ld")
file(WRITE "${APP_LINKER}" "${LINKER_TEXT}")

add_executable(jump_app.elf
    "${WCH_SDK}/Startup/startup_ch32h417_v3f.S"
    "${CMAKE_CURRENT_SOURCE_DIR}/main.c"
    "${ROOT_DIR}/User/serial.c"
    "${ROOT_DIR}/User/system_ch32h417.c"
    "${WCH_CORE}/core_riscv.c"
    "${WCH_PERIPH}/src/ch32h417_flash.c"
    "${WCH_PERIPH}/src/ch32h417_gpio.c"
    "${WCH_PERIPH}/src/ch32h417_rcc.c"
    "${WCH_PERIPH}/src/ch32h417_usart.c"
)

target_include_directories(jump_app.elf PRIVATE
    "${ROOT_DIR}/User"
    "${WCH_PERIPH}/inc"
    "${WCH_CORE}"
    "${WCH_SDK}/Debug"
    "${WCH_SDK}/Startup"
)

set(ARCH_FLAGS "-march=rv32imac_zba_zbb_zbc_zbs_xw -mabi=ilp32")
set(COMMON_FLAGS "${ARCH_FLAGS} -Os -msmall-data-limit=8 -msave-restore -ffunction-sections -fdata-sections -fno-common")
set(CMAKE_C_FLAGS "${COMMON_FLAGS} -DCore_V3F -std=gnu99")
set(CMAKE_ASM_FLAGS "${ARCH_FLAGS} -Os -x assembler-with-cpp")
target_compile_definitions(jump_app.elf PRIVATE JUMP_APP_ALIAS=${APP_ALIAS})
if(JUMP_APP_ENTER_DFU)
    target_compile_definitions(jump_app.elf PRIVATE JUMP_APP_ENTER_DFU=1)
else()
    target_compile_definitions(jump_app.elf PRIVATE JUMP_APP_ENTER_DFU=0)
endif()
if(JUMP_APP_HANG)
    target_compile_definitions(jump_app.elf PRIVATE JUMP_APP_HANG=1)
else()
    target_compile_definitions(jump_app.elf PRIVATE JUMP_APP_HANG=0)
endif()

set_target_properties(jump_app.elf PROPERTIES LINK_FLAGS
    "${ARCH_FLAGS} -Os -T \"${APP_LINKER}\" -nostartfiles -Wl,--gc-sections -Wl,-Map,jump_app.map --specs=nano.specs --specs=nosys.specs")

add_custom_command(TARGET jump_app.elf POST_BUILD
    COMMAND ${CMAKE_OBJCOPY} -O binary jump_app.elf jump_app.bin
    COMMAND ${CMAKE_SIZE} --format=berkeley jump_app.elf
)
