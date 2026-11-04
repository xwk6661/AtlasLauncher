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

import java.io.File;
import java.io.FileOutputStream;
import java.io.OutputStreamWriter;
import java.io.PrintWriter;
import java.nio.charset.StandardCharsets;
import java.lang.instrument.ClassFileTransformer;
import java.lang.instrument.Instrumentation;
import java.security.ProtectionDomain;
import java.text.SimpleDateFormat;
import java.util.Date;
import java.util.HashMap;
import java.util.Map;

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
    private static String logPath;

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

        if (isBlank(targetClass) || isBlank(targetMethod) || isBlank(targetDesc)) {
            log("缺少 cls/method/desc，本次不注入补丁");
            return;
        }

        log("待命：" + targetClass + "." + targetMethod + targetDesc + " loadLevel=local" + targetIndex);
        instrumentation.addTransformer(new Patcher());
    }

    /** 供离线自检使用：把一段 class 字节里的目标方法换成「直接 loadLevel」。 */
    public static byte[] patch(byte[] original, String className, String method, String descriptor, int loadLevelSlot) {
        final int slot = loadLevelSlot;
        final boolean[] found = new boolean[1];
        ClassReader reader = new ClassReader(original);
        final ClassWriter writer = new ClassWriter(reader, 0);
        ClassVisitor visitor = new ClassVisitor(Opcodes.ASM9, writer) {
            @Override
            public MethodVisitor visitMethod(int access, String name, String desc, String signature, String[] exceptions) {
                if (!name.equals(method) || !desc.equals(descriptor)) {
                    return super.visitMethod(access, name, desc, signature, exceptions);
                }
                found[0] = true;
                MethodVisitor mv = super.visitMethod(access, name, desc, signature, exceptions);
                mv.visitCode();
                mv.visitVarInsn(Opcodes.ALOAD, slot);
                mv.visitMethodInsn(Opcodes.INVOKEINTERFACE, "java/lang/Runnable", "run", "()V", true);
                mv.visitInsn(Opcodes.RETURN);
                // maxLocals 必须覆盖「所有参数」，哪怕我们只用到了其中一个：
                // 只按 ALOAD 的槽位算会被 JVM 拒（ClassFormatError: Arguments can't fit into locals）
                int argumentSlots = 1;  // this
                for (Type type : Type.getArgumentTypes(descriptor)) {
                    argumentSlots += type.getSize();
                }
                mv.visitMaxs(1, Math.max(slot + 1, argumentSlots));
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

    private static final class Patcher implements ClassFileTransformer {
        @Override
        public byte[] transform(ClassLoader loader, String className, Class<?> redefined,
                                ProtectionDomain domain, byte[] classfileBuffer) {
            if (classfileBuffer == null || !targetClass.equals(className)) {
                return null;
            }
            try {
                byte[] patched = patch(classfileBuffer, className, targetMethod, targetDesc, targetIndex);
                if (patched == null) {
                    log("类已加载，但没有匹配的方法：" + className + "." + targetMethod + targetDesc);
                    return null;
                }
                log("已跳过确认页：" + className + "." + targetMethod + targetDesc);
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
