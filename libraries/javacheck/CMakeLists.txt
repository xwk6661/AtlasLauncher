cmake_minimum_required(VERSION 3.15)
project(launcher Java)
find_package(Java 1.7 REQUIRED COMPONENTS Development)

include(UseJava)
set(CMAKE_JAVA_JAR_ENTRY_POINT JavaCheck)
set(CMAKE_JAVA_COMPILE_FLAGS -target 7 -source 7 -Xlint:deprecation -Xlint:unchecked)

set(SRC
    JavaCheck.java
)

add_jar(JavaCheck ${SRC})
# COMPONENT Runtime: without it the jar lands in "Unspecified", which the Windows
# portable packaging (tools/package-windows.ps1) never installs - the launcher then
# ships without jars/JavaCheck.jar and every Java check fails with
# "Java checker library could not be found".
install_jar(JavaCheck DESTINATION "${JARS_DEST_DIR}" COMPONENT Runtime)
