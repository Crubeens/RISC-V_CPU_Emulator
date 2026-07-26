# Third-party notices

## rv64gc-emu

部分外设寄存器布局和设备行为参考：

- Project: `bane9/rv64gc-emu`
- Source: https://github.com/bane9/rv64gc-emu
- Reference revision used during framework design: `554f364119d5d104faee802cee52cb315c51bfc8`
- License: MIT

本项目没有导入该项目的 CPU、CSR、MMU 或指令执行实现。外设代码经过重新组织，以移除具体 CPU 依赖并增加边界检查、结构化错误、确定性时钟及可测试接口。

MIT License

Copyright (c) 2025 bane9

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
