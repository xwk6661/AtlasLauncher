# Agent 符号表覆盖矩阵

> 由 `tools/agent_symbol_audit.py` 生成，**每一项都对着真实 client jar 校验过**，不要手改。
> 复核口径：类存在、方法名 + 描述符存在、static 标记一致、方法挂在约定的类上（见脚本头注释）。

- 数据目录里的 client jar：**103** 个
- 已发布符号表：**60** 张（全部通过全槽位复核）
- 官方映射生成：**21** 张 · 字节码指纹生成：**39** 张

## 已发布

| 版本 | 轨道 | 来源 | 类/方法 | 结论 |
| --- | --- | --- | --- | --- |
| 1.6.1 | Agent | 字节码指纹 | 16 | 全部通过 |
| 1.6.2 | Agent | 字节码指纹 | 16 | 全部通过 |
| 1.6.4 | Agent | 字节码指纹 | 16 | 全部通过 |
| 1.7.2 | Agent | 字节码指纹 | 16 | 全部通过 |
| 1.7.3 | Agent | 字节码指纹 | 16 | 全部通过 |
| 1.7.4 | Agent | 字节码指纹 | 16 | 全部通过 |
| 1.7.5 | Agent | 字节码指纹 | 16 | 全部通过 |
| 1.7.6 | Agent | 字节码指纹 | 16 | 全部通过 |
| 1.7.7 | Agent | 字节码指纹 | 16 | 全部通过 |
| 1.7.8 | Agent | 字节码指纹 | 16 | 全部通过 |
| 1.7.9 | Agent | 字节码指纹 | 16 | 全部通过 |
| 1.7.10 | Agent | 字节码指纹 | 16 | 全部通过 |
| 1.8 | Agent | 字节码指纹 | 16 | 全部通过 |
| 1.8.1 | Agent | 字节码指纹 | 16 | 全部通过 |
| 1.8.2 | Agent | 字节码指纹 | 16 | 全部通过 |
| 1.8.3 | Agent | 字节码指纹 | 16 | 全部通过 |
| 1.8.4 | Agent | 字节码指纹 | 16 | 全部通过 |
| 1.8.5 | Agent | 字节码指纹 | 16 | 全部通过 |
| 1.8.6 | Agent | 字节码指纹 | 16 | 全部通过 |
| 1.8.7 | Agent | 字节码指纹 | 16 | 全部通过 |
| 1.8.8 | Agent | 字节码指纹 | 16 | 全部通过 |
| 1.8.9 | Agent | 字节码指纹 | 18 | 全部通过 |
| 1.9 | Agent | 字节码指纹 | 16 | 全部通过 |
| 1.9.1 | Agent | 字节码指纹 | 16 | 全部通过 |
| 1.9.2 | Agent | 字节码指纹 | 16 | 全部通过 |
| 1.9.3 | Agent | 字节码指纹 | 16 | 全部通过 |
| 1.9.4 | Agent | 字节码指纹 | 16 | 全部通过 |
| 1.10 | Agent | 字节码指纹 | 16 | 全部通过 |
| 1.10.1 | Agent | 字节码指纹 | 16 | 全部通过 |
| 1.10.2 | Agent | 字节码指纹 | 16 | 全部通过 |
| 1.11 | Agent | 字节码指纹 | 16 | 全部通过 |
| 1.11.1 | Agent | 字节码指纹 | 16 | 全部通过 |
| 1.11.2 | Agent | 字节码指纹 | 16 | 全部通过 |
| 1.12 | Agent | 字节码指纹 | 18 | 全部通过 |
| 1.12.1 | Agent | 字节码指纹 | 16 | 全部通过 |
| 1.12.2 | Agent | 字节码指纹 | 18 | 全部通过 |
| 1.13 | Agent | 字节码指纹 | 16 | 全部通过 |
| 1.13.1 | Agent | 字节码指纹 | 16 | 全部通过 |
| 1.13.2 | Agent | 字节码指纹 | 16 | 全部通过 |
| 1.14.4 | Agent | 官方映射 | 18 | 全部通过 |
| 1.15 | Agent | 官方映射 | 19 | 全部通过 |
| 1.15.1 | Agent | 官方映射 | 19 | 全部通过 |
| 1.15.2 | Agent | 官方映射 | 19 | 全部通过 |
| 1.16 | Agent | 官方映射 | 19 | 全部通过 |
| 1.16.1 | Agent | 官方映射 | 19 | 全部通过 |
| 1.16.2 | Agent | 官方映射 | 19 | 全部通过 |
| 1.16.3 | Agent | 官方映射 | 19 | 全部通过 |
| 1.16.4 | Agent | 官方映射 | 19 | 全部通过 |
| 1.16.5 | Agent | 官方映射 | 19 | 全部通过 |
| 1.17 | Agent | 官方映射 | 19 | 全部通过 |
| 1.17.1 | Agent | 官方映射 | 19 | 全部通过 |
| 1.18 | Agent | 官方映射 | 19 | 全部通过 |
| 1.18.1 | Agent | 官方映射 | 19 | 全部通过 |
| 1.18.2 | Agent | 官方映射 | 19 | 全部通过 |
| 1.19 | Agent | 官方映射 | 19 | 全部通过 |
| 1.19.1 | Agent | 官方映射 | 19 | 全部通过 |
| 1.19.2 | Agent | 官方映射 | 19 | 全部通过 |
| 1.19.3 | Agent | 官方映射 | 19 | 全部通过 |
| 1.19.4 | Agent | 官方映射 | 19 | 全部通过 |
| 1.21.1 | quickPlay | 官方映射 | 18 | 全部通过 |

## 覆盖缺口：这些版本按设计走普通启动

它们在数据目录里有真实 client jar，但解析不出 Agent 的必需槽位。
运行时按设计降级成「停在主菜单 + 顶部提示」，不会坏档、也不会猜：

| 版本 | 缺什么 / 为什么 |
| --- | --- |
| 1.0 | 缺必需槽位：menuInit, enterWorld, setScreen, saveAllWorlds（worldClient: 没有任何来源给出候选; minecraftServer: 指纹：没有任何类包含 "Saving worlds" 字面量） |
| 1.1 | 缺必需槽位：menuInit, enterWorld, setScreen, saveAllWorlds（worldClient: 没有任何来源给出候选; minecraftServer: 指纹：没有任何类包含 "Saving worlds" 字面量） |
| 1.2.1 | 缺必需槽位：menuInit, enterWorld, setScreen, saveAllWorlds（worldClient: 没有任何来源给出候选; minecraftServer: 指纹：没有任何类包含 "Saving worlds" 字面量） |
| 1.2.2 | 缺必需槽位：menuInit, enterWorld, setScreen, saveAllWorlds（worldClient: 没有任何来源给出候选; minecraftServer: 指纹：没有任何类包含 "Saving worlds" 字面量） |
| 1.2.3 | 缺必需槽位：menuInit, enterWorld, setScreen, saveAllWorlds（worldClient: 没有任何来源给出候选; minecraftServer: 指纹：没有任何类包含 "Saving worlds" 字面量） |
| 1.2.4 | 缺必需槽位：menuInit, enterWorld, setScreen, saveAllWorlds（worldClient: 没有任何来源给出候选; minecraftServer: 指纹：没有任何类包含 "Saving worlds" 字面量） |
| 1.2.5 | 缺必需槽位：menuInit, enterWorld, setScreen, saveAllWorlds（worldClient: 没有任何来源给出候选; minecraftServer: 指纹：没有任何类包含 "Saving worlds" 字面量） |
| 1.3.1 | 缺必需槽位：menuInit（worldClient: 没有任何来源给出候选; menuInit: 指纹：主菜单类里没有 (II)V 方法（1.13+ 的 init 签名可能变了，需要映射表）） |
| 1.3.2 | 缺必需槽位：menuInit（worldClient: 没有任何来源给出候选; menuInit: 指纹：主菜单类里没有 (II)V 方法（1.13+ 的 init 签名可能变了，需要映射表）） |
| 1.4.2 | 缺必需槽位：menuInit（worldClient: 没有任何来源给出候选; menuInit: 指纹：主菜单类里没有 (II)V 方法（1.13+ 的 init 签名可能变了，需要映射表）） |
| 1.4.4 | 缺必需槽位：menuInit（worldClient: 没有任何来源给出候选; menuInit: 指纹：主菜单类里没有 (II)V 方法（1.13+ 的 init 签名可能变了，需要映射表）） |
| 1.4.5 | 缺必需槽位：menuInit（worldClient: 没有任何来源给出候选; menuInit: 指纹：主菜单类里没有 (II)V 方法（1.13+ 的 init 签名可能变了，需要映射表）） |
| 1.4.6 | 缺必需槽位：menuInit（worldClient: 没有任何来源给出候选; menuInit: 指纹：主菜单类里没有 (II)V 方法（1.13+ 的 init 签名可能变了，需要映射表）） |
| 1.4.7 | 缺必需槽位：menuInit（worldClient: 没有任何来源给出候选; menuInit: 指纹：主菜单类里没有 (II)V 方法（1.13+ 的 init 签名可能变了，需要映射表）） |
| 1.5.1 | 缺必需槽位：menuInit（worldClient: 没有任何来源给出候选; menuInit: 指纹：主菜单类里没有 (II)V 方法（1.13+ 的 init 签名可能变了，需要映射表）） |
| 1.5.2 | 缺必需槽位：menuInit（worldClient: 没有任何来源给出候选; menuInit: 指纹：主菜单类里没有 (II)V 方法（1.13+ 的 init 签名可能变了，需要映射表）） |
| 1.14 | 缺必需槽位：setScreen, saveAllWorlds（worldClient: 没有任何来源给出候选; setLevel: 指纹：参数类型（客户端世界类）没有可靠指纹，需映射表或版本微字典） |
| 1.14.1 | 缺必需槽位：setScreen, saveAllWorlds（worldClient: 没有任何来源给出候选; setLevel: 指纹：参数类型（客户端世界类）没有可靠指纹，需映射表或版本微字典） |
| 1.14.2 | 缺必需槽位：setScreen, saveAllWorlds（worldClient: 没有任何来源给出候选; setLevel: 指纹：参数类型（客户端世界类）没有可靠指纹，需映射表或版本微字典） |
| 1.14.3 | 缺必需槽位：setScreen, saveAllWorlds（worldClient: 没有任何来源给出候选; setLevel: 指纹：参数类型（客户端世界类）没有可靠指纹，需映射表或版本微字典） |

> 1.0–1.2.5 没有集成服务端（`saveAllWorlds` / `stopServer` 不存在），安全退出无从判定；
> 1.3–1.5.2 的主菜单构建方法在字节码里有多个同签名候选，指纹给不出唯一解；
> 1.14–1.14.3 早于 Mojang 发布 `client_mappings`（1.14.4 起才有），
> 而结构上存在 3 个 setScreen 候选、14 个 `(Z)V` 候选 —— 按「绝不猜」的原则不生成。

## 本轮生成明细

| 版本 | 结果 | 说明 |
| --- | --- | --- |
| 1.0 | incomplete | 缺必需槽位：menuInit, enterWorld, setScreen, saveAllWorlds（worldClient: 没有任何来源给出候选; minecraftServer: 指纹：没有任何类包含 "Saving worlds" 字面量） |
| 1.1 | incomplete | 缺必需槽位：menuInit, enterWorld, setScreen, saveAllWorlds（worldClient: 没有任何来源给出候选; minecraftServer: 指纹：没有任何类包含 "Saving worlds" 字面量） |
| 1.2.1 | incomplete | 缺必需槽位：menuInit, enterWorld, setScreen, saveAllWorlds（worldClient: 没有任何来源给出候选; minecraftServer: 指纹：没有任何类包含 "Saving worlds" 字面量） |
| 1.2.2 | incomplete | 缺必需槽位：menuInit, enterWorld, setScreen, saveAllWorlds（worldClient: 没有任何来源给出候选; minecraftServer: 指纹：没有任何类包含 "Saving worlds" 字面量） |
| 1.2.3 | incomplete | 缺必需槽位：menuInit, enterWorld, setScreen, saveAllWorlds（worldClient: 没有任何来源给出候选; minecraftServer: 指纹：没有任何类包含 "Saving worlds" 字面量） |
| 1.2.4 | incomplete | 缺必需槽位：menuInit, enterWorld, setScreen, saveAllWorlds（worldClient: 没有任何来源给出候选; minecraftServer: 指纹：没有任何类包含 "Saving worlds" 字面量） |
| 1.2.5 | incomplete | 缺必需槽位：menuInit, enterWorld, setScreen, saveAllWorlds（worldClient: 没有任何来源给出候选; minecraftServer: 指纹：没有任何类包含 "Saving worlds" 字面量） |
| 1.3.1 | incomplete | 缺必需槽位：menuInit（worldClient: 没有任何来源给出候选; menuInit: 指纹：主菜单类里没有 (II)V 方法（1.13+ 的 init 签名可能变了，需要映射表）） |
| 1.3.2 | incomplete | 缺必需槽位：menuInit（worldClient: 没有任何来源给出候选; menuInit: 指纹：主菜单类里没有 (II)V 方法（1.13+ 的 init 签名可能变了，需要映射表）） |
| 1.4.2 | incomplete | 缺必需槽位：menuInit（worldClient: 没有任何来源给出候选; menuInit: 指纹：主菜单类里没有 (II)V 方法（1.13+ 的 init 签名可能变了，需要映射表）） |
| 1.4.4 | incomplete | 缺必需槽位：menuInit（worldClient: 没有任何来源给出候选; menuInit: 指纹：主菜单类里没有 (II)V 方法（1.13+ 的 init 签名可能变了，需要映射表）） |
| 1.4.5 | incomplete | 缺必需槽位：menuInit（worldClient: 没有任何来源给出候选; menuInit: 指纹：主菜单类里没有 (II)V 方法（1.13+ 的 init 签名可能变了，需要映射表）） |
| 1.4.6 | incomplete | 缺必需槽位：menuInit（worldClient: 没有任何来源给出候选; menuInit: 指纹：主菜单类里没有 (II)V 方法（1.13+ 的 init 签名可能变了，需要映射表）） |
| 1.4.7 | incomplete | 缺必需槽位：menuInit（worldClient: 没有任何来源给出候选; menuInit: 指纹：主菜单类里没有 (II)V 方法（1.13+ 的 init 签名可能变了，需要映射表）） |
| 1.5.1 | incomplete | 缺必需槽位：menuInit（worldClient: 没有任何来源给出候选; menuInit: 指纹：主菜单类里没有 (II)V 方法（1.13+ 的 init 签名可能变了，需要映射表）） |
| 1.5.2 | incomplete | 缺必需槽位：menuInit（worldClient: 没有任何来源给出候选; menuInit: 指纹：主菜单类里没有 (II)V 方法（1.13+ 的 init 签名可能变了，需要映射表）） |
| 1.14 | incomplete | 缺必需槽位：setScreen, saveAllWorlds（worldClient: 没有任何来源给出候选; setLevel: 指纹：参数类型（客户端世界类）没有可靠指纹，需映射表或版本微字典） |
| 1.14.1 | incomplete | 缺必需槽位：setScreen, saveAllWorlds（worldClient: 没有任何来源给出候选; setLevel: 指纹：参数类型（客户端世界类）没有可靠指纹，需映射表或版本微字典） |
| 1.14.2 | incomplete | 缺必需槽位：setScreen, saveAllWorlds（worldClient: 没有任何来源给出候选; setLevel: 指纹：参数类型（客户端世界类）没有可靠指纹，需映射表或版本微字典） |
| 1.14.3 | incomplete | 缺必需槽位：setScreen, saveAllWorlds（worldClient: 没有任何来源给出候选; setLevel: 指纹：参数类型（客户端世界类）没有可靠指纹，需映射表或版本微字典） |
