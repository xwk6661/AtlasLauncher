#!/bin/sh
# 构建 quickboot.jar（仓库里提交的是产物，源码留在这里以便复现）。
#
# 这个 jar 是 ≤1.19.4 的「自动进图 + 落盘判定」Agent，由启动器按版本符号表拼参数注入。
#
# 依赖：JDK 8+（javac/jar）、ASM 9.7.1 的 core / tree / commons / analysis 四个 jar
#   https://repo1.maven.org/maven2/org/ow2/asm/asm/9.7.1/asm-9.7.1.jar
#   https://repo1.maven.org/maven2/org/ow2/asm/asm-tree/9.7.1/asm-tree-9.7.1.jar
#   https://repo1.maven.org/maven2/org/ow2/asm/asm-commons/9.7.1/asm-commons-9.7.1.jar
#   https://repo1.maven.org/maven2/org/ow2/asm/asm-analysis/9.7.1/asm-analysis-9.7.1.jar
#
# 为什么是 9.x 而不是上游 Spike 用的 5.2：ASM 5 只能读 class major ≤ 52（Java 8），
# 而 1.17+ 是 Java 17（major 61）。用 5.2 时 transform 会抛 IllegalArgumentException，
# 表现是「注入成功、游戏仍停在主菜单」（2026-11-15 实测 1.19.3）。
#
# 用法：./build.sh <asm-core.jar> <asm-tree.jar> <asm-commons.jar> <asm-analysis.jar>
set -e

HERE=$(cd "$(dirname "$0")" && pwd)
ASM_CORE=${1:?usage: build.sh <asm-core> <asm-tree> <asm-commons> <asm-analysis>}
ASM_TREE=${2:?usage: build.sh <asm-core> <asm-tree> <asm-commons> <asm-analysis>}
ASM_COMMONS=${3:?usage: build.sh <asm-core> <asm-tree> <asm-commons> <asm-analysis>}
ASM_ANALYSIS=${4:?usage: build.sh <asm-core> <asm-tree> <asm-commons> <asm-analysis>}
OUT="$HERE/out"
JAR="$HERE/../../launcher/resources/agents/quickboot.jar"
ASM_CP="$ASM_CORE:$ASM_TREE:$ASM_COMMONS:$ASM_ANALYSIS"

rm -rf "$OUT"
mkdir -p "$OUT/raw" "$OUT/classes"

# 1) Agent 与重定位工具（对原始包名的 ASM 编译；重定位在第 3 步）
javac --release 8 -Xlint:-options -encoding UTF-8 -cp "$ASM_CP" -d "$OUT/raw" \
      "$HERE"/src/mcml/agent/*.java "$HERE"/src/mcml/tools/Shade.java

# 2) ASM 本体解进 raw，和 Agent 一起重定位
for j in "$ASM_CORE" "$ASM_TREE" "$ASM_COMMONS" "$ASM_ANALYSIS"; do
    (cd "$OUT/raw" && jar xf "$(cd "$(dirname "$j")" && pwd)/$(basename "$j")")
done
rm -rf "$OUT/raw/META-INF" "$OUT/raw/module-info.class"

# 3) org/objectweb/asm → mcml/shaded/asm。不重定位的话，classpath 上会出现两份 ASM，
#    Fabric 的 Knot 会直接拒绝启动（见 Shade.java 头注释）。
java -cp "$ASM_CP:$OUT/raw" mcml.tools.Shade "$OUT/raw" "$OUT/classes"

# 4) 构建期工具不进 jar
rm -rf "$OUT/classes/mcml/tools" "$OUT/classes/META-INF" "$OUT/classes/module-info.class"

cat > "$OUT/MANIFEST.MF" <<'EOF'
Manifest-Version: 1.0
Premain-Class: mcml.agent.Agent
Can-Retransform-Classes: true
EOF

jar cfm "$JAR" "$OUT/MANIFEST.MF" -C "$OUT/classes" .
echo "built $JAR"
