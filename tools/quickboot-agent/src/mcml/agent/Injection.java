package mcml.agent;

import java.lang.instrument.ClassFileTransformer;
import java.security.ProtectionDomain;
import java.util.Set;
import java.util.concurrent.ConcurrentHashMap;

import org.objectweb.asm.ClassReader;
import org.objectweb.asm.ClassVisitor;
import org.objectweb.asm.ClassWriter;
import org.objectweb.asm.MethodVisitor;
import org.objectweb.asm.Opcodes;
import org.objectweb.asm.commons.AdviceAdapter;

/**
 * 三处注入，分两个类：
 *
 *   1. 主菜单的「构建 UI」方法**开头** → Hook.onMenuInit(screen)
 *      开头只有一个人口，天然每次调用触发一次；结尾可能有多个 RETURN。
 *
 *   2. saveAllWorlds 的**每一个返回点** → Hook.onSaveDone()
 *      这里必须用 AdviceAdapter 挂 onMethodExit，因为退出保存在不同版本里
 *      可能有多个 RETURN（早退分支），漏一个就可能永远等不到"已落盘"。
 *      ATHROW 路径不算完成 —— 保存抛异常时不能标记成功。
 *
 *   3. stopServer 的**入口 + 每个返回点** → Hook.onServerStop() / onServerStopped()
 *      入口置位 savePending（玩家自己点「Save and Quit to Title」时没人会替我们置位，
 *      状态机会空等到超时，TODO Q9）；返回点表示服务端把世界与 level.dat 都写完了，
 *      是"可以关游戏了"的最强信号 —— 有它就不必等客户端把标题画面画出来。
 *
 * 2 和 3 通常落在**同一个类**上（MinecraftServer），但一个是返回点、一个是入口，
 * 所以必须一趟 accept 里同时处理：ClassFileTransformer 一次只能返回一份字节，
 * 分两次 patch 会互相覆盖。
 *
 * 注意：用到 AdviceAdapter 时必须让 ClassReader 以 EXPAND_FRAMES 读取，
 * 否则运行期抛 `IllegalStateException: ClassReader.accept() should be
 * called with EXPAND_FRAMES flag`（实测踩过，transform 会静默失败，
 * 表现为钩子永远不触发）。只有需要 onMethodExit 的类才付这个代价。
 *
 * 所有符号名都由参数传入（见 Config），换版本只换参数，jar 不用重编译。
 */
final class Injection implements ClassFileTransformer {

    private final String menuClass;
    /** 同一个名字的斜杠形态：transformer 拿到的是内部名，而 Class.forName 要的是点分名 */
    private final String menuClassSlash;
    private final String menuMethod;
    private final String menuDesc;
    private final String saveClass;
    private final String saveMethod;
    private final String saveDesc;
    private final String stopClass;
    private final String stopMethod;
    private final String stopDesc;
    private final boolean inject;
    private final boolean injectSave;
    private final boolean injectStop;
    private final Set<String> done = ConcurrentHashMap.newKeySet();

    /**
     * 类名转储（调试用，`dumpClasses=<文件>`）。
     *
     * 存在的理由很具体：模组环境里符号对不上时，我们需要知道**运行时到底叫什么名字**。
     * 光看 PATCH_MISS 只知道"没匹配上"，不知道是 MCP 名、SRG 名还是别的。
     * 把 transformer 见到的每个类名写一行，`grep Menu` 就能看到真相。
     * 只在显式开启时生效，平时零开销（一次 null 判断）。
     */
    private final java.io.Writer dump;

    /**
     * 主菜单类是否被成功改写。
     *
     * 这是「Agent 到底有没有生效」的唯一可信判据：主菜单钩子挂不上，后面进图、
     * 退出保存全都无从谈起。模组环境（Fabric intermediary / 没覆盖到的改写）里
     * 类名对不上时，这里会一直是 false —— 由 Agent 的看门狗报 PATCH_MISS，
     * 让启动器能说"符号没对上，游戏会停在主菜单"，而不是让玩家对着主菜单猜。
     */
    private static volatile boolean menuPatched = false;

    static boolean isMenuPatched() {
        return menuPatched;
    }

    Injection(Config config) {
        this.menuClass = config.get("menu", "blr");
        // 原版是 `blp`（无包名，点分与斜杠相同）；Forge 映射后是
        // `net.minecraft.client.gui.GuiMainMenu`，而 transformer 的 className 是斜杠形态
        this.menuClassSlash = this.menuClass.replace('.', '/');
        this.menuMethod = config.get("menuMethod", "b");
        this.menuDesc = config.get("menuDesc", "(II)V");
        this.saveClass = config.get("saveClass", "");
        this.saveMethod = config.get("saveMethod", "");
        this.saveDesc = config.get("saveDesc", "");
        this.stopClass = config.get("stopClass", "");
        this.stopMethod = config.get("stopMethod", "");
        this.stopDesc = config.get("stopDesc", "");
        this.inject = !config.getBool("noInject");
        this.injectSave = !saveClass.isEmpty() && !saveMethod.isEmpty() && !config.getBool("noSaveHook");
        this.injectStop = !stopClass.isEmpty() && !stopMethod.isEmpty()
                && !config.getBool("noStopHook");

        java.io.Writer w = null;
        String dumpPath = config.get("dumpClasses", null);
        if (dumpPath != null) {
            try {
                w = new java.io.BufferedWriter(new java.io.FileWriter(dumpPath, false));
                Log.info("dumpClasses -> " + dumpPath);
            } catch (Throwable t) {
                Log.info("dumpClasses open failed: " + t);
            }
        }
        this.dump = w;
    }

        private boolean isMenuClass(String className) {
        if (className == null) return false;
        if (className.equals(menuClass) || className.equals(menuClassSlash)) return true;
        if (className.equals("net/minecraft/client/gui/GuiMainMenu")) return true;
        if (className.equals("net/minecraft/client/gui/screen/TitleScreen")) return true;
        if (className.equals("net/minecraft/client/gui/screens/TitleScreen")) return true;
        return false;
    }

    private boolean isServerClass(String className) {
        if (className == null) return false;
        if (className.equals(saveClass) || className.equals(stopClass)) return true;
        if (className.equals("net/minecraft/server/MinecraftServer")) return true;
        if (className.equals("net/minecraft/server/dedicated/DedicatedServer")) return true;
        if (className.equals("net/minecraft/server/integrated/IntegratedServer")) return true;
        return false;
    }

    private static final Set<ClassLoader> configuredLoaders = ConcurrentHashMap.newKeySet();

    private static void configureLoader(ClassLoader loader) {
        if (loader == null || !configuredLoaders.add(loader)) return;
        try {
            java.lang.reflect.Method m = loader.getClass().getMethod("addTransformerExclusion", String.class);
            m.invoke(loader, "mcml.agent.");
            m.invoke(loader, "atlas.");
        } catch (Throwable ignored) {}
    }

    @Override
    public byte[] transform(ClassLoader loader, String className, Class<?> redefined,
                            ProtectionDomain pd, byte[] buf) {
        if (loader != null) {
            configureLoader(loader);
        }
        if (!inject || className == null || buf == null) return null;

        if (dump != null && done.add("dump:" + className)) {
            try {
                dump.write(className + "\n");
                dump.flush();
            } catch (Throwable ignored) {
                // 转储失败不能影响游戏
            }
        }

        if (isMenuClass(className)) {
            Log.info("seen menu class " + className + " len=" + buf.length
                    + " loader=" + (loader == null ? "bootstrap" : loader.getClass().getName()));
            return patchMenu(buf);
        }
        if (isServerClass(className)) {
            return patchServer(buf, injectSave, injectStop);
        }
        return null;
    }

    private byte[] patchMenu(byte[] buf) {
        try {
            ClassReader cr = new ClassReader(buf);
            ClassWriter cw = new ClassWriter(cr, ClassWriter.COMPUTE_MAXS);
            final boolean[] patched = new boolean[1];
            cr.accept(new ClassVisitor(Opcodes.ASM9, cw) {
                @Override
                public MethodVisitor visitMethod(int access, String name, String desc,
                                                 String signature, String[] exceptions) {
                    MethodVisitor mv = super.visitMethod(access, name, desc, signature, exceptions);
                    boolean match = (name.equals(menuMethod) && desc.equals(menuDesc))
                            || (name.equals(menuMethod) && desc.equals("()V"))
                            || (name.equals("func_73866_w_") && desc.equals("()V"))
                            || (name.equals("initGui") && desc.equals("()V"))
                            || (name.equals("init") && desc.equals("()V"));
                    if (!match) return mv;
                    patched[0] = true;
                    return new MethodVisitor(Opcodes.ASM9, mv) {
                        @Override
                        public void visitCode() {
                            super.visitCode();
                            visitVarInsn(Opcodes.ALOAD, 0);
                            visitMethodInsn(Opcodes.INVOKESTATIC, "mcml/agent/Hook",
                                    "onMenuInit", "(Ljava/lang/Object;)V", false);
                        }
                    };
                }
            }, 0);
            if (!patched[0]) {
                /**
                 * 名字对不上时的兜底：钩这个类的**构造器** `<init>()V`。
                 *
                 * 为什么可行：构造器永远叫 `<init>`，不会被任何混淆器/加载器改名，
                 * 而"主菜单被创建"这个事件恰恰等价于"构造器执行完"。
                 * 实测触发点：Fabric 1.16.5 里 `doy.b()V` 在运行时既不是 `b` 也不在
                 * intermediary 表里（合成方法），按名字怎么都钩不上；构造器一钩就中。
                 *
                 * 注意必须在**方法退出**处插：构造器刚进入时字段还没赋值，
                 * Hook 要读 `this` 上的 Minecraft 引用。
                 */
                Log.info("WARN 未找到目标方法 " + menuClass + "." + menuMethod + menuDesc
                        + "，改用构造器兜底");
                return patchMenuCtor(buf);
            }
            if (done.add("menu:" + menuClass)) {
                Log.info("patched " + menuClass + "." + menuMethod + menuDesc);
            }
            menuPatched = true;
            return cw.toByteArray();
        } catch (Throwable t) {
            Log.info("transform(menu) failed: " + t);
            return null;
        }
    }

    /**
     * 兜底：在主菜单类的构造器**返回前**插 Hook.onMenuInit(this)。
     *
     * 与按方法名注入的区别只有两点：找的是 `<init>()V`，插的是返回点而不是入口。
     * 其余（`done` 去重、日志格式）保持一致，上层看不出差别。
     */
    private byte[] patchMenuCtor(byte[] buf) {
        try {
            ClassReader cr = new ClassReader(buf);
            ClassWriter cw = new ClassWriter(cr, ClassWriter.COMPUTE_MAXS);
            final boolean[] patched = new boolean[1];
            cr.accept(new ClassVisitor(Opcodes.ASM9, cw) {
                @Override
                public MethodVisitor visitMethod(int access, String name, String desc,
                                                 String signature, String[] exceptions) {
                    MethodVisitor mv = super.visitMethod(access, name, desc, signature, exceptions);
                    if (!name.equals("<init>") || !desc.equals("()V")) return mv;
                    patched[0] = true;
                    return new AdviceAdapter(Opcodes.ASM9, mv, access, name, desc) {
                        @Override
                        protected void onMethodExit(int opcode) {
                            if (opcode == Opcodes.ATHROW) return;
                            visitVarInsn(Opcodes.ALOAD, 0);
                            visitMethodInsn(Opcodes.INVOKESTATIC, "mcml/agent/Hook",
                                    "onMenuInit", "(Ljava/lang/Object;)V", false);
                        }
                    };
                }
            }, ClassReader.EXPAND_FRAMES);
            if (!patched[0]) {
                Log.info("WARN 连构造器都没找到 " + menuClass + ".<init>()V");
                return null;
            }
            if (done.add("menu:" + menuClass)) {
                Log.info("patched " + menuClass + ".<init>()V (fallback)");
            }
            menuPatched = true;
            return cw.toByteArray();
        } catch (Throwable t) {
            Log.info("transform(menu-ctor) failed: " + t);
            return null;
        }
    }

    /**
     * 服务端类：保存钩子（返回点）+ 停服钩子（入口）一起处理。
     * `withSave` / `withStop` 决定这次要打哪几个，单个类只 accept 一次。
     */
    private byte[] patchServer(byte[] buf, final boolean withSave, final boolean withStop) {
        try {
            ClassReader cr = new ClassReader(buf);
            ClassWriter cw = new ClassWriter(cr, ClassWriter.COMPUTE_MAXS);
            final boolean[] savePatched = new boolean[1];
            final boolean[] stopPatched = new boolean[1];
            final int[] exits = new int[1];
            cr.accept(new ClassVisitor(Opcodes.ASM9, cw) {
                @Override
                public MethodVisitor visitMethod(int access, String name, String desc,
                                                 String signature, String[] exceptions) {
                    MethodVisitor mv = super.visitMethod(access, name, desc, signature, exceptions);

                    boolean matchSave = withSave && (
                        (name.equals(saveMethod) && desc.equals(saveDesc))
                        || (name.equals("func_71267_a") && desc.equals("(Z)V"))
                        || (name.equals("saveAllWorlds") && desc.equals("(Z)V"))
                        || (name.equals("saveAllChunks") && desc.equals(saveDesc))
                        || (desc.equals("(Z)V") && (name.equals("a") || name.startsWith("func_")))
                    );
                    if (matchSave) {
                        savePatched[0] = true;
                        return new AdviceAdapter(Opcodes.ASM9, mv, access, name, desc) {
                            @Override
                            protected void onMethodExit(int opcode) {
                                if (opcode == Opcodes.ATHROW) return;
                                exits[0]++;
                                visitMethodInsn(Opcodes.INVOKESTATIC, "mcml/agent/Hook",
                                        "onSaveDone", "()V", false);
                            }
                        };
                    }

                    boolean matchStop = withStop && (
                        (name.equals(stopMethod) && desc.equals(stopDesc))
                        || (name.equals("func_71260_j") && desc.equals("()V"))
                        || (name.equals("stopServer") && desc.equals("()V"))
                        || (desc.equals("()V") && (name.equals("u") || name.equals("t") || name.startsWith("func_71260")))
                    );
                    if (matchStop) {
                        stopPatched[0] = true;
                        // 入口置位 savePending；每个返回点报「服务端已停完」
                        return new AdviceAdapter(Opcodes.ASM9, mv, access, name, desc) {
                            @Override
                            protected void onMethodEnter() {
                                visitMethodInsn(Opcodes.INVOKESTATIC, "mcml/agent/Hook",
                                        "onServerStop", "()V", false);
                            }

                            @Override
                            protected void onMethodExit(int opcode) {
                                if (opcode == Opcodes.ATHROW) return;
                                visitMethodInsn(Opcodes.INVOKESTATIC, "mcml/agent/Hook",
                                        "onServerStopped", "()V", false);
                            }
                        };
                    }

                    return mv;
                }
            }, (withSave || withStop) ? ClassReader.EXPAND_FRAMES : 0);

            if (withSave) {
                if (!savePatched[0]) {
                    Log.info("WARN 未找到保存方法 " + saveClass + "." + saveMethod + saveDesc);
                } else if (done.add("save:" + saveClass)) {
                    Log.info("patched save " + saveClass + "." + saveMethod + saveDesc
                            + " (returns=" + exits[0] + ")");
                }
            }
            if (withStop) {
                if (!stopPatched[0]) {
                    Log.info("WARN 未找到停服方法 " + stopClass + "." + stopMethod + stopDesc);
                } else if (done.add("stop:" + stopClass)) {
                    Log.info("patched stop " + stopClass + "." + stopMethod + stopDesc);
                }
            }
            // 只要有**一个**钩子挂上了就交回字节：另一个没挂上不该把已经成功的那个也丢掉
            return (savePatched[0] || stopPatched[0]) ? cw.toByteArray() : null;
        } catch (Throwable t) {
            Log.info("transform(server) failed: " + t);
            return null;
        }
    }
}
