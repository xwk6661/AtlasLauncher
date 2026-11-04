// SPDX-License-Identifier: GPL-3.0-only
/*
 * 把 ASM 的包名从 org/objectweb/asm 换到 atlas/introskip/shaded/asm。
 *
 * 为什么必须重定位：-javaagent 的 jar 会被挂到系统类路径上，它的类对整个游戏可见。
 * 如果保持原名，Forge / Fabric 自带的 ASM 就可能被我们这份（版本不同）顶掉 ——
 * 上游的 quickboot.jar 也是这么做的（mcml/shaded/asm）。
 *
 * 用法：java -cp asm-9.7.jar;asm-tree-9.7.jar;asm-commons-9.7.jar Relocate <asm.jar> <outDir>
 */

import java.io.ByteArrayOutputStream;
import java.io.InputStream;
import java.io.OutputStream;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.Paths;
import java.util.Enumeration;
import java.util.zip.ZipEntry;
import java.util.zip.ZipFile;

import org.objectweb.asm.ClassReader;
import org.objectweb.asm.ClassWriter;
import org.objectweb.asm.commons.ClassRemapper;
import org.objectweb.asm.commons.Remapper;

public final class Relocate {
    private static final String FROM = "org/objectweb/asm";
    private static final String TO = "atlas/introskip/shaded/asm";

    public static void main(String[] args) throws Exception {
        if (args.length != 2) {
            System.err.println("usage: Relocate <asm.jar> <outDir>");
            System.exit(1);
        }
        Path outDir = Paths.get(args[1]);
        Remapper remapper = new Remapper() {
            @Override
            public String map(String internalName) {
                if (internalName.startsWith(FROM)) {
                    return TO + internalName.substring(FROM.length());
                }
                return internalName;
            }
        };

        int count = 0;
        try (ZipFile zip = new ZipFile(args[0])) {
            Enumeration<? extends ZipEntry> entries = zip.entries();
            while (entries.hasMoreElements()) {
                ZipEntry entry = entries.nextElement();
                String name = entry.getName();
                if (!name.endsWith(".class") || name.startsWith("META-INF/") || name.equals("module-info.class")) {
                    continue;
                }
                try (InputStream in = zip.getInputStream(entry)) {
                    ClassReader reader = new ClassReader(readAll(in));
                    ClassWriter writer = new ClassWriter(0);
                    reader.accept(new ClassRemapper(writer, remapper), 0);

                    Path target = outDir.resolve(TO + name.substring(FROM.length()));
                    Files.createDirectories(target.getParent());
                    try (OutputStream out = Files.newOutputStream(target)) {
                        out.write(writer.toByteArray());
                    }
                    count++;
                }
            }
        }
        System.out.println("relocated " + count + " classes -> " + outDir.resolve(TO));
    }

    private static byte[] readAll(InputStream in) throws Exception {
        ByteArrayOutputStream buffer = new ByteArrayOutputStream();
        byte[] chunk = new byte[8192];
        int read;
        while ((read = in.read(chunk)) > 0) {
            buffer.write(chunk, 0, read);
        }
        return buffer.toByteArray();
    }
}
