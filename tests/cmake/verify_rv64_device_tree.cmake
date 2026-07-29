if(NOT DEFINED DTB OR NOT EXISTS "${DTB}")
    message(FATAL_ERROR "Generated DTB does not exist: ${DTB}")
endif()
if(NOT DEFINED FDTGET OR NOT EXISTS "${FDTGET}")
    message(FATAL_ERROR "fdtget does not exist: ${FDTGET}")
endif()

function(assert_fdt_property node property type expected)
    execute_process(
        COMMAND
            "${FDTGET}"
            "-t${type}"
            "${DTB}"
            "${node}"
            "${property}"
        RESULT_VARIABLE result
        OUTPUT_VARIABLE actual
        ERROR_VARIABLE error)
    if(NOT result EQUAL 0)
        message(
            FATAL_ERROR
            "Cannot read ${node}/${property}: ${error}")
    endif()

    string(STRIP "${actual}" actual)
    if(NOT actual STREQUAL expected)
        message(
            FATAL_ERROR
            "${node}/${property}: expected '${expected}', got '${actual}'")
    endif()
endfunction()

assert_fdt_property(
    "/"
    "compatible"
    "s"
    "rv64-emulator,virt")
assert_fdt_property(
    "/"
    "model"
    "s"
    "RV64 CPU Emulator Virtual Machine")
assert_fdt_property(
    "/cpus/cpu@0"
    "riscv,isa"
    "s"
    "rv64imac_zicsr_zifencei")
assert_fdt_property(
    "/cpus/cpu@0"
    "mmu-type"
    "s"
    "riscv,sv39")
assert_fdt_property(
    "/memory@80000000"
    "reg"
    "x"
    "0 80000000 0 10000000")
assert_fdt_property(
    "/soc/clint@2000000"
    "reg"
    "x"
    "0 2000000 0 10000")
assert_fdt_property(
    "/soc/rtc@101000"
    "compatible"
    "s"
    "google,goldfish-rtc")
assert_fdt_property(
    "/soc/rtc@101000"
    "reg"
    "x"
    "0 101000 0 1000")
assert_fdt_property(
    "/soc/rtc@101000"
    "interrupts"
    "x"
    "b")
assert_fdt_property(
    "/soc/interrupt-controller@c000000"
    "reg"
    "x"
    "0 c000000 0 4000000")
assert_fdt_property(
    "/soc/interrupt-controller@c000000"
    "riscv,ndev"
    "x"
    "35")
assert_fdt_property(
    "/soc/serial@10000000"
    "reg"
    "x"
    "0 10000000 0 100")
assert_fdt_property(
    "/soc/serial@10000000"
    "interrupts"
    "x"
    "a")
assert_fdt_property(
    "/soc/virtio_mmio@10001000"
    "reg"
    "x"
    "0 10001000 0 1000")
assert_fdt_property(
    "/soc/virtio_mmio@10001000"
    "interrupts"
    "x"
    "1")
assert_fdt_property(
    "/soc/virtio_mmio@10002000"
    "compatible"
    "s"
    "virtio,mmio")
assert_fdt_property(
    "/soc/virtio_mmio@10002000"
    "reg"
    "x"
    "0 10002000 0 1000")
assert_fdt_property(
    "/soc/virtio_mmio@10002000"
    "interrupts"
    "x"
    "2")
assert_fdt_property(
    "/soc/syscon@11100000"
    "reg"
    "x"
    "0 11100000 0 1000")
assert_fdt_property(
    "/chosen"
    "stdout-path"
    "s"
    "serial0:115200n8")
assert_fdt_property(
    "/chosen"
    "bootargs"
    "s"
    "console=tty0 console=ttyS0,115200 earlycon=uart8250,mmio,0x10000000 root=/dev/vda rw rootwait net.ifnames=0")
assert_fdt_property(
    "/chosen"
    "ranges"
    "x"
    "")
assert_fdt_property(
    "/chosen/framebuffer@40000000"
    "compatible"
    "s"
    "simple-framebuffer")
assert_fdt_property(
    "/chosen/framebuffer@40000000"
    "reg"
    "x"
    "0 40000000 0 12c000")
assert_fdt_property(
    "/chosen/framebuffer@40000000"
    "width"
    "u"
    "640")
assert_fdt_property(
    "/chosen/framebuffer@40000000"
    "height"
    "u"
    "480")
assert_fdt_property(
    "/chosen/framebuffer@40000000"
    "stride"
    "u"
    "2560")
assert_fdt_property(
    "/chosen/framebuffer@40000000"
    "format"
    "s"
    "x8r8g8b8")
