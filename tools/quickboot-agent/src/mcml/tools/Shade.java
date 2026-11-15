package mcml.tools;

import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.Paths;
import java.util.stream.Stream;

import org.objectweb.asm.ClassReader;
import org.objectweb.asm.ClassWriter;
import org.objectweb.asm.commons.ClassRemapper;
import org.objectweb.asm.commons.Remapper;

/**
 * 打包期工具：把内嵌的 ASM 从 `org/objectweb/asm` 重定位到 `mcml/shaded/asm`。
 *
 * 为什么必须这么做（Fabric 实测）：
 * agent jar 会出现在 JVM 的 classpath 上，而 Fabric 的 Knot 在启动时检查
 * "classpath 上是不是有多份 ASM"，发现两份就直接拒绝启动：
 *
 *   java.lang.IllegalStateException: duplicate ASM classes found on classpath:
 *     .../asm-9.10.1.jar!/org/objectweb/asm/ClassReader.class,
 *     .../mcml-agent.jar!/org/objectweb/asm/ClassReader.class
 *
 * Forge 目前不检查，但任何自带 ASM 的模组都可能踩同一个坑。重定位之后
 * jar 里只有 `mcml/shaded/asm/*`，与别人的 ASM 互不相干。
 *
 * 用法（由 spikes/build-agent.mjs 调用）：
 *   java -cp <asm-all.jar>;<编译输出> mcml.tools.Shade <输入目录> <输出目录>
 */
public final class Shade {

    /** 原包前缀 */
    private static final String FROM = "org/objectweb/asm/";
    /** 重定位后的前缀 */
    private static final String TO = "mcml/shaded/asm/";

    private Shade() {
    }

    public static void main(String[] args) throws Exception {
        if (args.length < 2) {
            System.err.println("usage: Shade <inDir> <outDir>");
            System.exit(2);
        }
        final Path in = Paths.get(args[0]);
        final Path out = Paths.get(args[1]);

        Remapper remapper = new Remapper() {
            @Override
            public String map(String internalName) {
                if (internalName.startsWith(FROM)) return TO + internalName.substring(FROM.length());
                return internalName;
            }

            @Override
            public Object mapValue(Object value) {
                if (value instanceof String) {
                    String s = (String) value;
                    if (s.startsWith("org.objectweb.asm.")) {
                        return "mcml.shaded.asm." + s.substring("org.objectweb.asm.".length());
                    }
                    if (s.startsWith("org/objectweb/asm/")) {
                        return "mcml/shaded/asm/" + s.substring("org/objectweb/asm/".length());
                    }
                }
                return super.mapValue(value);
            }
        };

        final int[] count = new int[2];
        try (Stream<Path> files = Files.walk(in)) {
            files.filter(Files::isRegularFile).forEach(p -> {
                try {
                    String rel = in.relativize(p).toString().replace('\\', '/');
                    byte[] bytes = Files.readAllBytes(p);
                    if (!rel.endsWith(".class")) {
                        // 非 class 文件原样复制（目前没有，留着以防将来加资源）
                        Path target = out.resolve(rel);
                        Files.createDirectories(target.getParent());
                        Files.write(target, bytes);
                        return;
                    }
                    ClassReader reader = new ClassReader(bytes);
                    ClassWriter writer = new ClassWriter(0);
                    // ClassRemapper 会同时改写：类名、父类、字段/方法描述符、签名、常量池里的类型引用
                    reader.accept(new ClassRemapper(writer, remapper), 0);
                    String newRel = rel.startsWith(FROM) ? TO + rel.substring(FROM.length()) : rel;
                    Path target = out.resolve(newRel);
                    Files.createDirectories(target.getParent());
                    Files.write(target, writer.toByteArray());
                    count[0]++;
                } catch (Exception e) {
                    throw new RuntimeException("relocate failed: " + p + " -> " + e, e);
                }
            });
        }
        // ASCII 输出：构建脚本的终端是 UTF-8，而 Java 在中文 Windows 上按 GBK 打印会乱码
        System.out.println("relocated " + FROM + " -> " + TO + " (" + count[0] + " classes)");
    }
}
