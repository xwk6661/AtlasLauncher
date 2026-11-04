// SPDX-License-Identifier: GPL-3.0-only
/*
 * 离线自检：读出某个 class、用 Agent 的补丁函数改写、写回文件。
 * 之后用 javap 看一眼目标方法是不是只剩 loadLevel.run()。
 *
 * 用法：java -cp introskip.jar SelfTest <in.class> <cls> <method> <desc> <index> <out.class>
 */

import java.nio.file.Files;
import java.nio.file.Paths;

import atlas.introskip.Agent;

public final class SelfTest {
    public static void main(String[] args) throws Exception {
        if (args.length != 6) {
            System.err.println("usage: SelfTest <in.class> <cls> <method> <desc> <index> <out.class>");
            System.exit(1);
        }
        byte[] original = Files.readAllBytes(Paths.get(args[0]));
        byte[] patched = Agent.patch(original, args[1], args[2], args[3], Integer.parseInt(args[4]));
        if (patched == null) {
            System.err.println("NO MATCH: " + args[1] + "." + args[2] + args[3]);
            System.exit(2);
        }
        Files.write(Paths.get(args[5]), patched);
        System.out.println("patched " + args[1] + "." + args[2] + args[3] + " -> " + args[5]);
    }
}
