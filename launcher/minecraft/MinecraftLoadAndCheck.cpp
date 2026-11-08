#include "MinecraftLoadAndCheck.h"
#include "Application.h"
#include "MinecraftInstance.h"
#include "PackProfile.h"

MinecraftLoadAndCheck::MinecraftLoadAndCheck(MinecraftInstance* inst, Net::Mode netmode) : m_inst(inst), m_netmode(netmode) {}

void MinecraftLoadAndCheck::executeTask()
{
    // add offline metadata load task
    auto* components = m_inst->getPackProfile();
    if (m_netmode == Net::Mode::Online && m_inst->settings()->get("UseLatestMinecraftVersion").toBool()) {
        auto releaseType = m_inst->settings()->get("UseLatestMinecraftVersionType").toString();
        if (components->updateLatestMinecraft(releaseType == "release") && APPLICATION->settings()->get("AutomaticJavaSwitch").toBool() &&
            m_inst->settings()->get("AutomaticJava").toBool() && m_inst->settings()->get("OverrideJavaLocation").toBool()) {
            m_inst->settings()->set("OverrideJavaLocation", false);
            m_inst->settings()->set("JavaPath", "");
        }
    }
    // 启动时的组件加载：对于已配置好版本的实例，默认优先以本地离线模式（Offline）加载，
    // 避免每次启动都向境外服务器重复拉取 index.json 导致阻塞卡死。
    // 只有在开启自动更新或本地元数据确实缺失时才向远端拉取。
    auto preferredMode = Net::Mode::Offline;
    if (m_netmode == Net::Mode::Online &&
        (m_inst->settings()->get("UseLatestMinecraftVersion").toBool() ||
         APPLICATION->settings()->get("MetaRefreshOnLaunch").toBool())) {
        preferredMode = Net::Mode::Online;
    }

    if (auto result = components->reload(preferredMode); !result) {
        if (preferredMode == Net::Mode::Offline && m_netmode == Net::Mode::Online) {
            if (auto retry = components->reload(Net::Mode::Online); !retry) {
                emitFailed(retry.error());
                return;
            }
        } else {
            emitFailed(result.error());
            return;
        }
    }
    m_task = components->getCurrentTask();

    if (!m_task) {
        emitSucceeded();
        return;
    }
    connect(m_task.get(), &Task::succeeded, this, &MinecraftLoadAndCheck::emitSucceeded);
    connect(m_task.get(), &Task::failed, this, [this](const QString& err) {
        if (m_netmode == Net::Mode::Online) {
            auto* comps = m_inst->getPackProfile();
            if (auto retry = comps->reload(Net::Mode::Online); retry) {
                m_task = comps->getCurrentTask();
                if (m_task) {
                    connect(m_task.get(), &Task::succeeded, this, &MinecraftLoadAndCheck::emitSucceeded);
                    connect(m_task.get(), &Task::failed, this, &MinecraftLoadAndCheck::emitFailed);
                    connect(m_task.get(), &Task::aborted, this, &MinecraftLoadAndCheck::emitAborted);
                    propagateFromOther(m_task.get());
                    return;
                }
                emitSucceeded();
                return;
            }
        }
        emitFailed(err);
    });
    connect(m_task.get(), &Task::aborted, this, &MinecraftLoadAndCheck::emitAborted);
    propagateFromOther(m_task.get());
}

bool MinecraftLoadAndCheck::canAbort() const
{
    if (m_task) {
        return m_task->canAbort();
    }
    return true;
}

bool MinecraftLoadAndCheck::abort()
{
    if (m_task && m_task->canAbort()) {
        return m_task->abort();
    }
    return Task::abort();
}
