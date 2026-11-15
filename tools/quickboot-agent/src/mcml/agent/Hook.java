package mcml.agent;

import java.lang.reflect.Field;
import java.lang.reflect.InvocationTargetException;
import java.lang.reflect.Method;
import java.util.concurrent.Callable;
import java.util.concurrent.atomic.AtomicInteger;

/**
 * 注入点回调 + 退出状态机。
 *
 * 反射调用的原因：目标方法名是混淆的且每版不同，全部由参数传入（见 Config）。
 *
 * 流程：
 *   1. 主菜单 UI 构建时被调用（menu init #N）
 *   2. N==1 且配置了 world → 等几秒 → 反射 launchIntegratedServer 进世界（enteredOnce=true）
 *   3. 配置了 quitAfter → 再等几秒 → 复刻「Save and Quit to Title」两步操作
 *      （退出前把 savePending 置位，表示「从现在起的一次保存才算退出保存」）
 *   3'. 玩家自己点 Save and Quit → stopServer 入口被钩到 → 同样置位 savePending
 *      （区别只是 quitByPlayer=true，见 onServerStop）
 *   4. 第二次主菜单出现 → menu2Seen=true
 *   5. 落盘完成 + （主菜单已出现 **或** 退出是玩家发起的）→ EXIT_SAFE
 *
 * 为什么必须两个条件（S2 实测）：
 *   第二次主菜单出现时，服务端连 "Stopping server" 都没打印完，退出保存压根没开始。
 *   11 次迭代里 "Saving worlds" 出现 0 次 —— 此刻杀进程会丢掉最后一个自动保存周期
 *   之内的所有改动。
 *
 * 为什么还要 savePending：
 *   周期自动保存也会调 saveAllWorlds。如果只看「保存过」，进入世界后的一次自动保存
 *   就会把条件提前满足，退出时照样丢数据。所以只认「退出请求之后完成的保存」。
 */
public final class Hook {

    private static final AtomicInteger menuCount = new AtomicInteger(0);
    private static final AtomicInteger saveCount = new AtomicInteger(0);

    /** 是否成功进过世界（没进过世界就谈不上"退出保存"） */
    private static volatile boolean enteredOnce = false;
    /** 是否已请求退出，等待一次保存完成 */
    private static volatile boolean savePending = false;
    /** 退出请求之后是否已有一次保存完成 */
    private static volatile boolean saveDone = false;
    /** 第二次主菜单是否已出现 */
    private static volatile boolean menu2Seen = false;
    /**
     * stopServer 是否已经**返回**。
     *
     * 比「保存完成」更强的信号：停服方法返回意味着服务端把该写的都写完了
     * （1.16.5 的 `saveEverything` 里 saveAllChunks 之后还有 saveLevel 要写 level.dat），
     * 此时结束进程不会再丢任何东西，也就不用再等客户端把标题画面画出来。
     */
    private static volatile boolean serverStopped = false;
    /** 状态机是否已给出结论（避免重复 halt） */
    private static volatile boolean finished = false;
    /**
     * 退出是谁发起的：launcher = 外部信号文件；player = 玩家自己点了 Save and Quit。
     * 只影响日志与「谁来收尾」，判定条件（主菜单 + 落盘）两者一致。
     */
    private static volatile boolean quitByPlayer = false;
    private static volatile boolean watchdogStarted = false;
    private static volatile boolean graceStarted = false;
    /** 缓存的客户端 Minecraft 实例 */
    private static volatile Object cachedMinecraft = null;

    private Hook() {
    }

    /** 主菜单 UI 每次被构建时触发 */
    public static void onMenuInit(Object screen) {
        final int n = menuCount.incrementAndGet();
        Log.info("menu init #" + n);
        // 诊断：Forge 的 LaunchClassLoader 会加载第二份 Hook，那份的静态状态是新的。
        // 打一行 world 与 ClassLoader，能一眼看出"配置到底有没有传进来"。
        Log.info("menu init #" + n + " world=" + Agent.configOrSystem().get("world", "-")
                + " loader=" + Hook.class.getClassLoader());

        final Config cfg = Agent.configOrSystem();

        if (n >= 2) {
            // 只有「进过世界之后」的主菜单才算退出。
            // 1.8.9 的 launchIntegratedServer 内部会先 loadWorld(null)，顺带构建一次
            // 主菜单（日志里的 menu init #2），此时还没进世界，不能当成退出信号。
            if (enteredOnce) {
                menu2Seen = true;
                if (cfg.getBool("safeExit")) {
                    Log.info("MENU_2_SEEN entered=" + enteredOnce + " saved=" + saveDone);
                    maybeFinish(cfg);
                } else if (cfg.getBool("haltOnMenu2")) {
                    // 旧的 S2 行为：不看是否落盘，直接杀，用于复现"丢进度"的对照组
                    Log.info("HALT_AT_MENU_2");
                    terminateJvm(0);
                }
            } else {
                Log.info("menu init #" + n + " before entering world (ignored)");
            }
        }

        if (n != 1) return;
        final String world = cfg.get("world", null);
        if (world == null) return;

        final Object mc = findMinecraft(screen, cfg.get("mc", "bib"));
        if (mc == null) {
            Log.info("ERROR cannot find Minecraft instance on " + screen.getClass().getName());
            return;
        }
        cachedMinecraft = mc;
        Log.info("Minecraft instance found: " + mc.getClass().getName());

        final int delay = cfg.getInt("enterDelay", 3);
        /**
         * 自动进图开关（设置页「进图与退出 → 自动跳过主菜单」）。
         *
         * 关掉时**不调度 enterWorld**，玩家自己从「单人游戏」里选存档；
         * 但下面的退出信号线程照常起 —— 玩家进图之后仍然可以按 Ctrl-C /
         * 界面上的「保存并退出」让 Agent 复刻 Save and Quit。
         * 缺省 true：老 jar 与旧参数的行为不变。
         */
        if (!cfg.getBool("autoEnter", true)) {
            // 纯 ASCII：游戏进程的 stdout 是 GBK，中文经管道到启动器会变成乱码
            Log.info("AUTO_ENTER_OFF staying at main menu (autoEnter=0)");
        } else {
            daemon("mcml-enter", new ThrowingRunnable() {
                @Override
                public void run() throws Throwable {
                    Thread.sleep(delay * 1000L);
                    Log.info("scheduling enterWorld");
                    schedule(mc, new Runnable() {
                        @Override
                        public void run() {
                            // 1.19+ 的入口形状和 1.7~1.16 完全不同：Minecraft.doWorldLoad 要
                            // 自己装配 LevelStorageAccess / PackRepository / WorldStem，装不出来。
                            // 走官方那条 UI 路径更稳：createWorldOpenFlows().loadLevel(null, 世界名)，
                            // 它内部会经过 askForBackup（introskip 补丁就在那儿），失败再退回旧扫描。
                            String flows = cfg.get("flows", "");
                            if (!flows.isEmpty()
                                    && enterWorldViaFlows(mc, world, flows, cfg.get("flowsProbe", ""))) {
                                return;
                            }
                            enterWorld(mc, world, cfg.get("launch", "a"), cfg.get("enterDesc", ""));
                        }
                    });
                }
            });
        }

        final int quitAfter = cfg.getInt("quitAfter", 0);
        final String quitSignal = cfg.get("quitSignal", null);
        if (quitSignal != null) {
            // 由外部在「世界真的加载完」时创建这个文件（比固定延时可靠：
            // 1.8.9 的 200MB 地图加载要十几秒，固定 12 秒会在加载中途退出）
            daemon("mcml-quit-signal", new ThrowingRunnable() {
                @Override
                public void run() throws Throwable {
                    long deadline = System.currentTimeMillis() + cfg.getInt("quitSignalTimeout", 60000);
                    while (System.currentTimeMillis() < deadline) {
                        if (new java.io.File(quitSignal).exists()) {
                            Log.info("QUIT_SIGNAL detected");
                            break;
                        }
                        Thread.sleep(200L);
                    }
                    if (!new java.io.File(quitSignal).exists()) {
                        Log.info("QUIT_SIGNAL timeout, quitting anyway");
                    }
                    Log.info("quitting world");
                    schedule(mc, new Runnable() {
                        @Override
                        public void run() {
                            quitWorld(mc, cfg);
                        }
                    });
                }
            });
        } else if (quitAfter > 0) {
            daemon("mcml-quit", new ThrowingRunnable() {
                @Override
                public void run() throws Throwable {
                    Thread.sleep((delay + quitAfter) * 1000L);
                    Log.info("quitting world");
                    schedule(mc, new Runnable() {
                        @Override
                        public void run() {
                            quitWorld(mc, cfg);
                        }
                    });
                }
            });
        }
    }

    /**
     * stopServer 入口触发（由 Injection 插在方法开头）。
     *
     * 存在的理由（TODO Q9）：退出保存在 stopServer 内部完成，而 savePending 原本只在
     * **启动器主动退出**时置位。玩家自己点「Save and Quit to Title」时没人置位，
     * 于是 menu2 会到、保存也会发生，但状态机认为"这不是退出保存"，
     * 白等到超时再报 EXIT_UNSAFE —— 数据没丢，报告却是错的。
     *
     * 钩在这里比钩在 saveAllWorlds 更早：停服一开始就置位，
     * 后面那次 saveAllWorlds 自然被认成退出保存。
     */
    public static void onServerStop() {
        final Config cfg = Agent.configOrSystem();
        if (!enteredOnce) {
            /**
             * 进过世界之前就停服，通常要忽略：1.8.9 的 `launchIntegratedServer` 内部会先
             * `loadWorld(null)`，顺带停一次服，那不是玩家退出。
             *
             * 但 `autoEnter=0`（设置里关掉了自动跳过主菜单）时，启动器**不会**替玩家调
             * 那一步 —— 此时出现的停服只可能来自玩家自己关掉世界，所以要认下来，
             * 否则「退出时自动关闭游戏」在这个组合下永远不会触发。
             */
            if (cfg.getBool("autoEnter", true)) {
                Log.info("SERVER_STOP ignored (world not entered yet)");
                return;
            }
            enteredOnce = true;
            Log.info("SERVER_STOP player-initiated (manual entry, autoEnter=0)");
        }
        if (savePending) {
            Log.info("SERVER_STOP (already pending by launcher)");
            return;
        }
        savePending = true;
        quitByPlayer = true;
        Log.info("SERVER_STOP player-initiated quit, waiting for saveAllWorlds");
    }

    /**
     * stopServer **返回**时触发（由 Injection 插在每个 RETURN 之前）。
     *
     * 这是"可以关游戏了"的最强信号：服务端已经把世界与 level.dat 都写完。
     * 有了它，玩家点 Save and Quit 之后不必等客户端把标题画面画出来再关 ——
     * 之前只能等主菜单出现（慢半拍，屏幕上会闪一下标题）。
     */
    public static void onServerStopped() {
        if (serverStopped) return;
        serverStopped = true;
        Log.info("SERVER_STOPPED");
        maybeFinish(Agent.configOrSystem());
    }

    /**
     * saveAllWorlds 返回时触发（由 Injection 插在每个 RETURN 之前）。
     *
     * 不接收参数：方法可能是 void，也可能返回 boolean（1.16.5 的
     * saveAllChunks(ZZZ)Z），无参静态调用在两种栈形状下都成立，不需要动返回值。
     */
    public static void onSaveDone() {
        final int n = saveCount.incrementAndGet();
        final boolean pending = savePending;
        Log.info("SAVE_DONE #" + n + (pending ? " (quit save)" : " (autosave)"));

        if (!pending) return;
        saveDone = true;
        maybeFinish(Agent.configOrSystem());
    }

    /**
     * 收尾判定：落盘是硬条件，"世界关掉了"是软条件。
     *
     * 软条件有三种满足方式，按可靠性排序：
     *   1. `SERVER_STOPPED` —— 停服方法返回，服务端写完了（含 level.dat）；
     *   2. `MENU_2_SEEN` —— 客户端回到主菜单，说明世界已卸载；
     *   3. 玩家发起 + 保存完成后等 2 秒 —— 兜底，防止钩子没挂上就永远不关。
     *
     * 为什么玩家发起时要放宽：他点下「Save and Quit to Title」的意图已经明确，
     * 此时唯一该等的就是落盘；而主菜单钩子（TitleScreen.init）与 SAVE_DONE 谁先到
     * 并没有保证，死等主菜单会让手快的玩家判定落空。
     */
    private static void maybeFinish(final Config cfg) {
        if (finished) return;
        if (!saveDone) {
            startWatchdog(cfg);
            return;
        }
        if (menu2Seen) {
            finishSafe(cfg, "menu2");
            return;
        }
        if (serverStopped) {
            finishSafe(cfg, "server-stopped");
            return;
        }
        if (!quitByPlayer) {
            // 启动器发起的退出：继续等主菜单（S2 的教训：那时保存可能还没开始）
            startWatchdog(cfg);
            return;
        }
        startGrace(cfg, 2000);
    }

    private static void finishSafe(final Config cfg, final String why) {
        if (finished) return;
        Log.info("EXIT_SAFE menu2=" + (menu2Seen ? "yes" : "no")
                + " stopped=" + (serverStopped ? "yes" : "no")
                + " saved=yes entered=" + enteredOnce
                + " by=" + (quitByPlayer ? "player" : "launcher")
                + " via=" + why);
        if (cfg.getBool("haltOnSafe") || cfg.getBool("haltOnMenu2")) {
            finished = true;
            try {
                terminateJvm(0);
            } catch (Throwable t) {
                finished = false;
                Log.info("WARN terminateJvm failed: " + t);
            }
        }
    }

    /**
     * 强行终止 JVM，对各种模组环境（特别是 Forge 的 TerminalTransformer 和 FMLSecurityManager）做全套防御。
     */
    private static void terminateJvm(int status) {
        Log.close();

        // 1. Forge 官方退出入口（FMLCommonHandler.exitJava）。
        // Forge 的 TerminalTransformer 特许 FMLCommonHandler，且 FMLSecurityManager 允许
        // net.minecraftforge.fml 包内的类发起 exitVM 权限请求。
        try {
            ClassLoader cl = Thread.currentThread().getContextClassLoader();
            if (cl == null) cl = Hook.class.getClassLoader();
            Class<?> fml = null;
            try {
                fml = Class.forName("net.minecraftforge.fml.common.FMLCommonHandler", true, cl);
            } catch (ClassNotFoundException e) {
                try {
                    fml = Class.forName("cpw.mods.fml.common.FMLCommonHandler", true, cl);
                } catch (ClassNotFoundException ignored) {}
            }
            if (fml != null) {
                Object inst = fml.getMethod("instance").invoke(null);
                if (inst != null) {
                    Method exitJava = fml.getMethod("exitJava", int.class, boolean.class);
                    exitJava.invoke(inst, status, status == 0);
                }
            }
        } catch (Throwable ignored) {}

        // 2. 尝试调用 Minecraft 客户端实例的 shutdown 方法
        try {
            Object mc = cachedMinecraft;
            if (mc != null) {
                for (String mName : new String[] { "shutdown", "func_71400_g", "n", "m", "c" }) {
                    try {
                        Method m = mc.getClass().getMethod(mName);
                        m.setAccessible(true);
                        m.invoke(mc);
                        break;
                    } catch (NoSuchMethodException ignored) {}
                }
            }
        } catch (Throwable ignored) {}

        // 3. 反射调用 Runtime.halt（绕过字节码重写）
        try {
            Method haltMethod = Runtime.class.getMethod("halt", int.class);
            haltMethod.setAccessible(true);
            haltMethod.invoke(Runtime.getRuntime(), status);
        } catch (Throwable ignored) {}

        // 4. 直接 Runtime.halt
        try {
            Runtime.getRuntime().halt(status);
        } catch (Throwable ignored) {}

        // 5. System.exit
        try {
            System.exit(status);
        } catch (Throwable ignored) {}
    }

    /**
     * 玩家发起的退出专用兜底：保存已经完成，但既没等到停服返回也没等到主菜单。
     * 等 `ms` 毫秒后仍无更可靠信号，就按"已落盘"收尾 —— 宁可早关，也不让玩家
     * 对着标题画面不知道发生了什么。
     */
    private static void startGrace(final Config cfg, final int ms) {
        if (graceStarted) return;
        graceStarted = true;
        daemon("mcml-exit-grace", new ThrowingRunnable() {
            @Override
            public void run() throws Throwable {
                Thread.sleep(ms);
                if (!finished && saveDone) finishSafe(cfg, "grace-" + ms + "ms");
            }
        });
    }

    /**
     * 兜底：第二次主菜单出现后等 saveWaitMs 仍未观察到保存完成，就明确报告
     * 「不安全」而不是无限等。典型原因是符号名不对（钩子挂到了别的重载上）。
     */
    private static void startWatchdog(final Config cfg) {
        if (watchdogStarted) return;
        watchdogStarted = true;
        final int waitMs = cfg.getInt("saveWaitMs", 8000);
        daemon("mcml-save-watchdog", new ThrowingRunnable() {
            @Override
            public void run() throws Throwable {
                long deadline = System.currentTimeMillis() + waitMs;
                while (System.currentTimeMillis() < deadline) {
                    if (saveDone) {
                        maybeFinish(cfg);
                        return;
                    }
                    Thread.sleep(50L);
                }
                if (saveDone) {
                    maybeFinish(cfg);
                    return;
                }
                Log.info("EXIT_UNSAFE_TIMEOUT saveDone=false after " + waitMs + "ms"
                        + " (saveCount=" + saveCount.get() + ")");
                if (cfg.getBool("haltOnTimeout")) {
                    terminateJvm(2);
                }
            }
        });
    }

    /**
     * 反射进入存档。
     *
     * 三个版本族的入口形状完全不同，所以按**描述符**匹配，不靠参数个数猜：
     *   1.7~1.12  launchIntegratedServer(String, String, WorldSettings)
     *   1.13~1.16 loadLevel(String)
     *   1.17+     doWorldLoad(...)  —— 参数要自己装配，改走 enterWorldViaFlows（见上）
     *
     * 参数填法：String 参数填世界目录名，其余（WorldSettings 等）填 null，
     * 让游戏自己走"新建/默认设置"的分支。
     */
    /**
     * 1.19+ 的进图：`Minecraft.createWorldOpenFlows().loadLevel(screen, levelId)`。
     *
     * 为什么不给 doWorldLoad 装配参数：1.19 起进世界的入口是
     * `doWorldLoad(String, LevelStorageAccess, PackRepository, WorldStem, boolean)`，
     * 后三个都得先构造出来（storage access 还要先开文件锁），装错了就是坏档风险。
     * 而 `WorldOpenFlows.loadLevel` 是「单人游戏 → 选中存档 → 进入」按钮自己走的那条，
     * 世界数据、数据包、实验性设置判断全交给游戏。
     *
     * 参数从哪来：`flowsClass` 是 WorldOpenFlows 的混淆类名（启动器从 introskip 表里取），
     * `probeDesc` 是同一个类里 askForBackup 的描述符 —— 用它把 Screen 类名解析出来，
     * 进而拼出 `loadLevel(Screen, String)` 的描述符。全是**查出来**的，不是猜的：
     * 描述符对不上的方法一个都不碰。
     *
     * Screen 传 null：正常路径上这个参数只用于出错时把页面切回去。
     *
     * @return true = 调用已经发出（不代表世界加载完成，但进图请求已交给游戏）
     */
    private static boolean enterWorldViaFlows(Object mc, String world, String flowsClass, String probeDesc) {
        try {
            String screen = firstObjectParam(probeDesc);
            if (screen == null) {
                Log.info("flows: probeDesc 里没有 Screen 参数，跳过");
                return false;
            }
            Object flows = null;
            for (Method m : mc.getClass().getDeclaredMethods()) {
                if (m.getParameterCount() == 0 && m.getReturnType().getName().equals(flowsClass)) {
                    m.setAccessible(true);
                    flows = m.invoke(mc);
                    Log.info("flows: " + mc.getClass().getName() + "." + m.getName() + "() -> " + flowsClass);
                    break;
                }
            }
            if (flows == null) {
                Log.info("flows: 找不到返回 " + flowsClass + " 的无参方法");
                return false;
            }

            final String want = "(L" + screen + ";Ljava/lang/String;)V";
            for (Method m : flows.getClass().getDeclaredMethods()) {
                if (!descriptorOf(m.getParameterTypes(), m.getReturnType()).equals(want)) continue;
                m.setAccessible(true);
                Log.info("flows: invoke " + flows.getClass().getName() + "." + m.getName() + want + " world=" + world);
                m.invoke(flows, null, world);
                enteredOnce = true;
                Log.info("ENTER_WORLD_OK via flows");
                return true;
            }
            Log.info("flows: " + flows.getClass().getName() + " 里没有 " + want);
            return false;
        } catch (InvocationTargetException e) {
            Log.info("flows threw: " + e.getCause());
            return false;
        } catch (Throwable t) {
            Log.info("flows failed: " + t);
            return false;
        }
    }

    /** 从 `(Lepb;Ljava/lang/String;ZLjava/lang/Runnable;)V` 里取出第一个对象参数的内部名 */
    private static String firstObjectParam(String desc) {
        if (desc == null || desc.length() < 3 || desc.charAt(0) != '(' || desc.charAt(1) != 'L') return null;
        int end = desc.indexOf(';', 2);
        return end < 0 ? null : desc.substring(2, end);
    }

    private static void enterWorld(Object mc, String world, String launchMethod, String wantDesc) {
        try {
            Log.info("enterWorld start thread=" + Thread.currentThread().getName()
                    + " launch=" + launchMethod + " desc=" + wantDesc);
            Method target = null;
            Method[] all = mc.getClass().getDeclaredMethods();
            Log.info("enterWorld scanning " + all.length + " methods");
            for (Method m : all) {
                boolean nameMatch = m.getName().equals(launchMethod)
                        || m.getName().equals("func_71371_a")
                        || m.getName().equals("launchIntegratedServer")
                        || m.getName().equals("loadLevel")
                        || m.getName().equals("func_241559_a_");
                Class<?>[] ps = m.getParameterTypes();
                if (nameMatch) {
                    if (!wantDesc.isEmpty()) {
                        if (descriptorOf(ps, m.getReturnType()).equals(wantDesc)) {
                            target = m;
                            break;
                        }
                    }
                    if (plausibleEntry(ps)) {
                        target = m;
                        break;
                    }
                }
            }
            if (target == null) {
                for (Method m : all) {
                    if (plausibleEntry(m.getParameterTypes())) {
                        target = m;
                        break;
                    }
                }
            }
            if (target == null) {
                Log.info("ERROR launch method not found: " + launchMethod
                        + (wantDesc.isEmpty() ? "" : wantDesc));
                return;
            }
            target.setAccessible(true);
            Class<?>[] ps = target.getParameterTypes();
            Object[] args = new Object[ps.length];
            for (int i = 0; i < ps.length; i++) {
                args[i] = ps[i] == String.class ? world : null;
            }
            Log.info("invoke " + target.getName() + descriptorOf(ps, target.getReturnType())
                    + " world=" + world + " argc=" + ps.length);
            target.invoke(mc, args);
            enteredOnce = true;
            Log.info("ENTER_WORLD_OK enteredOnce=true");
        } catch (InvocationTargetException e) {
            Log.info("enterWorld threw: " + e.getCause());
            Throwable cause = e.getCause();
            StackTraceElement[] st = cause == null ? new StackTraceElement[0] : cause.getStackTrace();
            for (int i = 0; i < Math.min(6, st.length); i++) Log.info("    at " + st[i]);
        } catch (Throwable t) {
            Log.info("enterWorld failed: " + t);
        }
    }

    /** 没给描述符时的兜底判断：3 参（两个 String）或 1 参（String） */
    private static boolean plausibleEntry(Class<?>[] ps) {
        if (ps.length == 3 && ps[0] == String.class && ps[1] == String.class) return true;
        return ps.length == 1 && ps[0] == String.class;
    }

    private static String descriptorOf(Class<?>[] params, Class<?> returnType) {
        StringBuilder sb = new StringBuilder("(");
        for (Class<?> p : params) sb.append(org.objectweb.asm.Type.getDescriptor(p));
        sb.append(')').append(org.objectweb.asm.Type.getDescriptor(returnType));
        return sb.toString();
    }

    private static void quitWorld(Object mc, Config cfg) {
        try {
            String name = cfg.get("loadWorld", "a");
            String desc = cfg.get("loadWorldDesc", "");
            Method target = null;
            for (Method m : mc.getClass().getDeclaredMethods()) {
                boolean nameMatch = m.getName().equals(name)
                        || m.getName().equals("func_71403_a")
                        || m.getName().equals("loadWorld");
                Class<?>[] ps = m.getParameterTypes();
                if (ps.length != 1) continue;
                if (nameMatch) {
                    if (!desc.isEmpty()) {
                        String actual = "(" + org.objectweb.asm.Type.getDescriptor(ps[0]) + ")V";
                        if (actual.equals(desc)) {
                            target = m;
                            break;
                        }
                    }
                    target = m;
                    break;
                }
            }
            if (target == null) {
                Log.info("ERROR loadWorld method not found");
                return;
            }
            target.setAccessible(true);
            // 从这里开始，只有「之后完成的保存」才算退出保存
            savePending = true;
            Log.info("QUIT_REQUESTED waiting for saveAllWorlds");
            Log.info("QUIT_BEGIN " + target.getName() + "(" + target.getParameterTypes()[0].getName() + ")null");
            target.invoke(mc, new Object[]{null});
            Log.info("QUIT_RETURNED");
            showMainMenu(mc, cfg);
        } catch (InvocationTargetException e) {
            Log.info("quitWorld threw: " + e.getCause());
        } catch (Throwable t) {
            Log.info("quitWorld failed: " + t);
        }
    }

    /**
     * 反射 new 一个主菜单并 displayGuiScreen，复刻「Save and Quit to Title」的第二步。
     *
     * 少了这一步，世界会卸载但界面停在半黑屏，也就观察不到「第二次主菜单」。
     */
    private static void showMainMenu(Object mc, Config cfg) {
        try {
            String menuClassName = cfg.get("menu", "blr");
            Class<?> menuClass = null;
            try {
                menuClass = Class.forName(menuClassName);
            } catch (Throwable t) {
                try {
                    menuClass = Class.forName("net.minecraft.client.gui.GuiMainMenu");
                } catch (Throwable t2) {
                    menuClass = Class.forName("net.minecraft.client.gui.screens.TitleScreen");
                }
            }
            Object menu = menuClass.getDeclaredConstructor().newInstance();
            String displayName = cfg.get("displayGui", "a");

            for (Method m : mc.getClass().getDeclaredMethods()) {
                boolean nameMatch = m.getName().equals(displayName)
                        || m.getName().equals("func_147108_a")
                        || m.getName().equals("displayGuiScreen")
                        || m.getName().equals("setScreen");
                Class<?>[] ps = m.getParameterTypes();
                if (ps.length != 1) continue;
                if (!ps[0].isAssignableFrom(menuClass) && !ps[0].getName().endsWith("GuiScreen") && !ps[0].getName().endsWith("Screen")) continue;
                if (nameMatch) {
                    m.setAccessible(true);
                    m.invoke(mc, menu);
                    Log.info("DISPLAY_MAIN_MENU_OK");
                    return;
                }
            }
            Log.info("ERROR displayGuiScreen not found");
        } catch (Throwable t) {
            Log.info("showMainMenu failed: " + t);
        }
    }

    // ---- 反射工具 ----

    private static Object findMinecraft(Object screen, String mcClass) {
        String mcClassDot = mcClass == null ? "" : mcClass.replace('/', '.');
        for (Class<?> c = screen.getClass(); c != null && c != Object.class; c = c.getSuperclass()) {
            for (Field f : c.getDeclaredFields()) {
                String typeName = f.getType().getName();
                if (typeName.equals(mcClass) || typeName.equals(mcClassDot)
                        || typeName.equals("net.minecraft.client.Minecraft")) {
                    try {
                        f.setAccessible(true);
                        Object obj = f.get(screen);
                        if (obj != null) return obj;
                    } catch (Throwable ignored) {
                    }
                }
            }
        }
        try {
            Class<?> mcCls = Class.forName("net.minecraft.client.Minecraft");
            Method m = mcCls.getMethod("getMinecraft");
            Object obj = m.invoke(null);
            if (obj != null) return obj;
        } catch (Throwable ignored) {}
        try {
            Class<?> mcCls = Class.forName("net.minecraft.client.Minecraft");
            Method m = mcCls.getMethod("getInstance");
            Object obj = m.invoke(null);
            if (obj != null) return obj;
        } catch (Throwable ignored) {}
        if (!mcClassDot.isEmpty()) {
            try {
                Class<?> mcCls = Class.forName(mcClassDot);
                for (Method m : mcCls.getDeclaredMethods()) {
                    if (java.lang.reflect.Modifier.isStatic(m.getModifiers())
                            && m.getReturnType() == mcCls
                            && m.getParameterTypes().length == 0) {
                        m.setAccessible(true);
                        Object obj = m.invoke(null);
                        if (obj != null) return obj;
                    }
                }
            } catch (Throwable ignored) {}
        }
        for (Class<?> c = screen.getClass(); c != null && c != Object.class; c = c.getSuperclass()) {
            for (Field f : c.getDeclaredFields()) {
                if (f.getType().getName().endsWith("Minecraft")) {
                    try {
                        f.setAccessible(true);
                        Object val = f.get(screen);
                        if (val != null) return val;
                    } catch (Throwable ignored) {}
                }
            }
        }
        return null;
    }

    /**
     * 把任务丢到游戏主线程队列（等价于 addScheduledTask / execute）。
     *
     * 混淆环境下这段特别容易踩坑（1.16.5 实测）：
     *   1. 调度方法可能是**继承**来的（`execute(Runnable)` 在 BlockableEventLoop 上），
     *      只扫 `getDeclaredMethods()` 扫不到；
     *   2. 同名形状的干扰方法很多：
     *      · `boolean shouldRun(Runnable)` —— 返回基本类型的谓词
     *      · `Runnable wrapRunnable(Runnable)` —— 只包装不执行
     *      按「单参 Runnable」盲选，任务会被悄悄丢掉（界面正常但什么都不发生）。
     *
     * 所以按名字优先：实现 `java.util.concurrent.Executor` 的 `execute` 不会被混淆
     * （JDK 接口的方法名必须保留），`submit` 同理。名字对不上才退回形状启发式。
     */
    private static void schedule(Object mc, Runnable task) {
        try {
            Method best = null;
            int bestScore = 0;
            for (Class<?> c = mc.getClass(); c != null && c != Object.class; c = c.getSuperclass()) {
                for (Method m : c.getDeclaredMethods()) {
                    Class<?>[] ps = m.getParameterTypes();
                    if (ps.length != 1) continue;
                    boolean isRunnable = ps[0] == Runnable.class;
                    boolean isCallable = ps[0] == Callable.class;
                    if (!isRunnable && !isCallable) continue;

                    int score = 0;
                    if (isRunnable && m.getName().equals("execute")) score = 100;
                    else if (isRunnable && m.getName().equals("submit")) score = 90;
                    else if (m.getReturnType() == void.class) score = isRunnable ? 50 : 40;
                    else if (isCallable && !m.getReturnType().isPrimitive()) score = 30;

                    if (score > bestScore) {
                        best = m;
                        bestScore = score;
                    }
                }
            }
            if (best == null) {
                Log.info("no scheduler found, running inline");
                task.run();
                return;
            }
            best.setAccessible(true);
            if (best.getParameterTypes()[0] == Runnable.class) {
                best.invoke(mc, task);
                Log.info("scheduled via " + best.getDeclaringClass().getSimpleName()
                        + "." + best.getName() + "(Runnable) score=" + bestScore);
            } else {
                best.invoke(mc, new Callable<Object>() {
                    @Override
                    public Object call() {
                        task.run();
                        return null;
                    }
                });
                Log.info("scheduled via " + best.getDeclaringClass().getSimpleName()
                        + "." + best.getName() + "(Callable) score=" + bestScore);
            }
        } catch (Throwable t) {
            Log.info("schedule failed: " + t);
            task.run();
        }
    }

    private interface ThrowingRunnable {
        void run() throws Throwable;
    }

    private static void daemon(String name, final ThrowingRunnable body) {
        Thread t = new Thread(new Runnable() {
            @Override
            public void run() {
                try {
                    body.run();
                } catch (Throwable e) {
                    Log.info(name + " failed: " + e);
                }
            }
        }, name);
        t.setDaemon(true);
        t.start();
    }
}
