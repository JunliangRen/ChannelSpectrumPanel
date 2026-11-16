当前功能扩展与未完成项见 [PARITY.md](PARITY.md)（0.3.0）。

# x64 首版实现与验证记录

日期：2026-11-16。首版：0.1.0；当前更新：0.2.0。

0.2.0 样式、配置升级和验证见 [SIMPLE-LINE.md](SIMPLE-LINE.md)。以下保留首版记录。目标：64 位 foobar2000 2.x / Columns UI。

## 结构

- `src/panel.cpp`：Columns UI 工厂、独立 GUID、原生窗口、可视化流、配置序列化和弹窗。
- `src/spectrum.h`：频率分组、幅度曲线、左右坐标和定点峰值状态。
- `src/render.h`：灰度渐变细柱、单像素高峰值亮点、黑底 DIB。
- `tests/core_tests.cpp`：纯离线核心断言及合成 BMP。

## 原版行为的落实

FFT 默认 2048，对应 1024 个频率 bin。调用宿主 `get_spectrum_absolute`，旧模式创建流 flags=0，新模式 flags=1。读取 SDK 的 audio_sample 后先转 float32，再进行计算，以接近原版样本消费精度。跳过 DC bin。

设 A 为该柱覆盖 bin 的最大幅度，M 为 bin 数，R 为 range，K=0.2309×log2(M+1)：

- 旧模式：q=(100/R)×A/K。
- 旧模式勾选 Log amplitude：q=(100/R)×ln(1+2A)/ln(1+2K)。
- 新模式：q=(100/R)×max(0,1+(2/7)×log10(max(A,2^-17)))。
- 新模式勾选 Log amplitude：上式斜率改为 1/4。

柱高为截断后的 height×q，并裁剪到面板高度。无穷、NaN、非正数忽略。左右分别取最大值，不求平均、不混声道。

设中心 x=floor(W/2)，柱序 j 从低到高，pitch=柱宽+间距：左柱起点为 x-j×pitch-柱宽，右柱起点为 x+j×pitch。奇数宽最外边可能保留一个背景像素。频率分组沿用静态分析中的 Q(k) 和锚点公式，但最终使用固定间距量化和范围裁剪；这是首版主要近似点，不能声称与原版所有分辨率下完全相同。

峰值的屏幕 y 坐标使用 256 倍定点数。当前柱顶达到或超过峰值（含相等）时重置峰值、速度和保持时间。保持结束后每帧 position+=velocity，再 velocity+=acceleration。acceleration=trunc(128×fall×interval/(500-interval))；默认参数得 36，即第一帧下落 36/256 px。峰值落到画面之外进行饱和保护。GetTickCount 的无符号差处理计时回绕。

隐藏时释放流，暂停时冻结，停止时清空，跳转或较大时间断点重置。尺寸和声道布局变化清除旧峰值。配置只在 OK 时保存，Cancel 不应用；持久化版本 1，共 44 字节。FFT / 幅度设置变更通过弹窗应用时同时重置流与动画。

## 当前验证

Release x64 DLL、离线测试程序均构建成功。测试覆盖：四种幅度曲线参考点、静音/非有限输入、峰值保持临界点、十帧累积下落、相等峰值刷新、计时回绕、左右隔离、低频中心与高频外侧、同组取 max、多个面板宽度的边界、相同声道等高、输入截断、声道布局改变重置峰值。断言数量包含逐柱几何检查，不等于独立测试案例数。

`build/synthetic-preview.bmp` 使用相同 Engine 和 render 生成。它用于检查几何和绘制结果，采用合成频谱和新 FFT 幅度模式；不证明真实音乐或默认旧 FFT 的宿主输出已经匹配。

宿主加载测试未完成：准备了 `build/portable-test` 独立便携副本，但通过 Computer Use 启动时应用授权超时。没有向现有用户组件目录安装此 DLL，也没有修改现有播放器配置。仍需验证：组件识别、CUI 添加/删除面板、播放/暂停/停止/拖动进度、配置保存重启、多实例与关闭、实际音频幅度及原版动画对照。

## 依赖与来源

- foobar2000 SDK 2026-10-01：https://www.foobar2000.org/downloads/SDK-2026-10-01.7z
- SDK 压缩包 SHA256：D4C55077336FAE81BF8DF0259B5B2748FA45EA84132C656EAD93EB123CBCDC26。
- Columns UI SDK：https://github.com/reupen/columns_ui-sdk
- 固定提交：e33a58aece664dc2f02342289f25211af5e7576c。
- 第三方许可见根目录 THIRD-PARTY-NOTICES.txt。

未恢复原作者的源码或符号名；本组件是依据静态证据进行的独立 C++ 实现。未实现原版所有排列模式、DirectDraw 扫描线同步或原版配置导入。
