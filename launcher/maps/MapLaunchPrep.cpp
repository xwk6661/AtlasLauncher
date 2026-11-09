// SPDX-License-Identifier: GPL-3.0-only
/*
 *  Prism Launcher - Minecraft Launcher
 *  Copyright (c) 2026 Prism Launcher contributors
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, version 3.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */
#include "MapLaunchPrep.h"

#include <QDateTime>
#include <QDebug>
#include <QFile>

#include <algorithm>

#include "AgentBootstrap.h"
#include "Application.h"
#include "BaseInstance.h"
#include "DataVersionTable.h"
#include "FileSystem.h"
#include "minecraft/MinecraftInstance.h"
#include "settings/SettingsObject.h"

namespace maps {

namespace {

// 「这个版本能不能 quickPlay」在正常流程里靠实例解析后的 traits 判断。但 Application::launch()
// 走的是「刚启动、组件还没解析完」的时刻，那一刻 traits() 是空的——于是 1.21.1 会被误判成
// 「不支持 quickPlay」，进而不写 JoinWorldOnLaunch，游戏停在主菜单（2026-11-03 实测）。
// 所以这里不猜：traits 为空 = 还不知道，由调用方决定怎么降级。
QSet<QString> traitsFor(MinecraftInstance* instance)
{
    return instance->traits();
}

/**
 * 1.20+ 的补丁注入：把「不支持使用实验性设置的世界」那一页跳过。
 *
 * 与轨道无关 —— 只要这个版本有签名表就注入。原因有两个：
 *   · 1.20+ 走 quickPlay 时，这一页正是唯一会挡住自动进图的东西；
 *   · 启动瞬间 traits 可能还没解析（命令行/快捷方式冷启动），那时判不出轨道，
 *     但补丁本身不需要知道轨道，先注进去就行。
 * 失败一律不挡启动：最多少点一次「我知道我在做什么」。
 */
void tryInjectIntroSkip(MinecraftInstance* instance, const MapMeta& meta, MapLaunchPrep::Plan* plan)
{
    if (!instance->settings()->get("MapSkipExperimentalPrompt").toBool()) {
        // 关掉开关还不够：上一次启动写进 JvmArgs 的那条会继续生效，得摘掉
        (void) AgentBootstrap::clearIntroSkip(instance);
        return;
    }

    QString error;
    const auto rule = AgentBootstrap::introSkipRule(meta.version.mcVersion, &error);
    if (!rule) {
        plan->warnings.append(error);
        (void) AgentBootstrap::clearIntroSkip(instance);
        return;
    }

    const QString logPath = AgentBootstrap::introSkipLogPath(instance);
    const auto injected = AgentBootstrap::injectIntroSkip(instance, *rule, logPath);
    if (!injected) {
        plan->warnings.append(injected.error());
        return;
    }
    plan->reason += QStringLiteral("；已注入 introskip（跳过实验性设置确认页，日志 %1）").arg(logPath);
}

}  // namespace

QString MapLaunchPrep::trackName(Track track)
{
    switch (track) {
        case Track::QuickPlay:
            return QStringLiteral("quickplay");
        case Track::Agent:
            return QStringLiteral("agent");
        case Track::Plain:
            break;
    }
    return QStringLiteral("plain");
}

MapLaunchPrep::Plan MapLaunchPrep::plan(MinecraftInstance* instance, const MapMeta& meta, const QString& worldFolder)
{
    Plan plan;

    if (instance == nullptr) {
        plan.reason = QStringLiteral("实例不存在");
        return plan;
    }
    if (meta.worlds.isEmpty()) {
        plan.reason = QStringLiteral("这个实例没有记录世界目录，无法自动进图");
        return plan;
    }

    plan.worldFolder = worldFolder.isEmpty() ? meta.worlds.first().folder : worldFolder;
    // 读实例设置而不是全局：这三项在实例里是 OverrideSetting，
    // 没勾「覆盖全局的地图启动设置」时自动回落到全局值（设置页的改动即时可见）。
    plan.autoEnter = instance->settings()->get("MapAutoEnter").toBool();
    plan.closeOnQuit = instance->settings()->get("MapSafeExit").toBool();
    plan.enterDelay = std::clamp(instance->settings()->get("MapEnterDelay").toInt(), 0, 60);

    const auto traits = traitsFor(instance);
    const bool traitSaysQuickPlay = traits.contains(QStringLiteral("feature:is_quick_play_singleplayer"));
    // traits 为空 = 组件尚未解析（命令行 / 快捷方式冷启动）。这一刻不能就此判「不支持 quickPlay」：
    // 1.20+ 会一路掉进下面的 Agent 分支，而 1.21.1 恰好有一张符号表 —— 结果是把旧版 Agent
    // 注进新版游戏（2026-11-08 复查代码时发现，见 06 §50）。所以只在「trait 缺失且 traits 为空」
    // 时用版本表兜底；traits 有内容就以它为准（Forge/NeoForge 派生版本的 trait 可能和原版不同）。
    const bool versionSaysQuickPlay = traits.isEmpty() && DataVersionTable::supportsQuickPlay(meta.version.mcVersion);
    if (traitSaysQuickPlay || versionSaysQuickPlay) {
        if (!plan.autoEnter) {
            plan.track = Track::Plain;
            plan.reason = QStringLiteral("已关闭「自动跳过主菜单」，将从主菜单启动");
            return plan;
        }
        plan.track = Track::QuickPlay;
        plan.reason = traitSaysQuickPlay
                          ? QStringLiteral("该版本原生支持 --quickPlaySingleplayer，直接进图")
                          : QStringLiteral("实例组件尚未解析，按版本表判定 %1 支持 quickPlay，直接进图")
                                .arg(meta.version.mcVersion);
        return plan;
    }

    if (!AgentBootstrap::hasSymbols(meta.version.mcVersion)) {
        plan.track = Track::Plain;
        plan.reason = QStringLiteral("该版本（%1）没有离线符号表，将停在主菜单，请手动选择存档")
                          .arg(meta.version.mcVersion);
        plan.warnings.append(plan.reason);
        return plan;
    }
    if (!AgentBootstrap::hasAgentJar()) {
        plan.track = Track::Plain;
        plan.reason = QStringLiteral("缺少 Agent jar，无法自动进图");
        plan.warnings.append(plan.reason);
        return plan;
    }

    QString error;
    const auto symbols = AgentBootstrap::loadSymbols(meta.version.mcVersion, &error);
    if (!symbols) {
        plan.track = Track::Plain;
        plan.reason = QStringLiteral("符号表加载失败：%1").arg(error);
        plan.warnings.append(plan.reason);
        return plan;
    }

    AgentBootstrap::AgentOptions options;
    options.worldFolder = plan.worldFolder;
    options.enterDelay = plan.enterDelay;
    options.autoEnter = plan.autoEnter;
    options.closeOnQuit = plan.closeOnQuit;
    const auto args = AgentBootstrap::buildAgentArgs(*symbols, options);
    if (args.args.isEmpty()) {
        plan.track = Track::Plain;
        plan.reason = args.skipReason.isEmpty() ? QStringLiteral("无法生成 Agent 参数") : args.skipReason;
        plan.warnings.append(plan.reason);
        return plan;
    }

    plan.track = Track::Agent;
    plan.canRequestQuit = args.canRequestQuit;
    plan.reason = plan.autoEnter ? QStringLiteral("该版本不支持 quickPlay，将通过 Java Agent 自动进图")
                                 : QStringLiteral("已关闭「自动跳过主菜单」，Agent 仍会注入（用于落盘判定）");
    return plan;
}

Result<> MapLaunchPrep::prepare(MinecraftInstance* instance, const MapMeta& meta, Plan* appliedPlan,
                                const QString& worldFolder)
{
    if (instance == nullptr)
        return std::unexpected{ QStringLiteral("实例不存在") };

    auto plan = MapLaunchPrep::plan(instance, meta, worldFolder);
    plan.applicable = true;

    // appliedPlan 必须反映**实际**结果：中途失败时不能再让调用方以为进了 Agent 轨道
    const auto finish = [&plan, appliedPlan](Result<> result) {
        if (appliedPlan != nullptr)
            *appliedPlan = plan;
        return result;
    };

    if (plan.worldFolder.isEmpty())
        return finish(std::unexpected{ plan.reason });

    // 每次启动前清掉上一次的退出标记，否则退出判定会读到旧结论（见 clearAgentLog）
    clearAgentLog(instance->instanceRoot());

    const SettingsObject::Lock lock(instance->settings());

    switch (plan.track) {
        case Track::QuickPlay: {
            // Prism 启动时读的是 MinecraftTarget->world（见 MinecraftInstance::processMinecraftArgs），
            // 这里写设置只是为了设置页回填与「创建快捷方式」能带上这张图。
            instance->settings()->set("JoinWorldOnLaunch", plan.worldFolder);
            tryInjectIntroSkip(instance, meta, &plan);
            return finish({});
        }

        case Track::Agent: {
            QString error;
            const auto symbols = AgentBootstrap::loadSymbols(meta.version.mcVersion, &error);
            if (!symbols) {
                plan.track = Track::Plain;
                plan.reason = error;
                return finish(std::unexpected{ error });
            }

            // Agent 自己写日志文件：Forge/FML 会把 stdout 重定向进 log4j，管道收不到标记
            const QString logPath = FS::PathCombine(instance->instanceRoot(), QStringLiteral("agent.log"));
            AgentBootstrap::AgentOptions options;
            options.worldFolder = plan.worldFolder;
            options.enterDelay = plan.enterDelay;
            options.agentLogPath = logPath;
            options.autoEnter = plan.autoEnter;
            options.closeOnQuit = plan.closeOnQuit;
            const auto args = AgentBootstrap::buildAgentArgs(*symbols, options);
            if (args.args.isEmpty()) {
                plan.track = Track::Plain;
                plan.reason = args.skipReason;
                return finish(std::unexpected{ args.skipReason });
            }

            // quickPlay 与 Agent 二选一：老版本不认这个设置，留着反而会误导
            instance->settings()->set("JoinWorldOnLaunch", QString());
            const auto injected = AgentBootstrap::injectIntoJvmArgs(instance, args.args);
            if (!injected) {
                plan.track = Track::Plain;
                plan.reason = injected.error();
            }
            tryInjectIntroSkip(instance, meta, &plan);
            return finish(injected);
        }

        case Track::Plain:
            break;
    }

    // 普通启动：明确不自动进图，避免"上次设置还留着"
    instance->settings()->set("JoinWorldOnLaunch", QString());
    return finish(std::unexpected{ plan.reason });
}

MapLaunchPrep::Plan MapLaunchPrep::prepareForLaunch(MinecraftInstance* instance, const QString& worldFolder)
{
    Plan plan;
    if (instance == nullptr)
        return plan;

    // 每次启动先清空上一次的运行时注入：属性不会落盘，但同一个实例对象会复用 ——
    // 不清的话，从地图实例切到「普通启动」时 agent 还会被带上。
    AgentBootstrap::clearMapAgents(instance);

    const QString metaPath = FS::PathCombine(instance->instanceRoot(), MapMeta::fileName());
    if (!QFile::exists(metaPath))
        return plan;  // 不是地图实例：一行都不碰，普通实例的启动路径保持原样

    plan.applicable = true;

    // 「进入地图」按的是第二个存档时，主窗口会把世界名记在实例属性上 ——
    // 真正的准备工作发生在 Application::launch 里，拿不到那个参数，所以在这里取一次。
    QString requested = worldFolder;
    if (requested.isEmpty())
        requested = instance->property("mapRequestedWorld").toString();
    instance->setProperty("mapRequestedWorld", QVariant());  // 一次性，别污染下一次启动

    QString error;
    const auto meta = MapMeta::load(instance->instanceRoot(), &error);
    if (meta.worlds.isEmpty()) {
        plan.track = Track::Plain;
        plan.reason = error.isEmpty() ? QStringLiteral("map.json 里没有记录世界目录，无法自动进图") : error;
        installExitTracker(instance);
        return plan;
    }

    // prepare() 内部已经清过一次 agent.log；这里再清是对「meta 读不出来」分支的兜底
    clearAgentLog(instance->instanceRoot());


    // prepare 会通过 appliedPlan 回写**实际**结果（失败时 track 已退化成 Plain），
    // 所以这里的返回值只用来留一条日志，不作为控制流。
    const auto applied = prepare(instance, meta, &plan, requested);
    plan.applicable = true;
    // 这行是排查「点了启动却停在主菜单」的唯一线索：track 决定走 quickPlay / Agent / 普通启动，
    // reason 说明为什么。没有它只能靠猜（见 06 变更记录）。
    qDebug() << "Map launch plan:" << instance->id() << "track=" << static_cast<int>(plan.track) << "world=" << plan.worldFolder
             << "reason=" << plan.reason;
    if (!applied)
        qDebug() << "Map launch: 无法自动进图 —" << applied.error() << "（实例" << instance->id() << "）";
    installExitTracker(instance);
    return plan;
}

void MapLaunchPrep::clearAgentLog(const QString& instanceRoot)
{
    const QString path = FS::PathCombine(instanceRoot, QStringLiteral("agent.log"));
    if (QFile::exists(path) && !QFile::remove(path))
        qWarning() << "MapLaunchPrep: 无法删除上次的 agent.log" << path;
}

void MapLaunchPrep::installExitTracker(MinecraftInstance* instance)
{
    if (instance == nullptr || instance->property("mapExitHookInstalled").toBool())
        return;
    instance->setProperty("mapExitHookInstalled", true);

    QObject::connect(instance, &BaseInstance::runningStatusChanged, instance, [instance](bool running) {
        if (running) {
            // 以「真正开始运行」为计时起点：prepare 之后用户可能还会取消启动
            instance->setProperty("mapLaunchStartedAt", QDateTime::currentDateTimeUtc());
            return;
        }

        const auto started = instance->property("mapLaunchStartedAt").toDateTime();
        const qint64 seconds = started.isValid() ? started.secsTo(QDateTime::currentDateTimeUtc()) : -1;
        // Agent 标记优先；quickPlay 轨道没有 Agent，用启动器的崩溃判定兜底
        const QString status = MapLaunchPrep::resolveExitStatus(instance->instanceRoot(), instance->hasCrashed());
        MapLaunchPrep::recordExit(instance->instanceRoot(), status, seconds);
        // 记录完就作废，避免下一次「启动失败」也按上次的时间算时长
        instance->setProperty("mapLaunchStartedAt", QVariant());
    });
}

QString MapLaunchPrep::detectExitStatus(const QString& instanceRoot)
{
    // Agent 自己写日志文件（Forge 会把 stdout 重定向进 log4j，管道收不到标记），
    // 所以退出后直接读文件尾部，比依赖进程退出码可靠。
    QFile log(FS::PathCombine(instanceRoot, QStringLiteral("agent.log")));
    if (!log.open(QIODevice::ReadOnly))
        return QStringLiteral("unknown");

    const qint64 keep = 8192;
    log.seek(std::max<qint64>(0, log.size() - keep));
    const QString tail = QString::fromUtf8(log.readAll());

    // 以**最后出现的那条**标记为准：同一次运行里可能先超时、后落盘成功，
    // 只判断「包含」会把结论写反。
    const qsizetype safeAt = tail.lastIndexOf(QLatin1String("EXIT_SAFE"));
    const qsizetype unsafeAt = tail.lastIndexOf(QLatin1String("EXIT_UNSAFE_TIMEOUT"));
    if (safeAt < 0 && unsafeAt < 0)
        return QStringLiteral("unknown");
    return safeAt > unsafeAt ? QStringLiteral("safe") : QStringLiteral("crash");
}

QString MapLaunchPrep::resolveExitStatus(const QString& instanceRoot, bool crashed)
{
    const QString fromAgent = detectExitStatus(instanceRoot);
    if (fromAgent != QLatin1String("unknown"))
        return fromAgent;

    // 没有 Agent 标记（quickPlay 轨道不注入，或 Agent 没跑起来）：
    // 崩塌与否只有启动器自己知道，用它兜底。
    return crashed ? QStringLiteral("crash") : QStringLiteral("safe");
}

void MapLaunchPrep::recordExit(const QString& instanceRoot, const QString& status, qint64 playedSeconds)
{
    QString error;
    auto meta = MapMeta::load(instanceRoot, &error);
    if (!meta.title.isEmpty() || !meta.worlds.isEmpty()) {
        // unknown 不覆盖结论：Agent 没留标记不等于没正常退出，
        // 把上一次的 safe 抹成"未知"会让玩家以为存档有问题
        if (status != QLatin1String("unknown"))
            meta.stats.lastExit = status;
        meta.stats.lastPlayed = QDateTime::currentDateTimeUtc();
        if (playedSeconds >= 0)
            meta.stats.playSeconds += playedSeconds;
        meta.save(instanceRoot, &error);
    }
    if (!error.isEmpty())
        qWarning() << "MapLaunchPrep::recordExit:" << error;
}

}  // namespace maps
