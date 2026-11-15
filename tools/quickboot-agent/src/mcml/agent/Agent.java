package mcml.agent;

import java.lang.instrument.Instrumentation;

/**
 * 注入型 Agent。
 *
 * 设计要点：**类名与方法名全部由参数传入**，不写死在代码里。
 * 因为原版 jar 是混淆的，同一套逻辑在不同版本对应不同的符号名；
 * 把符号名外置后，换版本只需要换参数，agent jar 本身不用重编译。
 *
 * 参数示例：
 *   world=MapWorld,menu=blr,menuMethod=b,menuDesc=(II)V,mc=bib,launch=a,
 *   loadWorld=a,loadWorldDesc=(Lbsb;)V,saveClass=net/minecraft/server/MinecraftServer,
 *   saveMethod=a,saveDesc=(Z)V,safeExit=1,haltOnSafe=1
 *
 * 符号名从 `mcml symbols <version>` 的输出里取，不要手抄。
 */
public final class Agent {

    public static Config config = new Config();

    /**
     * 拿配置：本进程里可能有两个 `mcml.agent.Agent`（见 Config.fromSystem 的说明），
     * 被注入字节码调用到的那份是「野生副本」，它的 config 是空的 —— 从系统属性补上。
     */
    public static Config configOrSystem() {
        if (config.values.isEmpty()) config = Config.fromSystem();
        return config;
    }

    public static void premain(String args, Instrumentation inst) {
        config = Config.parse(args);
        /**
         * 把参数与日志路径落到系统属性里：Forge 的 LaunchClassLoader 会再加载一份
         * `mcml.agent.*`，那份的静态字段是新的，只能靠 JVM 全局的系统属性把参数递过去。
         */
        System.setProperty("mcml.agent.args", args == null ? "" : args);
        System.setProperty("mcml.agent.log", config.get("log", "mcml-agent.log"));
        Log.open(config.get("log", "mcml-agent.log"));
        Log.info("premain. menuClass=" + config.get("menu", "?")
                + " mcClass=" + config.get("mc", "?")
                + " world=" + config.get("world", "-")
                + " save=" + config.get("saveClass", "-") + "."
                + config.get("saveMethod", "-") + config.get("saveDesc", ""));
        inst.addTransformer(new Injection(config), false);
        startPatchWatchdog(config);
    }

    /**
     * 钩子看门狗：一段时间后主菜单类还没被改写，就明确报 PATCH_MISS。
     *
     * 为什么需要它：符号对不上时，transformer 根本不会被调用 —— 没有异常、没有日志，
     * 表现只是"游戏停在主菜单"。上层（启动器）没法区分"还在加载"和"钩子没挂上"，
     * 玩家更是只能猜。有了这条标记，`play` 就能给出人话解释。
     *
     * 时间默认给 75 秒：模组环境下加载到主菜单本来就要 30~60 秒，
     * 报早了会误伤"只是慢"的机器。可用 `patchMissMs` 参数调整。
     */
    private static void startPatchWatchdog(final Config cfg) {
        final int ms = cfg.getInt("patchMissMs", 75000);
        Thread t = new Thread(new Runnable() {
            @Override
            public void run() {
                try {
                    Thread.sleep(ms);
                } catch (InterruptedException e) {
                    return;
                }
                if (Injection.isMenuPatched()) return;
                // 纯 ASCII：游戏进程的 stdout 是 GBK，中文经管道到启动器会变成乱码
                Log.info("PATCH_MISS menu " + cfg.get("menu", "?") + "." + cfg.get("menuMethod", "?")
                        + cfg.get("menuDesc", "") + " not patched within " + ms
                        + "ms (modded class names? or slow load)");
            }
        }, "mcml-patch-watchdog");
        t.setDaemon(true);
        t.start();
    }
}
