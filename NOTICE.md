# NOTICE

本仓库是 [DLSS Video Player v0.28.3](https://github.com/2600th/dlss5-video-player) 的修改版。

## 上游部分

```
MIT License
Copyright (c) 2026 DLSS Video Player contributors
```

完整文本见 `LICENSE`。`src/`、`tests/`、`docs/`、`tools/`、`CMakeLists.txt` 等绝大多数内容
来自上游，未做改动或仅做下述改动。

## 本项目新增部分（MIT）

Copyright (c) 2026 HTG

- `gui/htg_gui.cpp` — 原生 Win32 图形界面（深色主题、拖放、日志框、DWM 暗色标题栏）
- `src/AmdNrAbi.h`、`src/AmdNrBackend.h`、`src/AmdNrBackend.cpp` — AMD 神经渲染后端
- `src/D3D12Renderer.*`、`src/OfflineNeuralRenderer.*` — 接入 AMD 后端的改动
- `tools/amd_nr_smoke/` — 后端冒烟测试
- `dist/` — 分发脚本（`一键转换.cmd`、`校验完整性.*`、`gen_hashes.ps1`）
- `engine/neural-runtime/NeuralWorker.exe` 的重新编译产物（可执行文件随 Release 分发）

以上同样以 MIT 授权：

```
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
```

## 不在 MIT 范围内的组件

**NVIDIA 神经渲染运行时与权重**（随 Release 附件分发，不在本仓库中）：

- `engine/neural-runtime/nvngx_dlss.dll`（NVIDIA 签名，310.9.1）
- `engine/neural-runtime/nvngx_dlssnr.dll`（社区修改版 ShortFuse 310.8.SF-v2，**签名已被移除**）
- `engine/amd-nr-assets/` 下的权据资产（`dlssnr.bin` 等，抽取自上述运行时）

这些是 NVIDIA 的专有材料。本仓库不对其授权状态作任何主张，也不通过 MIT 许可对其再授权。
**不要单独抽取、再分发或商用其中的权重。**

其余第三方组件的许可见 `THIRD_PARTY.md` 与 `THIRD_PARTY_LICENSES/`。
