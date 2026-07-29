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

## SDL2

The optional graphical frontend dynamically links SDL2.

- Project: SDL
- Source: https://github.com/libsdl-org/SDL
- Development version used for M7 validation: 2.32.10
- License: zlib License

Copyright (C) 1997-2025 Sam Lantinga <slouken@libsdl.org>

This software is provided 'as-is', without any express or implied warranty.
In no event will the authors be held liable for any damages arising from the
use of this software.

Permission is granted to anyone to use this software for any purpose,
including commercial applications, and to alter it and redistribute it
freely, subject to the following restrictions:

1. The origin of this software must not be misrepresented; you must not claim
   that you wrote the original software. If you use this software in a
   product, an acknowledgment in the product documentation would be
   appreciated but is not required.
2. Altered source versions must be plainly marked as such, and must not be
   misrepresented as being the original software.
3. This notice may not be removed or altered from any source distribution.

## font8x8

The SDL terminal embeds the Basic Latin bitmap table from `font8x8_basic`.

- Project: `dhepper/font8x8`
- Source: https://github.com/dhepper/font8x8
- License: Public Domain
- Embedded file: `app/src/font8x8_basic.hpp`

The table is based on public-domain IBM VGA fonts and is distributed by its
author as public domain.

## riscv-tests

The RV32IMAC acceptance suite vendors the applicable instruction-test
sources, scalar macros, and physical test environment from:

- Project: `riscv-software-src/riscv-tests`
- Source: https://github.com/riscv-software-src/riscv-tests
- Test revision: `ec8e5a29845b97b515299b89c523831b41367cda`
- Environment revision: `6de71edb142be36319e380ce782c3d1830c65d68`
- License: BSD 3-Clause

The complete license text is retained at
[`third_party/riscv-tests/LICENSE`](third_party/riscv-tests/LICENSE), and the
vendored scope/configuration is recorded in
[`third_party/riscv-tests/SOURCE_REVISION.md`](third_party/riscv-tests/SOURCE_REVISION.md).

## Berkeley SoftFloat Release 3e

The RV64 floating-point execution backend uses Berkeley SoftFloat Release 3e,
fetched during CMake configuration from:

- Repository: <https://github.com/ucb-bar/berkeley-softfloat-3>
- Pinned revision: `a0c6494cdc11865811dec815d5c0049fba9d82a8`
- Specialization: `RISCV`

Copyright 2011, 2012, 2013, 2014, 2015, 2016, 2017, 2018 The Regents of the
University of California. All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:

1. Redistributions of source code must retain the above copyright notice,
   this list of conditions and the following disclaimer.
2. Redistributions in binary form must reproduce the above copyright notice,
   this list of conditions and the following disclaimer in the documentation
   and/or other materials provided with the distribution.
3. Neither the name of the University nor the names of its contributors may
   be used to endorse or promote products derived from this software without
   specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS "AS IS" AND ANY
EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE FOR ANY
DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
(INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
