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

QString introSkipJarTargetPath()
{
    return FS::PathCombine(dataRoot(), "introskip.jar");
}

/// 把随包的 jar 释放到数据目录（-javaagent 只认真实文件，读不了 qrc）
bool copyBundledJar(const QString& resource, const QString& target)
{
    QFile bundled(resource);
    if (!bundled.exists())
        return false;

    if (!FS::ensureFolderPathExists(dataRoot()))
        return false;
    if (!bundled.open(QIODevice::ReadOnly))
        return false;

    const QByteArray bundledBytes = bundled.readAll();
    if (QFile::exists(target)) {
        QFile existing(target);
        if (existing.open(QIODevice::ReadOnly) && existing.readAll() == bundledBytes)
            return true;
    }

    QFile out(target);
    if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return false;
    out.write(bundledBytes);
    return true;
}

bool ensureAgentJar()
{
    return copyBundledJar(QStringLiteral(":/agents/quickboot.jar"), jarTargetPath());
}

bool ensureIntroSkipJar()
{
    return copyBundledJar(QStringLiteral(":/agents/introskip.jar"), introSkipJarTargetPath());
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

bool AgentBootstrap::hasIntroSkipJar()
{
    ensureAgentsResources();
    return QFile::exists(introSkipJarTargetPath()) || QFile::exists(QStringLiteral(":/agents/introskip.jar"));
}

QString AgentBootstrap::introSkipJarPath()
{
    return introSkipJarTargetPath();
}

std::optional<IntroSkipRule> AgentBootstrap::introSkipRule(const QString& mcVersion, QString* error)
{
    ensureAgentsResources();
    if (mcVersion.isEmpty()) {
        if (error != nullptr)
            *error = QStringLiteral("版本号为空");
        return std::nullopt;
    }

    QFile file(QStringLiteral(":/agents/introskip.json"));
    if (!file.open(QIODevice::ReadOnly)) {
        if (error != nullptr)
            *error = QStringLiteral("随包缺少 introskip.json");
        return std::nullopt;
    }

    QJsonParseError parseError{};
    const auto document = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError) {
        if (error != nullptr)
            *error = QStringLiteral("introskip.json 不是合法 JSON：%1").arg(parseError.errorString());
        return std::nullopt;
    }

    const auto item = document.object().value(QStringLiteral("versions")).toObject().value(mcVersion).toObject();
    IntroSkipRule rule;
    rule.className = item.value(QStringLiteral("class")).toString();
    rule.method = item.value(QStringLiteral("method")).toString();
    rule.descriptor = item.value(QStringLiteral("desc")).toString();
    rule.loadLevelSlot = item.value(QStringLiteral("index")).toInt(3);
    rule.defer = item.value(QStringLiteral("defer")).toBool(false);
    if (rule.className.isEmpty() || rule.method.isEmpty() || rule.descriptor.isEmpty()) {
        if (error != nullptr)
            *error = QStringLiteral("%1 没有「实验性设置确认页」的签名（只影响少点一次，不影响进图）").arg(mcVersion);
        return std::nullopt;
    }
    return rule;
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
    // 与 AIO 的差别（docs/atlas 决策表）：让 JVM 在确认落盘后自己退出。
    // 启动器侧只有「进程退出」概念，没有「落盘完成」，宿主 kill 的时机只能靠猜。
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

namespace {

constexpr const char* MAP_AGENT_PROPERTY = "mapsJavaAgents";

QString agentArgument(const QString& jarPath, const QString& agentArgs)
{
    const QString jar = QDir::toNativeSeparators(jarPath);
    return agentArgs.isEmpty() ? QStringLiteral("-javaagent:%1").arg(jar)
                               : QStringLiteral("-javaagent:%1=%2").arg(jar, agentArgs);
}

/// 换掉同一个 jar 的旧条目，再放新的。
/// 直接作为 argv 元素交给 JVM：不经 JvmArgs 的字符串拼接与二次切分，也不用引号来回转义。
void setMapAgent(MinecraftInstance* instance, const QString& jarPath, const QString& agentArgs)
{
    const QString prefix = QStringLiteral("-javaagent:%1=").arg(QDir::toNativeSeparators(jarPath));
    QStringList agents = instance->property(MAP_AGENT_PROPERTY).toStringList();
    for (int i = agents.size() - 1; i >= 0; --i) {
        if (agents.at(i).startsWith(prefix))
            agents.removeAt(i);
    }
    agents.append(agentArgument(jarPath, agentArgs));
    instance->setProperty(MAP_AGENT_PROPERTY, agents);
}

}  // namespace

Result<> AgentBootstrap::injectIntoJvmArgs(MinecraftInstance* instance, const QString& agentArgs)
{
    if (instance == nullptr)
        return std::unexpected{ QStringLiteral("实例为空，无法注入 Agent") };
    if (agentArgs.isEmpty())
        return std::unexpected{ QStringLiteral("Agent 参数为空") };
    if (!ensureAgentJar())
        return std::unexpected{ QStringLiteral("找不到 Agent jar（resources/agents/quickboot.jar）") };

    setMapAgent(instance, jarTargetPath(), agentArgs);
    return {};
}

Result<> AgentBootstrap::injectIntroSkip(MinecraftInstance* instance, const IntroSkipRule& rule, const QString& logPath)
{
    if (instance == nullptr)
        return std::unexpected{ QStringLiteral("实例为空，无法注入 Agent") };
    if (!ensureIntroSkipJar())
        return std::unexpected{ QStringLiteral("找不到 introskip.jar（resources/agents/introskip.jar）") };

    QString args = QStringLiteral("cls=%1,method=%2,desc=%3,index=%4")
                       .arg(rule.className, rule.method, rule.descriptor)
                       .arg(rule.loadLevelSlot);
    if (rule.defer)
        args += QStringLiteral(",defer=1");
    if (!logPath.isEmpty())
        args += QStringLiteral(",log=%1").arg(QDir::toNativeSeparators(logPath));

    setMapAgent(instance, introSkipJarTargetPath(), args);
    return {};
}

Result<> AgentBootstrap::clearIntroSkip(MinecraftInstance* instance)
{
    if (instance == nullptr)
        return {};

    const QString prefix = QStringLiteral("-javaagent:%1=").arg(QDir::toNativeSeparators(introSkipJarTargetPath()));
    QStringList agents = instance->property(MAP_AGENT_PROPERTY).toStringList();
    const int before = agents.size();
    for (int i = agents.size() - 1; i >= 0; --i) {
        if (agents.at(i).startsWith(prefix))
            agents.removeAt(i);
    }
    if (agents.size() != before)
        instance->setProperty(MAP_AGENT_PROPERTY, agents);
    return {};
}

QString AgentBootstrap::introSkipLogPath(MinecraftInstance* instance)
{
    if (instance == nullptr)
        return {};
    // 用实例 id 的哈希做文件名：目录名可能是中文/希腊字母，但日志路径必须保持 ASCII
    const QString key = QString::number(qHash(instance->id()), 16);
    return FS::PathCombine(dataRoot(), QStringLiteral("introskip-%1.log").arg(key));
}

QStringList AgentBootstrap::mapAgents(MinecraftInstance* instance)
{
    if (instance == nullptr)
        return {};
    return instance->property(MAP_AGENT_PROPERTY).toStringList();
}

void AgentBootstrap::clearMapAgents(MinecraftInstance* instance)
{
    if (instance == nullptr)
        return;
    instance->setProperty(MAP_AGENT_PROPERTY, QStringList());
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
