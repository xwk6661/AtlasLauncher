package mcml.agent;

import java.util.HashMap;
import java.util.Map;

/** Agent 参数解析：`k=v,k=v` 形式 */
public final class Config {

    public final Map<String, String> values = new HashMap<String, String>();

    public static Config parse(String args) {
        Config c = new Config();
        if (args == null || args.isEmpty()) return c;
        for (String part : args.split(",")) {
            int eq = part.indexOf('=');
            if (eq <= 0) continue;
            c.values.put(part.substring(0, eq).trim(), part.substring(eq + 1).trim());
        }
        return c;
    }

    /**
     * 从系统属性里读参数。
     *
     * 为什么需要这条路：Forge 的 LaunchClassLoader 会**再加载一份** `mcml.agent.*`
     * （它的类源里包含 agent jar），于是被注入的字节码调用的是另一份 Hook，
     * 那份的静态字段是全新的 —— `Agent.config` 是空的，`world` 读出来是 null，
     * 表现就是「menu init #1 打了，然后什么都不发生」。
     * 系统属性是 JVM 全局的，跨 ClassLoader 共享，正好用来把参数递过去。
     */
    public static Config fromSystem() {
        return parse(System.getProperty("mcml.agent.args", ""));
    }

    public String get(String key, String fallback) {
        String v = values.get(key);
        return v == null || v.isEmpty() ? fallback : v;
    }

    public int getInt(String key, int fallback) {
        try {
            return Integer.parseInt(get(key, String.valueOf(fallback)));
        } catch (NumberFormatException e) {
            return fallback;
        }
    }

    public boolean getBool(String key) {
        String v = values.get(key);
        return v != null && (v.equals("1") || v.equalsIgnoreCase("true"));
    }

    /**
     * 带缺省的布尔读取：**没传这个键**时用 `fallback`，而不是一律 false。
     *
     * 为什么需要它：新增开关（如 `autoEnter`）必须做到"老参数、老 jar 行为不变" ——
     * 启动器只在**关**的时候才拼 `autoEnter=0`，所以"没这个键"等于"开"。
     * 用无缺省的那个重载会把"没传"读成 false，功能就默认全关了。
     *
     * 认 `0/false` 与 `1/true`（大小写不敏感）；其他值按缺省处理，不抛异常 ——
     * 参数是启动器拼的，写错一个值不该让游戏起不来。
     */
    public boolean getBool(String key, boolean fallback) {
        String v = values.get(key);
        if (v == null || v.isEmpty()) return fallback;
        if (v.equals("1") || v.equalsIgnoreCase("true")) return true;
        if (v.equals("0") || v.equalsIgnoreCase("false")) return false;
        return fallback;
    }
}
