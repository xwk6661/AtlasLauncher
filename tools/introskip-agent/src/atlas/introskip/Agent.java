// SPDX-License-Identifier: GPL-3.0-only
/*
 *  Atlas - Minecraft map launcher
 *  Copyright (c) 2026 Atlas contributors
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

package atlas.introskip;

import java.io.BufferedReader;
import java.io.File;
import java.io.FileOutputStream;
import java.io.InputStream;
import java.io.InputStreamReader;
import java.io.OutputStreamWriter;
import java.io.PrintWriter;
import java.net.URL;
import java.nio.charset.StandardCharsets;
import java.lang.instrument.ClassFileTransformer;
import java.lang.instrument.Instrumentation;
import java.security.ProtectionDomain;
import java.text.SimpleDateFormat;
import java.util.Date;
import java.util.HashMap;
import java.util.Map;
import java.util.regex.Pattern;

import atlas.introskip.shaded.asm.ClassReader;
import atlas.introskip.shaded.asm.ClassVisitor;
import atlas.introskip.shaded.asm.ClassWriter;
import atlas.introskip.shaded.asm.MethodVisitor;
import atlas.introskip.shaded.asm.Opcodes;
import atlas.introskip.shaded.asm.Type;

/**
 * 跳过 1.20+ 的「实验性设置」确认页（不支持使用实验性设置的世界 / 我知道我在做什么）。
 *
 * <p>闸门在 {@code WorldOpenFlows.askForBackup(LevelStorageAccess, boolean customized,
 * Runnable loadLevel, Runnable onCancel)}：世界被判定为 experimental（自带数据包改过世界生成）
 * 或来自旧版本时，它不加载世界，而是弹一个确认页。玩家点「我知道我在做什么」走的就是
 * {@code loadLevel}；点「创建备份并加载」会先复制一份存档。
 *
 * <p>这里把那个方法的字节码整体换成 {@code loadLevel.run(); return;} —— 等于永远选
 * 「我知道我在做什么」：世界内容、数据包、level.dat 一律不动，只是不再停在这一页。
 * 1.20+ 的类名/方法名都是混淆的，且随版本变，所以由启动器按版本把
 * {@code cls/method/desc} 传进来（表在 {@code resources/agents/introskip.json}）。
 *
 * <p>参数（k=v,k=v）：
 * {@code cls=ftm,method=a,desc=(Lerf$c;ZLjava/lang/Runnable;Ljava/lang/Runnable;)V,index=3,log=<path>}
 * {@code index} 是 loadLevel 那个 Runnable 的局部变量槽位：1.20.3+ 是 3（第 3 个参数），
 * 1.20~1.20.2 的方法签名是 (Screen, String, boolean, Runnable) → 4。
 */
public final class Agent {

    private static String targetClass;
    private static String targetMethod;
    private static String targetDesc;
    private static int targetIndex = 3;
    private static boolean targetDefer = false;
    private static String logPath;

    /**
     * Fabric（以及任何把游戏 jar 重映射成 intermediary 的加载器）下，运行时类名不是
     * {@code ftm} 而是 {@code net/minecraft/class_7196}，方法名也变成 {@code method_41898}。
     * 这里缓存翻译后的类名；非空即表示本次运行在重映射环境里。
     */
    private static String targetClassRemapped;

    private Agent() {}

    public static void premain(String args, Instrumentation instrumentation) {
        Map<String, String> config = parse(args);
        targetClass = config.get("cls");
        targetMethod = config.get("method");
        targetDesc = config.get("desc");
        logPath = config.get("log");
        if (config.containsKey("index")) {
            try {
                targetIndex = Integer.parseInt(config.get("index"));
            } catch (NumberFormatException ignored) {
                targetIndex = 3;
            }
        }
        if (config.containsKey("defer")) {
            targetDefer = "1".equals(config.get("defer")) || "true".equalsIgnoreCase(config.get("defer"));
        }

        if (isBlank(targetClass) || isBlank(targetMethod) || isBlank(targetDesc)) {
            log("缺少 cls/method/desc，本次不注入补丁");
            return;
        }

        log("待命：" + targetClass + "." + targetMethod + targetDesc + " loadLevel=local" + targetIndex
                + (targetDefer ? " (defer=true)" : ""));

        targetClassRemapped = remappedName(targetClass);
        if (targetClassRemapped != null) {
            log("检测到重映射环境（Fabric/intermediary）：" + targetClass + " → " + targetClassRemapped
                    + "，按描述符形状匹配方法");
        }

        instrumentation.addTransformer(new Patcher());
    }

    /**
     * 从 classpath 上的 {@code intermediary-*.jar} 里读 {@code mappings/mappings.tiny}，
     * 把官方（混淆）类名翻成 intermediary 名。找不到映射资源就返回 null —— 原版路径行为不变。
     *
     * <p>只查 CLASS 段：方法名同样会变，但我们不需要它 —— {@link #shapeOf(String)} 会按参数结构
     * 找到唯一那个方法（{@code (Screen, boolean, Runnable, Runnable)void}），类名翻过去就够了。
     */
    private static String remappedName(String official) {
        if (isBlank(official)) {
            return null;
        }
        InputStream in = null;
        try {
            ClassLoader loader = ClassLoader.getSystemClassLoader();
            URL url = loader.getResource("mappings/mappings.tiny");
            if (url == null) {
                return null;
            }
            in = url.openStream();
            BufferedReader reader = new BufferedReader(new InputStreamReader(in, StandardCharsets.UTF_8));
            String line;
            String prefix = "CLASS\t" + official + "\t";
            while ((line = reader.readLine()) != null) {
                if (line.startsWith(prefix)) {
                    String value = line.substring(prefix.length()).trim();
                    return value.isEmpty() ? null : value;
                }
            }
        } catch (Throwable error) {
            log("读取 intermediary 映射失败（按原版处理）：" + error);
        } finally {
            if (in != null) {
                try {
                    in.close();
                } catch (Exception ignored) {}
            }
        }
        return null;
    }

    /** 把描述符里的每个 {@code Lxxx;} 通配掉，只保留参数个数 / 基本类型结构。 */
    private static Pattern shapeOf(String desc) {
        StringBuilder sb = new StringBuilder();
        int i = 0;
        while (i < desc.length()) {
            char c = desc.charAt(i);
            if (c == 'L') {
                int end = desc.indexOf(';', i);
                if (end < 0) {
                    sb.append(Pattern.quote(desc.substring(i)));
                    break;
                }
                sb.append("L[^;]+;");
                i = end + 1;
            } else {
                sb.append(Pattern.quote(String.valueOf(c)));
                i++;
            }
        }
        return Pattern.compile(sb.toString());
    }

    /**
     * 针对部分旧版本（如 1.16.5）：
     * 确认页所在的 doLoadLevel 此时还持有存档文件锁（cyg$a / LevelStorageAccess）。
     * 若在确认页方法内同步执行 runnable.run()，重入的 doLoadLevel 会因 session.lock 未释放而抛出
     * java.io.IOException（文件已被另一个程序锁定）。
     * 因此在此异步等 150ms，让外层 doLoadLevel 关闭旧锁并退出，再把任务派发回主渲染线程执行。
     */
    public static void defer(final Object mc, final Runnable r) {
        if (r == null) {
            return;
        }
        log("捕获确认页弹窗，延迟派发以释放存档锁...");
        new Thread(new Runnable() {
            @Override
            public void run() {
                try {
                    Thread.sleep(150);
                } catch (InterruptedException ignored) {}
                try {
                    if (mc instanceof java.util.concurrent.Executor) {
                        ((java.util.concurrent.Executor) mc).execute(r);
                    } else {
                        r.run();
                    }
                    log("已异步派发 loadLevel 任务");
                } catch (Throwable t) {
                    log("派发 loadLevel 任务异常: " + t);
                }
            }
        }, "atlas-introskip-defer").start();
    }

    public static byte[] patch(byte[] original, String className, String method, String descriptor, int loadLevelSlot) {
        return patch(original, className, method, descriptor, loadLevelSlot, false);
    }

    /** 供离线自检使用：把一段 class 字节里的目标方法换成「直接 loadLevel」或「延迟派发 loadLevel」。 */
    public static byte[] patch(byte[] original, String className, String method, String descriptor, int loadLevelSlot, boolean defer) {
        return patch(original, className, method, descriptor, loadLevelSlot, defer, false);
    }

    /**
     * @param byShape true = 忽略方法名与描述符里的类名，只按 {@link #targetDescShape} 的参数结构匹配。
     *                重映射环境（Fabric）走这条：类名与方法名都变了，但参数结构不变。
     */
    public static byte[] patch(byte[] original, String className, String method, String descriptor, int loadLevelSlot,
                               boolean defer, boolean byShape) {
        final int slot = loadLevelSlot;
        final boolean deferExec = defer;
        // 形状在调用点算，不依赖 premain 的静态状态：离线自检也能直接调
        final Pattern shape = byShape ? shapeOf(descriptor) : null;
        if (shape != null) {
            int matches = countShapeMatches(original, shape);
            if (matches != 1) {
                log("按形状匹配到 " + matches + " 个方法（期望 1），本次不改写：" + className + " " + descriptor);
                return null;
            }
        }
        final boolean[] found = new boolean[1];
        ClassReader reader = new ClassReader(original);
        final ClassWriter writer = new ClassWriter(reader, 0);
        ClassVisitor visitor = new ClassVisitor(Opcodes.ASM9, writer) {
            @Override
            public MethodVisitor visitMethod(int access, String name, String desc, String signature, String[] exceptions) {
                boolean hit = shape != null ? shape.matcher(desc).matches()
                                            : (name.equals(method) && desc.equals(descriptor));
                if (!hit) {
                    return super.visitMethod(access, name, desc, signature, exceptions);
                }
                found[0] = true;
                MethodVisitor mv = super.visitMethod(access, name, desc, signature, exceptions);
                mv.visitCode();
                if (deferExec) {
                    mv.visitVarInsn(Opcodes.ALOAD, 0);
                    mv.visitVarInsn(Opcodes.ALOAD, slot);
                    mv.visitMethodInsn(Opcodes.INVOKESTATIC, "atlas/introskip/Agent", "defer", "(Ljava/lang/Object;Ljava/lang/Runnable;)V", false);
                } else {
                    mv.visitVarInsn(Opcodes.ALOAD, slot);
                    mv.visitMethodInsn(Opcodes.INVOKEINTERFACE, "java/lang/Runnable", "run", "()V", true);
                }
                mv.visitInsn(Opcodes.RETURN);
                // maxLocals 必须覆盖「所有参数」，哪怕我们只用到了其中一个：
                // 只按 ALOAD 的槽位算会被 JVM 拒（ClassFormatError: Arguments can't fit into locals）
                int argumentSlots = 1;  // this
                for (Type type : Type.getArgumentTypes(descriptor)) {
                    argumentSlots += type.getSize();
                }
                mv.visitMaxs(deferExec ? 2 : 1, Math.max(slot + 1, argumentSlots));
                mv.visitEnd();
                // 原方法体（含确认页的构造）整个丢掉
                return null;
            }
        };
        reader.accept(visitor, 0);
        if (!found[0]) {
            return null;
        }
        return writer.toByteArray();
    }

    /** 只数有几个方法符合目标描述符的「形状」，用来保证不会误改同形状的其它方法。 */
    private static int countShapeMatches(byte[] original, final Pattern shape) {
        final int[] count = new int[1];
        new ClassReader(original).accept(new ClassVisitor(Opcodes.ASM9) {
            @Override
            public MethodVisitor visitMethod(int access, String name, String desc, String signature, String[] exceptions) {
                if (shape.matcher(desc).matches()) {
                    count[0]++;
                }
                return null;
            }
        }, ClassReader.SKIP_CODE | ClassReader.SKIP_DEBUG | ClassReader.SKIP_FRAMES);
        return count[0];
    }

    public static final class Patcher implements ClassFileTransformer {
        @Override
        public byte[] transform(ClassLoader loader, String className, Class<?> redefined,
                                ProtectionDomain domain, byte[] classfileBuffer) {
            if (classfileBuffer == null || className == null) {
                return null;
            }
            // 原版：类名一致即可。重映射环境：类名已经换成 intermediary，方法名/描述符跟着变，
            // 只能按「参数形状」找方法（见 shapeOf）。
            final boolean remapped = !targetClass.equals(className)
                                     && targetClassRemapped != null && targetClassRemapped.equals(className);
            if (!remapped && !targetClass.equals(className)) {
                return null;
            }
            try {
                byte[] patched = patch(classfileBuffer, className, remapped ? null : targetMethod, targetDesc,
                                       targetIndex, targetDefer, remapped);
                if (patched == null) {
                    log("类已加载，但没有匹配的方法：" + className + (remapped ? "（按形状匹配）" : "." + targetMethod + targetDesc));
                    return null;
                }
                log("已跳过确认页：" + className + (remapped ? "（形状匹配 " + targetDesc + "）" : "." + targetMethod + targetDesc)
                        + (targetDefer ? " (deferred)" : ""));
                return patched;
            } catch (Throwable error) {
                // 补丁失败绝不能让游戏起不来：原样返回，游戏自己会弹那一页
                log("改写失败，按原样加载：" + error);
                return null;
            }
        }
    }

    private static Map<String, String> parse(String args) {
        Map<String, String> config = new HashMap<String, String>();
        if (args == null) {
            return config;
        }
        String[] parts = args.split(",");
        for (String part : parts) {
            int eq = part.indexOf('=');
            if (eq <= 0) {
                continue;
            }
            config.put(part.substring(0, eq).trim(), part.substring(eq + 1).trim());
        }
        return config;
    }

    private static boolean isBlank(String value) {
        return value == null || value.trim().isEmpty();
    }

    private static synchronized void log(String message) {
        String line = "[atlas-introskip] " + new SimpleDateFormat("HH:mm:ss").format(new Date()) + " " + message;

        if (append(logPath, line)) {
            return;
        }
        // 主路径写不了（典型原因：路径里有 GBK 表示不了的字符，JVM 直接找不到文件）
        // 退到临时目录，至少别把线索丢了
        String temp = System.getProperty("java.io.tmpdir");
        if (temp != null && append(new File(temp, "atlas-introskip.log").getPath(), line)) {
            return;
        }
        System.out.println(line);
    }

    private static boolean append(String path, String line) {
        if (path == null || path.trim().isEmpty()) {
            return false;
        }
        try {
            PrintWriter out = new PrintWriter(new OutputStreamWriter(
                    new FileOutputStream(path, true), StandardCharsets.UTF_8));
            try {
                out.println(line);
            } finally {
                out.close();
            }
            return true;
        } catch (Throwable ignored) {
            return false;
        }
    }
}
