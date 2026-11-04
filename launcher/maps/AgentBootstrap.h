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
#pragma once

#include <QHash>
#include <QObject>
#include <QString>
#include <QStringList>

#include <optional>

#include "Result.h"

class LaunchTask;
class MinecraftInstance;

namespace maps {

/// 一个类槽位（menuClass / minecraft …）
struct AgentClassSlot {
    QString obf;
    QString name;
};

/// 一个方法槽位（menuInit / enterWorld …）
struct AgentMethodSlot {
    QString owner;
    QString name;
    QString descriptor;
};

/**
 * 离线符号表：由 AIO 的 `symbols` 命令预先算好，随包发布，运行时只读。
 *
 * 为什么不在运行时解析：要扫版本 jar、算描述符，秒级开销且版本一多就散；
 * 而且解析器的坑（ProGuard / Forge SRG / Fabric intermediary）已经在 AIO 里踩完了。
 */
struct AgentSymbolTable {
    QString version;
    QHash<QString, AgentClassSlot> classes;
    QHash<QString, AgentMethodSlot> methods;

    bool isEmpty() const { return classes.isEmpty() || methods.isEmpty(); }
};

/**
 * ≤1.19.4 的「直连存档」：拼 -javaagent 参数 + 消费 Agent 事件流。
 *
 * 三条原则（照抄 AIO 的结论，别自己发明）：
 *   1. 参数全部来自符号表，一个混淆名都不许手抄；
 *   2. 拿不准就不注入 —— 符号表缺槽位／jar 不存在时退回普通启动，并说明原因；
 *   3. 事件走 Agent 自己的日志文件，不解析游戏日志（Forge 会把 stdout 重定向进 log4j）。
 */
/**
 * 1.20+ 的「实验性设置」确认页闸门：`WorldOpenFlows.askForBackup(...)`
 * （方法会弹「创建备份并加载 / 我知道我在做什么 / 取消」，quickPlay 会停在这一页）。
 *
 * 类名与方法名逐版本混淆，且 1.20~1.20.2 的签名不同（loadLevel 在第 4 个局部变量），
 * 所以 cls/method/desc/slot 全部来自随包的 `:/agents/introskip.json`（由官方映射离线生成）。
 */
struct IntroSkipRule {
    QString className;
    QString method;
    QString descriptor;
    int loadLevelSlot = 3;
};

class AgentBootstrap : public QObject {
    Q_OBJECT

   public:
    enum class Event { EnteredWorld, SaveDone, Menu2Seen, ExitSafe, ExitUnsafeTimeout, Unknown };

    /// 某个 MC 版本是否有可用符号表（`:/agents/symbols/<ver>.json`）
    static bool hasSymbols(const QString& mcVersion);
    static std::optional<AgentSymbolTable> loadSymbols(const QString& mcVersion, QString* error = nullptr);
    /// agent jar 是否随包存在
    static bool hasAgentJar();
    static QString agentJarPath();

    /// 某个 MC 版本是否有「跳过实验性设置确认页」的签名（`:/agents/introskip.json`）
    static std::optional<IntroSkipRule> introSkipRule(const QString& mcVersion, QString* error = nullptr);
    static bool hasIntroSkipJar();
    static QString introSkipJarPath();

    struct ArgsResult {
        QString args;      // k=v,k=v ；为空表示不能注入
        QStringList missing;
        bool canRequestQuit = false;
        QString skipReason;
    };

    /// 符号表 -> Agent 的 `k=v,k=v` 参数串（缺槽位时 missing 非空，不猜）
    struct AgentOptions {
        QString worldFolder;
        int enterDelay = 1;
        QString agentLogPath;
        QString quitSignalPath;
        /// 关掉「自动进图」时仍然注入（落盘判定要用），只是不替玩家进存档
        bool autoEnter = true;
        /// 关掉「退出即关游戏」时不 halt，让 JVM 留在标题画面
        bool closeOnQuit = true;
    };

    static ArgsResult buildAgentArgs(const AgentSymbolTable& table, const AgentOptions& options);

    /// 把 `-javaagent:<jar>=<args>` 追加进实例的 JvmArgs（JvmArgs 是追加语义，不会顶掉内存/GC 参数）
    static Result<> injectIntoJvmArgs(MinecraftInstance* instance, const QString& agentArgs);

    /// 1.20+：注入 introskip agent（把确认页那道闸门改成直接加载世界）
    static Result<> injectIntroSkip(MinecraftInstance* instance, const IntroSkipRule& rule, const QString& logPath);

    /// 把上一次注入的 introskip 摘掉（关掉设置项后不能让它继续生效）
    static Result<> clearIntroSkip(MinecraftInstance* instance);

    /// introskip 的日志路径。**必须是纯 ASCII**：JVM 的 sun.jnu.encoding 在中文
    /// Windows 上是 GBK，实例目录名里的 `Ξ` 这类字符会让 FileWriter 直接找不到路径。
    static QString introSkipLogPath(MinecraftInstance* instance);

    /**
     * 运行时注入的 `-javaagent` 列表（存在实例的 QObject 属性上，不落盘）。
     *
     * 为什么不写 `JvmArgs`：那是 override 设置（`OverrideJavaArgs`），未开启覆盖时
     * `settings()->get("JvmArgs")` 直接回落到全局值，实例里写的值根本读不到 ——
     * 之前 quickboot 与 introskip 都栽在这里（参数写进去了，但从来没进过命令行）。
     */
    static QStringList mapAgents(MinecraftInstance* instance);
    /// 清掉所有运行时注入（每次启动前调一次，普通启动与非地图实例都不该带 agent）
    static void clearMapAgents(MinecraftInstance* instance);

    /// 从一行日志里识别 Agent 事件
    static Event parseEvent(const QString& line, int* saveSequence = nullptr);

    explicit AgentBootstrap(QObject* parent = nullptr);

    /// 挂到 LaunchTask 的日志模型上，边跑边识别事件
    void attach(LaunchTask* task);

   signals:
    void enteredWorld();
    void saveCompleted(int sequence);
    void menuSeenAgain();
    void exitSafe();
    void exitUnsafeTimeout();

   private:
    QObject* m_context = nullptr;
};

}  // namespace maps
