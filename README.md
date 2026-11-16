# Channel Spectrum Mirror (x64)

当前实验版本：0.4.0。独立重建的 64 位 foobar2000 2.x / Columns UI 组件，不是原作者官方版本，也不是恢复出的原始源码。**目前尚不能称为完整、逐像素一致的原版移植。**

## 安装

在 Preferences → Components → Install 选择 `dist/foo_uie_channel_spectrum_x64-0.4.0.fb2k-component`，应用并重启。在 Columns UI 布局添加 Visualisations → Channel Spectrum Mirror。右键 Configure 设置。

已有 0.1/0.2 配置继续读取，默认保持左右镜像、低频居中、黑底灰白渐变柱和峰值亮点。新配置版本为 3，不支持用旧版组件读取。仅支持 x64 Columns UI；需要对应 x64 MSVC 运行库。

## 当前功能

- 原版对应模式：Simple、Horizontal、Vertical、Simple-antialiasing、Horizontal-antialiasing、Simple-line；保留前两版的 Gradient bars、Separate thin lines。
- Mirror / Right / Left，频率反转、声道位置反转，0/90/180/270 度顺时针旋转。
- Separate / Merge all / Specified input；可启用全部输入声道，设置 18 项顺序和活动掩码。编号按 18 个扬声器位置，使用宿主 mask 映射实际输入；详见还原记录。
- 柱宽、间距、分段柱、左右留白、声道间距、面板边框。
- 五种自定义 RGB 颜色、八色前景和背景调色板。
- 2 kHz 频率刻度、相对幅度刻度、长短刻度长度；长度 100 表示整个绘图方向长度。
- 峰值开关、保持时间和下降速度；旧/新 FFT、FFT 大小、幅度曲线和频率轴模式。
- 父窗口背景和背景缓存、实验性 DirectDraw 扫描线同步；默认关闭。
- Apply / Cancel / Defaults；`.csm` 新版预设，完整 300 字节原版 `.csp` 和旧 DWORD 边界前缀转换。

Simple-line 原版会禁用峰值，本版按用户要求允许保留。渐变柱和分离细竖线是新增样式，导出为 `.csp` 时会明确拒绝；请使用 `.csm` 保存它们。原版 Separate 没有本项目“强制双声道”扩展，`.csp` 再导入时按全部活动输入声道处理。

## 已验证与限制

Release x64 构建和 7696 项离线断言通过；数量包含逐柱边界检查。覆盖配置版本迁移、完整字段往返、旧预设字段/颜色字节序/旧前缀兼容、声道、旋转、样式、峰值、留白和缓冲区边界。

独立便携测试副本已成功启动并显示首次 UI 选择窗口，但 Computer Use 无法激活窗口，重试仍返回 `failed to activate captured window`，因此没有完成 Columns UI 面板添加、设置交互、真实音频播放或稳定性验证。用户现有播放器配置未被修改。

本轮已重建原始四 DWORD 频率描述符：受控 x87/SSE 仿真下，13 组夹具的 17,637 条有效记录零差异。已恢复八档幅度刻度、扬声器映射、原生 GDI 渐变和折线、短预设前缀兼容。

尚未证明完全一致：所有旋转的边缘像素、跨声道折线端点、bin 0 刻度、不同浮点环境、透明合成/缓存、扫描线调度及真实宿主 FFT 输入。受控仿真不替代原版实际播放对照。详见 [0.4.0 还原记录](analysis/RESTORATION-0.4.md)。

完整功能状态见 [差异与移植清单](analysis/PARITY.md)，不要将“有同名开关”理解为已验证完全一致。

## 构建

Windows / Visual Studio C++ v143 / Windows SDK / PowerShell / Git / 7-Zip：

```powershell
.\scripts\setup-deps.ps1
.\scripts\build.ps1
```

可通过 `-MSBuild` 指定 MSBuild.exe。依赖版本和哈希固定于 setup-deps；输出在 dist/；测试和合成图在 build/。调试 PDB 不包含在安装包中。自有文本使用 UTF-8 / CRLF，第三方 SDK 保留原始文件。

[静态分析](analysis/STATIC-ANALYSIS.md) · [首版实现](analysis/IMPLEMENTATION.md) · [Simple-line 分析](analysis/SIMPLE-LINE.md) · [第三方许可](THIRD-PARTY-NOTICES.txt)

## 仓库内容与可选指令仿真

仓库包含源码、构建/打包脚本、分析记录、精选证据及回归测试夹具。SDK、原版 DLL、完整反汇编、Python 依赖、便携播放器、构建产物与重复仿真输出不提交；已存在的本地文件保留。

正常构建及回归测试使用已提交夹具，不需要原版 DLL 或 Python。仅在重新生成原版指令仿真夹具时，需要自行提供 SHA256 匹配的原版 DLL，并执行：

```powershell
python -m pip install --target tools/oracle-deps unicorn==2.1.4 pefile==2024.8.26
$env:CSP_ORIGINAL_DLL = 'C:\path\to\foo_uie_vis_channel_spectrum.dll'
python scripts/original_oracle.py
```

仿真脚本只执行受控坐标例程，不调用原 DLL 初始化。生成的原始 JSON 被忽略，精简的坐标夹具用于后续回归对照。
