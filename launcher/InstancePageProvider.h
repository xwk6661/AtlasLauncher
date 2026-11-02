#pragma once
#include <FileSystem.h>
#include "Application.h"
#include "settings/SettingsObject.h"
#include <ui/pages/instance/DataPackPage.h>
#include "minecraft/MinecraftInstance.h"
#include "ui/pages/BasePage.h"
#include "ui/pages/BasePageProvider.h"
#include "ui/pages/instance/InstanceSettingsPage.h"
#include "ui/pages/instance/LogPage.h"
#include "ui/pages/instance/ManagedPackPage.h"
#include "ui/pages/instance/ModFolderPage.h"
#include "ui/pages/instance/NotesPage.h"
#include "ui/pages/instance/OtherLogsPage.h"
#include "ui/pages/instance/ResourcePackPage.h"
#include "ui/pages/instance/ScreenshotsPage.h"
#include "ui/pages/instance/ServersPage.h"
#include "ui/pages/instance/ShaderPackPage.h"
#include "ui/pages/instance/TexturePackPage.h"
#include "ui/pages/instance/VersionPage.h"
#include "ui/pages/instance/WorldListPage.h"

#include "maps/MapInstancePage.h"

class InstancePageProvider : protected QObject, public BasePageProvider {
    Q_OBJECT
   public:
    explicit InstancePageProvider(MinecraftInstance* parent) { inst = parent; }

    virtual ~InstancePageProvider() = default;
    virtual QList<BasePage*> getPages() override
    {
        auto* settings = APPLICATION->settings();

        /**
         * 换地图即换实例：一个实例只有一个存档目录，所以「世界」「服务器」「备注」
         * 「其他日志」这四页在地图启动器里几乎没有用武之地，默认收起，只留
         * 日志 / 版本 / 模组 / 材质 / 光影 / 信息 / 截图 / 设置。
         *
         * 这里直接不构造页面（而不是 shouldDisplay 返回 false）：页面本身会建
         * 文件监听、世界扫描这些活，收起的页不该在后台跑。
         * 开关在「文件 → 地图设置 → 控制台页签」。
         */
        const bool showNotes = settings->get("MapConsoleShowNotes").toBool();
        const bool showWorlds = settings->get("MapConsoleShowWorlds").toBool();
        const bool showServers = settings->get("MapConsoleShowServers").toBool();
        const bool showOtherLogs = settings->get("MapConsoleShowOtherLogs").toBool();

        QList<BasePage*> values;
        /**
         * 「设置」置顶。
         *
         * 玩家打开控制台，十次有九次是去改 Java / 内存 / 窗口大小这类东西；
         * 日志是在出问题时的第二落点。所以把设置放到列表第一项，并让窗口
         * 默认选中它（见 InstanceWindow 里的 defaultId）。
         */
        values.append(new InstanceSettingsPage(inst));
        values.append(new LogPage(inst));
        values.append(new VersionPage(inst));
        values.append(ManagedPackPage::createPage(inst));
        auto modsPage = new ModFolderPage(inst, inst->loaderModList());
        modsPage->setFilter("%1 (*.zip *.jar *.litemod *.nilmod)");
        values.append(modsPage);
        values.append(new CoreModFolderPage(inst, inst->coreModList()));
        values.append(new NilModFolderPage(inst, inst->nilModList()));
        values.append(new ResourcePackPage(inst, inst->resourcePackList()));
        values.append(new GlobalDataPackPage(inst));
        values.append(new TexturePackPage(inst, inst->texturePackList()));
        values.append(new ShaderPackPage(inst, inst->shaderPackList()));
        if (showNotes)
            values.append(new NotesPage(inst));
        // 地图实例才有这一页（shouldDisplay 里有 map.json 判定）
        values.append(new maps::MapInstancePage(inst));
        if (showWorlds)
            values.append(new WorldListPage(inst, inst->worldList()));
        if (showServers)
            values.append(new ServersPage(inst));
        values.append(new ScreenshotsPage(FS::PathCombine(inst->gameRoot(), "screenshots")));
        if (showOtherLogs)
            values.append(new OtherLogsPage("logs", tr("其他日志"), "Other-Logs", inst));
        return values;
    }

    virtual QString dialogTitle() override { return tr("Edit Instance (%1)").arg(inst->name()); }

   protected:
    MinecraftInstance* inst;
};
