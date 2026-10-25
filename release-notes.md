## 下载

| 文件 | 大小 | SHA-256 |
|---|---|---|
| `HTG-DLSS5-AMD-v1.01-win64.7z` | 349 MiB (365,607,713 字节) | `7144D875B12C2D861213B46DD5472E9A7A31CFBC4384226CAB387C7160127D63` |

解压后 812 MiB / 921 个文件。**必须解压整个目录**，`HTG-DLSS5-AMD.exe` 单独拿出去跑不起来。

## 这个版本做了什么

把 DLSS 神经渲染离线跑在 AMD 显卡上。

上游 `NeuralWorker` 走 NVIDIA NGX，在 A 卡上直接失败（`Feature 18 was not created`）。
本项目重写了后端，改为读写 AMD 神经渲染资产（Mochizuki NR + RenoDX DLSS5），
并配了一个原生 Win32 界面，不需要命令行。

- **一键转换**：选视频 → 开始，输出同目录 `原名-nr.mkv`
- **深色界面**：原生 Win32 绘制，无 Electron、无 .NET 运行时依赖，exe 只有 194 KB
- **诊断日志**：`%TEMP%\HTG-DLSS5-AMD-gui.log` 记录每次转换的完整命令行、PID、退出码
- **完整性自检**：包内附 `校验完整性.cmd`，919 个文件逐一比对 SHA-256

## 验证记录

- 1080p / 中文路径 / 日文长文件名 实测通过，退出码 0
- 打包后解压到干净目录重跑自检：`checked 919 files, 0 problem(s)`
- GUI 端到端自检（`--selftest` 隐藏开关）跑通完整链路

## 已知问题

**未做代码签名。** 首次运行会被 Windows SmartScreen 拦一次，点「更多信息 → 仍要运行」即可。
SHA-256 是唯一的完整性凭据，请核对上面的哈希。

**跑过一次转换后包内会出现运行痕迹。** `engine\neural-runtime\ReShade.ini` 会被重写
（写入本机缓存路径），`engine\` 下会多出 `.log` 和 `cache\`。这是正常现象，不影响使用。
若你核对 SHA-256 想验证解压是否完好，请在**首次运行之前**跑 `校验完整性.cmd`。

## 系统要求

Windows 10 / 11 x64 · AMD 显卡（建议 RDNA2+）· 1080p 建议 6 GB 显存

## 关于运行时

包内含 NVIDIA 专有运行时（`nvngx_dlss.dll`、`nvngx_dlssnr.dll`）及抽取的权重。
`nvngx_dlssnr.dll` 是社区修改版，**签名已移除**，属实验性运行时。权重不属于 MIT 范围，
请勿单独抽取再分发。详见 `EXPERIMENTAL_RUNTIME_NOTICE.txt` 与 `NOTICE.md`。
