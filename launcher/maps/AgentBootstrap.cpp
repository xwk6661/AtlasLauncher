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
#include "AgentBootstrap.h"

#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QStandardPaths>

#include <algorithm>

#include "Commandline.h"
#include "FileSystem.h"
#include "launch/LaunchTask.h"
#include "launch/LogModel.h"
#include "minecraft/MinecraftInstance.h"
#include "settings/SettingsObject.h"

// 静态库里的 qrc 不会自动注册（与 maps.qrc 同理）。
// Q_INIT_RESOURCE 必须在全局作用域展开，否则会声明成 maps::qInitResources_agents。
static void ensureAgentsResources()
{
    static bool done = false;
    if (done)
        return;
    done = true;
    Q_INIT_RESOURCE(agents);
}

namespace maps {
namespace {

/// Agent 打在日志里的标记前缀
constexpr const char* AGENT_MARKER = "[mcml-agent]";

QString dataRoot()
{
    const QString path = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    return FS::PathCombine(path, "agents");
}

QString jarTargetPath()
{
    return FS::PathCombine(dataRoot(), "quickboot.jar");
}

/// 把随包的 jar 释放到数据目录（-javaagent 只认真实文件，读不了 qrc）
bool ensureAgentJar()
{
    const QString target = jarTargetPath();
    QFile bundled(QStringLiteral(":/agents/quickboot.jar"));
    if (!bundled.exists())
        return false;

    // 启动器升级后随包的 jar 会变。只比大小不够严谨，但足以区分「没变」与「换了」，
    // 且不需要每次启动都做一次 SHA 校验。
    if (QFile::exists(target) && QFileInfo(target).size() == bundled.size())
        return true;

    if (!FS::ensureFolderPathExists(dataRoot()))
        return false;
    if (!bundled.open(QIODevice::ReadOnly))
        return false;

    QFile out(target);
    if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return false;
    out.write(bundled.readAll());
    return true;
}

QString dotted(const QString& internalName)
{
    QString out = internalName;
    out.replace(QLatin1Char('/'), QLatin1Char('.'));
    return out;
}

}  // namespace

bool AgentBootstrap::hasAgentJar()
{
    ensureAgentsResources();
    return QFile::exists(jarTargetPath()) || QFile::exists(QStringLiteral(":/agents/quickboot.jar"));
}

QString AgentBootstrap::agentJarPath()
{
    return jarTargetPath();
}

bool AgentBootstrap::hasSymbols(const QString& mcVersion)
{
    ensureAgentsResources();
    if (mcVersion.isEmpty())
        return false;
    return QFile::exists(QStringLiteral(":/agents/symbols/%1.json").arg(mcVersion));
}

std::optional<AgentSymbolTable> AgentBootstrap::loadSymbols(const QString& mcVersion, QString* error)
{
    ensureAgentsResources();
    const QString path = QStringLiteral(":/agents/symbols/%1.json").arg(mcVersion);
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        if (error != nullptr)
            *error = QStringLiteral("没有 %1 的符号表").arg(mcVersion);
        return std::nullopt;
    }

    QJsonParseError parseError{};
    const auto document = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError) {
        if (error != nullptr)
            *error = QStringLiteral("符号表不是合法 JSON：%1").arg(parseError.errorString());
        return std::nullopt;
    }

    const auto root = document.object();
    AgentSymbolTable table;
    // AIO 生成的表用 mcVersion，早期版本用 version —— 两个都认
    const QString versionKey = root.contains(QStringLiteral("mcVersion")) ? QStringLiteral("mcVersion")
                                                                          : QStringLiteral("version");
    table.version = root.value(versionKey).toString(mcVersion);

    const auto classes = root.value(QStringLiteral("classes")).toObject();
    for (auto it = classes.constBegin(); it != classes.constEnd(); ++it) {
        const auto item = it.value().toObject();
        AgentClassSlot slot;
        slot.obf = item.value(QStringLiteral("obf")).toString();
        slot.name = item.value(QStringLiteral("name")).toString();
        if (!slot.obf.isEmpty())
            table.classes.insert(it.key(), slot);
    }

    const auto methods = root.value(QStringLiteral("methods")).toObject();
    for (auto it = methods.constBegin(); it != methods.constEnd(); ++it) {
        const auto item = it.value().toObject();
        AgentMethodSlot slot;
        slot.owner = item.value(QStringLiteral("owner")).toString();
        slot.name = item.value(QStringLiteral("name")).toString();
        slot.descriptor = item.value(QStringLiteral("descriptor")).toString();
        if (!slot.name.isEmpty() && !slot.descriptor.isEmpty())
            table.methods.insert(it.key(), slot);
    }

    if (table.isEmpty()) {
        if (error != nullptr)
            *error = QStringLiteral("符号表缺少 classes/methods");
        return std::nullopt;
    }
    return table;
}

AgentBootstrap::ArgsResult AgentBootstrap::buildAgentArgs(const AgentSymbolTable& table, const AgentOptions& options)
{
    ArgsResult result;

    // 必需槽位。setLevel 不在必需项里：1.7.10/1.12 解析不出来，但进图与玩家自己退出都不需要它
    static const QStringList requiredClasses{ QStringLiteral("menuClass"), QStringLiteral("minecraft") };
    static const QStringList requiredMethods{ QStringLiteral("menuInit"), QStringLiteral("enterWorld"),
                                              QStringLiteral("setScreen"), QStringLiteral("saveAllWorlds") };

    for (const auto& slot : requiredClasses) {
        if (!table.classes.contains(slot))
            result.missing.append(slot);
    }
    for (const auto& slot : requiredMethods) {
        if (!table.methods.contains(slot))
            result.missing.append(slot);
    }
    if (!result.missing.isEmpty()) {
        result.skipReason = QStringLiteral("符号表缺少槽位：%1").arg(result.missing.join(QStringLiteral(", ")));
        return result;
    }

    const auto menu = table.classes.value(QStringLiteral("menuClass"));
    const auto minecraft = table.classes.value(QStringLiteral("minecraft"));
    const auto menuInit = table.methods.value(QStringLiteral("menuInit"));
    const auto enter = table.methods.value(QStringLiteral("enterWorld"));
    const auto setScreen = table.methods.value(QStringLiteral("setScreen"));
    const auto save = table.methods.value(QStringLiteral("saveAllWorlds"));

    QStringList parts;
    parts << QStringLiteral("world=%1").arg(options.worldFolder);
    parts << QStringLiteral("menu=%1").arg(dotted(menu.obf));
    parts << QStringLiteral("menuMethod=%1").arg(menuInit.name);
    parts << QStringLiteral("menuDesc=%1").arg(menuInit.descriptor);
    parts << QStringLiteral("mc=%1").arg(dotted(minecraft.obf));
    parts << QStringLiteral("launch=%1").arg(enter.name);
    parts << QStringLiteral("enterDesc=%1").arg(enter.descriptor);
    parts << QStringLiteral("displayGui=%1").arg(setScreen.name);
    parts << QStringLiteral("saveClass=%1").arg(save.owner);
    parts << QStringLiteral("saveMethod=%1").arg(save.name);
    parts << QStringLiteral("saveDesc=%1").arg(save.descriptor);
    parts << QStringLiteral("enterDelay=%1").arg(std::clamp(options.enterDelay, 0, 60));
    parts << QStringLiteral("safeExit=1");
    // 与 AIO 的差别（docs/map-launcher 决策表）：让 JVM 在确认落盘后自己退出。
    // Prism 侧只有「进程退出」概念，没有「落盘完成」，宿主 kill 的时机只能靠猜。
    parts << QStringLiteral("haltOnSafe=%1").arg(options.closeOnQuit ? 1 : 0);
    // 关掉「自动进图」时仍然注入（落盘判定要用），只是不替玩家进存档
    if (!options.autoEnter)
        parts << QStringLiteral("autoEnter=0");

    const auto setLevel = table.methods.value(QStringLiteral("setLevel"));
    result.canRequestQuit = !setLevel.name.isEmpty();
    if (result.canRequestQuit) {
        parts << QStringLiteral("loadWorld=%1").arg(setLevel.name);
        parts << QStringLiteral("loadWorldDesc=%1").arg(setLevel.descriptor);
    }

    const auto stop = table.methods.value(QStringLiteral("stopServer"));
    if (!stop.name.isEmpty()) {
        parts << QStringLiteral("stopClass=%1").arg(stop.owner);
        parts << QStringLiteral("stopMethod=%1").arg(stop.name);
        parts << QStringLiteral("stopDesc=%1").arg(stop.descriptor);
    }

    if (!options.agentLogPath.isEmpty()) {
        // Agent 的参数按逗号切分 —— 路径里绝不能有逗号
        QString log = options.agentLogPath;
        log.remove(QLatin1Char(','));
        parts << QStringLiteral("log=%1").arg(log);
    }

    if (!options.quitSignalPath.isEmpty() && result.canRequestQuit) {
        parts << QStringLiteral("quitSignal=%1").arg(options.quitSignalPath);
        parts << QStringLiteral("quitSignalTimeout=86400000");
    }

    result.args = parts.join(QLatin1Char(','));
    return result;
}

Result<> AgentBootstrap::injectIntoJvmArgs(MinecraftInstance* instance, const QString& agentArgs)
{
    if (instance == nullptr)
        return std::unexpected{ QStringLiteral("实例为空，无法注入 Agent") };
    if (agentArgs.isEmpty())
        return std::unexpected{ QStringLiteral("Agent 参数为空") };
    if (!ensureAgentJar())
        return std::unexpected{ QStringLiteral("找不到 Agent jar（resources/agents/quickboot.jar）") };

    const QString jar = QDir::toNativeSeparators(jarTargetPath());

    // JvmArgs 会被 Commandline::splitArgs 按空格切成 argv，所以整条参数（含日志路径里的空格）
    // 必须当成一个 argv 元素交给 JVM —— 用 Prism 自己的引号工具，反斜杠才会被正确还原。
    const QString agentArg = QStringLiteral("-javaagent:%1=%2").arg(jar, agentArgs);
    const QString quoted = Commandline::quoteForSplitCommand(agentArg);

    const SettingsObject::Lock lock(instance->settings());
    const QString current = instance->settings()->get("JvmArgs").toString();
    QStringList parts = Commandline::splitArgs(current);

    // 每次启动的世界 / 延迟可能变了：摘掉上一次我们自己注入的那条再放新的。
    // 别人手动配的 -javaagent（路径不是我们的 jar）一律不碰，冲突时直接放弃注入。
    const QString ourPrefix = QStringLiteral("-javaagent:%1=").arg(jar);
    int existing = -1;
    for (int i = 0; i < parts.size(); ++i) {
        const QString& part = parts.at(i);
        if (!part.startsWith(QLatin1String("-javaagent:")))
            continue;
        if (part.startsWith(ourPrefix)) {
            existing = i;
            continue;
        }
        return std::unexpected{ QStringLiteral("实例已经配置了别的 -javaagent（%1），跳过自动注入以免冲突").arg(part) };
    }

    if (existing < 0) {
        // 没注入过：直接在原文后面追加，不动用户已有的写法
        instance->settings()->set("JvmArgs", current.isEmpty() ? quoted : current + QLatin1Char(' ') + quoted);
        return {};
    }

    // 注入过：重排一次（引号由 quoteForSplitCommand 负责，splitArgs 能原样读回来）
    parts[existing] = agentArg;
    QStringList rendered;
    rendered.reserve(parts.size());
    for (const auto& part : parts)
        rendered.append(Commandline::quoteForSplitCommand(part));
    instance->settings()->set("JvmArgs", rendered.join(QLatin1Char(' ')));
    return {};
}

AgentBootstrap::Event AgentBootstrap::parseEvent(const QString& line, int* saveSequence)
{
    if (!line.contains(QLatin1String(AGENT_MARKER)))
        return Event::Unknown;

    if (line.contains(QLatin1String("EXIT_SAFE")))
        return Event::ExitSafe;
    if (line.contains(QLatin1String("EXIT_UNSAFE_TIMEOUT")))
        return Event::ExitUnsafeTimeout;
    if (line.contains(QLatin1String("ENTER_WORLD_OK")))
        return Event::EnteredWorld;
    if (line.contains(QLatin1String("MENU_2_SEEN")))
        return Event::Menu2Seen;
    if (line.contains(QLatin1String("SAVE_DONE"))) {
        static const QRegularExpression sequence(QStringLiteral("SAVE_DONE #(\\d+)"));
        if (saveSequence != nullptr) {
            const auto match = sequence.match(line);
            *saveSequence = match.hasMatch() ? match.captured(1).toInt() : 0;
        }
        return Event::SaveDone;
    }
    return Event::Unknown;
}

AgentBootstrap::AgentBootstrap(QObject* parent) : QObject(parent) {}

void AgentBootstrap::attach(LaunchTask* task)
{
    if (task == nullptr)
        return;

    auto model = task->getLogModel();
    if (!model)
        return;

    m_context = task;
    // 逐行扫新增的日志：不解析游戏日志格式，只认 Agent 自己的标记
    connect(model.get(), &QAbstractItemModel::rowsInserted, this, [this, model](const QModelIndex&, int first, int last) {
        for (int row = first; row <= last; ++row) {
            const QString line = model->data(model->index(row, 0), Qt::DisplayRole).toString();
            int sequence = 0;
            switch (parseEvent(line, &sequence)) {
                case Event::EnteredWorld:
                    emit enteredWorld();
                    break;
                case Event::SaveDone:
                    emit saveCompleted(sequence);
                    break;
                case Event::Menu2Seen:
                    emit menuSeenAgain();
                    break;
                case Event::ExitSafe:
                    emit exitSafe();
                    break;
                case Event::ExitUnsafeTimeout:
                    emit exitUnsafeTimeout();
                    break;
                case Event::Unknown:
                    break;
            }
        }
    });
}

}  // namespace maps
