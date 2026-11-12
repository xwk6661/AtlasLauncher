#pragma once

/**
 * 地图功能开关。
 *
 * 这里放的是「做完但暂时不上」的功能：代码、测试、文档都留着，
 * 只在界面上把入口摘掉。恢复的时候把对应的 bool 改回 true 就够，
 * 不需要翻代码、也不需要找回被删的实现。
 */
namespace maps::features {

/**
 * 汉化（体检 / 导出 / 自动翻译 / 写回存档 / 资源包）。
 *
 * 2026-11-12 起默认关闭：链路是通的，但「效果」不够好 ——
 * 字形表（键化图）的图根本翻不了，C 档注入后又没人验证过
 * 游戏里的实际显示，与其让玩家看到一堆看不懂的数字，
 * 不如先收起来。实现见 `MapL10n*` 系列文件，测试见
 * `MapL10nProbe_test` / `MapL10nApply_test` / `MapL10nTranslate_test`，
 * 设计方案见 `docs/atlas/07-一键汉化方案.md`。
 */
inline constexpr bool kLocalization = false;

}  // namespace maps::features
