#!/bin/sh
# 构建 introskip.jar（提交到仓库的是产物，源码留在这里以便复现）。
#
# 依赖：JDK 8+（javac/jar）、ASM 9.7 的 core / tree / commons 三个 jar
#   https://repo1.maven.org/maven2/org/ow2/asm/asm/9.7/asm-9.7.jar
#   https://repo1.maven.org/maven2/org/ow2/asm/asm-tree/9.7/asm-tree-9.7.jar
#   https://repo1.maven.org/maven2/org/ow2/asm/asm-commons/9.7/asm-commons-9.7.jar
#
# 用法：./build.sh <asm-core.jar> <asm-tree.jar> <asm-commons.jar>
set -e

HERE=$(cd "$(dirname "$0")" && pwd)
ASM_CORE=${1:?usage: build.sh <asm-core.jar> <asm-tree.jar> <asm-commons.jar>}
ASM_TREE=${2:?usage: build.sh <asm-core.jar> <asm-tree.jar> <asm-commons.jar>}
ASM_COMMONS=${3:?usage: build.sh <asm-core.jar> <asm-tree.jar> <asm-commons.jar>}
OUT="$HERE/out"
JAR="$HERE/../../launcher/resources/agents/introskip.jar"
ASM_CP="$ASM_CORE:$ASM_TREE:$ASM_COMMONS"

rm -rf "$OUT"
mkdir -p "$OUT/classes" "$OUT/tools"

# 1) 重定位工具（只在构建期用）
javac -source 8 -target 8 -Xlint:-options -cp "$ASM_CP" -d "$OUT/tools" "$HERE/relocate/Relocate.java"

# 2) ASM 打进 atlas/introskip/shaded/asm —— 不能占用 org/objectweb/asm，
#    否则 Forge/Fabric 自带的 ASM 可能被这份顶掉
java -cp "$OUT/tools:$ASM_CP" Relocate "$ASM_CORE" "$OUT/classes"

# 3) Agent 本身（对重定位后的 ASM 编译）
javac -source 8 -target 8 -Xlint:-options -cp "$OUT/classes" -d "$OUT/classes" \
      "$HERE/src/atlas/introskip/Agent.java"

cat > "$OUT/MANIFEST.MF" <<'EOF'
Manifest-Version: 1.0
Premain-Class: atlas.introskip.Agent
Can-Retransform-Classes: false
EOF

jar cfm "$JAR" "$OUT/MANIFEST.MF" -C "$OUT/classes" .
echo "built $JAR"
