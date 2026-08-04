# CH32H417 DFU Bootloader — CMake Build
# Toolchain: MRS RISC-V GCC12 (riscv-wch-elf-)
# Generator: Ninja

cmake_minimum_required(VERSION 3.16)
set(CMAKE_SYSTEM_NAME Generic)

SET(CMAKE_C_COMPILER_WORKS TRUE)
SET(CMAKE_CXX_COMPILER_WORKS TRUE)
SET(CMAKE_ASM_COMPILER_WORKS TRUE)

# ── Toolchain (from MRS install) ──────────────────────────────────────────
set(DEFAULT_TOOLCHAIN_FOLDER "D:/APP/MRS/MounRiver_Studio2/resources/app/resources/win32/components/WCH/Toolchain/RISC-V Embedded GCC12")
if(DEFINED ENV{WCH_TOOLCHAIN_ROOT})
    set(DEFAULT_TOOLCHAIN_FOLDER "$ENV{WCH_TOOLCHAIN_ROOT}")
endif()
set(TOOLCHAIN_FOLDER "${DEFAULT_TOOLCHAIN_FOLDER}" CACHE PATH
    "WCH RISC-V Embedded GCC12 toolchain root")
set(TOOLCHAIN_PREFIX "riscv-wch-elf-")
set(CMAKE_C_COMPILER   "${TOOLCHAIN_FOLDER}/bin/${TOOLCHAIN_PREFIX}gcc.exe")
set(CMAKE_CXX_COMPILER "${TOOLCHAIN_FOLDER}/bin/${TOOLCHAIN_PREFIX}g++.exe")
set(CMAKE_ASM_COMPILER "${TOOLCHAIN_FOLDER}/bin/${TOOLCHAIN_PREFIX}gcc.exe")
set(CMAKE_OBJCOPY      "${TOOLCHAIN_FOLDER}/bin/${TOOLCHAIN_PREFIX}objcopy.exe")
set(CMAKE_OBJDUMP      "${TOOLCHAIN_FOLDER}/bin/${TOOLCHAIN_PREFIX}objdump.exe")
set(CMAKE_SIZE         "${TOOLCHAIN_FOLDER}/bin/${TOOLCHAIN_PREFIX}size.exe")

# ── Project ───────────────────────────────────────────────────────────────
set(PROJECT_NAME "ch32h417_dfu")
set(TARGET_ELF    "${PROJECT_NAME}.elf")
project("${PROJECT_NAME}" LANGUAGES C ASM)

option(DFU_DEBUG_STAY_IN_DFU "Return to dfuIDLE after manifestation" OFF)
option(DFU_FLASH_DEBUG_LOG "Enable verbose Flash UART logging" OFF)

# ── SDK paths ─────────────────────────────────────────────────────────────
set(WCH_SDK "${CMAKE_CURRENT_SOURCE_DIR}/Common" CACHE PATH
    "CH32H417 WCH SDK root")
set(WCH_PERIPH  "${WCH_SDK}/Peripheral")
set(WCH_CORE    "${WCH_SDK}/Core")
set(WCH_DEBUG   "${WCH_SDK}/Debug")

# ── Source files ──────────────────────────────────────────────────────────
set(CHERRYUSB "${CMAKE_CURRENT_SOURCE_DIR}/CherryUSB")

set(SRC_FILES
    "${WCH_SDK}/Startup/startup_ch32h417_v3f.S"
    "${CMAKE_CURRENT_SOURCE_DIR}/User/main.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/User/dfu_usb.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/User/dfu_flash.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/User/serial.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/User/system_ch32h417.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/User/ch32h417_it.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/User/usb_desc.c"
    "${WCH_CORE}/core_riscv.c"
    "${WCH_PERIPH}/src/ch32h417_flash.c"
    "${WCH_PERIPH}/src/ch32h417_gpio.c"
    "${WCH_PERIPH}/src/ch32h417_rcc.c"
    "${WCH_PERIPH}/src/ch32h417_usart.c"
    # CherryUSB core + port + DFU class
    "${CHERRYUSB}/core/usbd_core.c"
    "${CHERRYUSB}/port/usb_dc_ch32h417_usbhs.c"
    "${CHERRYUSB}/class/dfu/usbd_dfu.c"
)

# ── Include directories ───────────────────────────────────────────────────
include_directories(
    "${CMAKE_CURRENT_SOURCE_DIR}/User"
    "${WCH_PERIPH}/inc"
    "${WCH_CORE}"
    "${WCH_DEBUG}"
    "${WCH_SDK}/Startup"
    # CherryUSB include paths
    "${CHERRYUSB}/common"
    "${CHERRYUSB}/core"
    "${CHERRYUSB}/port"
    "${CHERRYUSB}/class/dfu"
)

# ── Compiler flags ────────────────────────────────────────────────────────
set(ARCH_FLAGS  "-march=rv32imac_zba_zbb_zbc_zbs_xw -mabi=ilp32")
set(OPT_FLAGS   "-Os -msmall-data-limit=8 -msave-restore")
set(WARN_FLAGS  "-Wall -Wno-unused-variable -fmax-errors=20")
set(DBG_FLAGS   "-g")
set(SECT_FLAGS  "-ffunction-sections -fdata-sections -fno-common -flto -fno-unwind-tables -fno-asynchronous-unwind-tables")

set(CMAKE_C_FLAGS   "${ARCH_FLAGS} ${OPT_FLAGS} ${WARN_FLAGS} ${DBG_FLAGS} ${SECT_FLAGS} -DCore_V3F -std=gnu99")
set(CMAKE_ASM_FLAGS "${ARCH_FLAGS} ${OPT_FLAGS} ${DBG_FLAGS} -x assembler-with-cpp")
set(CMAKE_CXX_FLAGS "${ARCH_FLAGS} ${OPT_FLAGS} ${WARN_FLAGS} ${DBG_FLAGS} ${SECT_FLAGS} -std=gnu++11")

# ── Link ──────────────────────────────────────────────────────────────────
add_executable("${TARGET_ELF}" ${SRC_FILES})

if(DFU_DEBUG_STAY_IN_DFU)
    target_compile_definitions("${TARGET_ELF}" PRIVATE DFU_DEBUG_STAY_IN_DFU=1)
else()
    target_compile_definitions("${TARGET_ELF}" PRIVATE DFU_DEBUG_STAY_IN_DFU=0)
endif()
if(DFU_FLASH_DEBUG_LOG)
    target_compile_definitions("${TARGET_ELF}" PRIVATE DFU_FLASH_DEBUG_LOG=1)
else()
    target_compile_definitions("${TARGET_ELF}" PRIVATE DFU_FLASH_DEBUG_LOG=0)
endif()

set(LINK_FLAGS "${ARCH_FLAGS} ${OPT_FLAGS} ${DBG_FLAGS} -flto -T \"${CMAKE_CURRENT_SOURCE_DIR}/Ld/Link.ld\" -nostartfiles -Xlinker --gc-sections -Wl,-Map,\"${PROJECT_NAME}.map\" --specs=nano.specs --specs=nosys.specs -o \"${TARGET_ELF}\"")

set_target_properties("${TARGET_ELF}" PROPERTIES LINK_FLAGS "${LINK_FLAGS}")

target_link_libraries("${TARGET_ELF}" m)

# ── Post-build ────────────────────────────────────────────────────────────
add_custom_command(TARGET "${TARGET_ELF}" POST_BUILD
    COMMAND ${CMAKE_OBJCOPY} ARGS -O binary "${TARGET_ELF}" "${PROJECT_NAME}.bin"
    COMMAND ${CMAKE_OBJCOPY} ARGS -O ihex   "${TARGET_ELF}" "${PROJECT_NAME}.hex"
    COMMAND ${CMAKE_OBJDUMP} ARGS --all-headers --demangle --disassemble -M xw "${TARGET_ELF}" > "${PROJECT_NAME}.lst"
    COMMAND ${CMAKE_SIZE}    ARGS --format=berkeley "${TARGET_ELF}"
)
