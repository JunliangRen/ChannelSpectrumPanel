# 0.4.0 还原记录

2026-11-16。独立重建 x64 实现，不是原始源码恢复。

## 频率坐标的指令级对照

scripts/original_oracle.py 在 Unicorn 2.1.4 中仅执行原 DLL 的 0x1000B370 坐标构造例程，停止于 0x1000C3A5；没有调用 DLL 初始化，也没有把原 DLL 载入本机进程。容器、清零、CopyRect 和频率阈值辅助调用由受控桩实现。原 DLL SHA256：e5bc0405fd9da2a59d9889fffc6fc21f8e9da71dc972d68c5e5218794a038c4b。

原始每 bin 四 DWORD 为 left / draw / right / baseline。一个新边界出现时，原版标记并改写前一个描述符，而不是等宽分桶后逐桶画柱。新版照此聚合重叠 bin 的幅度最大值，再更新该绘制位置的峰值。

对数映射保留 x87 中间值和部分 double 缓存的取整顺序；src/mapping_x87.asm 为独立编写的 Win64 MASM 算术辅助，进入时保存、退出时恢复 x87 控制字。本次固定仿真 profile：CW=0x027F、原 CRT SSE 截断分支启用、pow 的 x87 分支。尚未测量用户原播放器实际控制字，不能把此 profile 宣称为所有宿主的唯一行为。

13 组夹具覆盖线性/对数、宽窄窗口、256/1024 bins、二/三声道、节距、间距、方向和声道位置反转。17,637 条 bin>=1 有效描述符逐字段对照为零差异。bin 0 的原始异常边界没有照搬，绘图从 bin 1 开始；这不是全参数穷举。夹具：tests/fixtures/original-coordinates.txt；原始输出：analysis/evidence/original-coordinate-oracle.json（可重新生成，不纳入 Git）。

另外纠正：强制 Right / Left 覆盖 Reverse frequency；Mirror 在单显示声道时翻转方向，来自调用者 0x1000384B–0x10003859。

## 刻度与声道

幅度刻度来自 0x1000C13C–0x1000C39F，为 0 至 -70 dB、10 dB 一档，长短交替。旧 FFT 先计算 0.2309*log2(1+bins*10^(-index/2)) 再使用幅度映射；新 FFT 的相对坐标为 (100/range)*(1-index/7)，log 模式分母改为 8。刻度在整个 padded 图的两侧绘制，不逐声道重复；长度 100 为全宽。夹具检查 y 的容差为 1 像素，不宣称刻度逐像素零差异。

频率刻度落在描述符中心，2 kHz 一档；最初 bin 0 的安全处理和原版构造中途的描述符状态尚未逐项对照。

扬声器映射根据原 0x10001580：先检查 speaker bit 是否存在，再统计更低有效位数得到宿主紧凑通道索引。指定声道和排序使用扬声器位置，修正了把它当作连续输入编号的问题。UI 编号 1–18 依次为 FL, FR, FC, LFE, BL, BR, FLC, FRC, BC, SL, SR, TC, TFL, TFC, TFR, TBL, TBC, TBR。

## GDI 绘制

使用本机内存 DC、32 位 DIB、GradientFill 和 Polyline，替代近似颜色插值与自制折线光栅化。24 色缓存从 foreground2 到 foreground，比例为 index/23；渐变使用 RGB 分量左移 8 位的 TRIVERTEX。背景/Vertical 八色渐变按七段整数边界构建。前景八色只作用于 Vertical；Simple、Horizontal、两种 AA 不再误用该调色板。

两种 AA 是柱顶 blur 百分比高度的背景到前景渐变；blur=0 不再插入一个黑色像素。Simple-line 使用 1 px GDI pen，按物理分区补连接相邻端点。原版是向各声道点数组插入边界点，新版分开调用连接线；方向相关的端点包含、所有旋转路径及全部调色板组合仍需与原版画面逐像素对照。

峰值定点动画和幅度公式继续沿用已恢复实现。Simple-line 保留峰值是用户明确要求的扩展；原版此样式禁用峰值。

## 预设兼容

原读例程 0x1001D7B0 在 DWORD 边界遇到 EOF 时保留后续配置字段。现在接受 magic + 0 至 74 个 DWORD 的前缀，缺失尾项从调用者当前 Settings 补全；完整 300 字节继续读写。无 magic、非 DWORD 对齐、过长、未知枚举拒绝。尚无原程序真实导出/重新导入的运行时验证。对话框导入使用上次 Apply 的配置作为缺失尾项基线。

## 验证边界

Release x64 插件及测试构建成功，7696 项断言通过；计数包含逐柱检查，并不代表 7696 个独立场景。13 组描述符夹具对照包含在测试中，另生成三种样式的离线合成图。已检查 Simple-line 合成图，但不是原版播放对照截图。

Computer Use 对便携测试副本的首次 UI 选择窗口执行选择时返回 coordinate input geometry is unavailable；刷新选择并激活重试返回 failed to activate captured window。按技能恢复规则停止进一步窗口输入。没有完成 Columns UI 实例化、设置保存重启、真实播放、透明背景或扫描线同步的运行验证。

后续真实验证必须包括：相同音频/FFT 的 x86 与 x64 对照、四方向边缘像素、原版预设文件、暂停/seek/停止、多面板、DPI/多显示器、透明背景及扫描线。透明与同步当前仍为兼容实现，未声称原版代码路径完全一致。
