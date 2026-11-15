package mcml.agent;

import java.io.FileWriter;
import java.io.IOException;
import java.text.SimpleDateFormat;
import java.util.Date;

/**
 * 极简日志：同时写 stdout 和文件。
 *
 * 不引日志框架的原因：Agent 在 premain 阶段加载，此时游戏的 log4j 还没初始化，
 * 而且我们只关心"什么时候发生了什么"，时间戳比级别重要。
 */
public final class Log {

    private static final SimpleDateFormat FMT = new SimpleDateFormat("HH:mm:ss.SSS");
    private static FileWriter file;
    private static boolean openAttempted = false;
    private static long t0 = System.currentTimeMillis();

    private Log() {
    }

    /**
     * 打开日志文件（**追加**模式）。
     *
     * 追加而不是覆盖，是因为进程里可能有两个 `mcml.agent.Log`（Forge 的 LaunchClassLoader
     * 会再加载一份），两份都会打开同一个文件 —— 用覆盖模式会互相把对方的行擦掉。
     */
    public static synchronized void open(String path) {
        openAttempted = true;
        try {
            file = new FileWriter(path, true);
        } catch (IOException e) {
            System.out.println("[mcml-agent] 无法写入日志文件 " + path + ": " + e);
        }
    }

    /**
     * 懒打开：野生副本（被 LaunchClassLoader 加载的那份）没有走 premain，
     * 文件句柄是空的，只能从系统属性里取路径自己打开。
     * 这一条很关键 —— 启动器现在靠 tail 这个文件收事件，缺了它模组实例就"没有事件"。
     */
    private static synchronized void ensureOpen() {
        if (file != null || openAttempted) return;
        open(System.getProperty("mcml.agent.log", "mcml-agent.log"));
    }

    public static synchronized void info(String msg) {
        ensureOpen();
        long elapsed = System.currentTimeMillis() - t0;
        String line = "[mcml-agent] +" + elapsed + "ms " + msg;
        System.out.println(line);
        if (file != null) {
            try {
                // 文件里也带 `[mcml-agent]` 前缀：启动器 tail 它的时候与 stdout 用同一套解析
                file.write(line + "\n");
                file.flush();
            } catch (IOException ignored) {
                // 日志失败不能影响游戏
            }
        }
    }

    public static synchronized void close() {
        if (file != null) {
            try {
                file.close();
            } catch (IOException ignored) {
            }
            file = null;
        }
    }
}
