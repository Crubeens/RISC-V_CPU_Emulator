# RV32 CPU 文件结构

```text
rv32_cpu_base/
├── CMakeLists.txt
├── CMakePresets.json
├── RV32_CPU_实现框架.md
├── include/
│   └── rv32/
│       ├── cpu.h
│       ├── decode.h
│       ├── dmem.h
│       ├── execute.h
│       ├── imem.h
│       └── types.h
├── src/
│   ├── cpu.c
│   ├── decode.c
│   ├── dmem.c
│   ├── execute.c
│   ├── imem.c
│   └── main.c
└── tests/
    ├── test_decode.c
    ├── test_memory.c
    └── test_step.c
```
