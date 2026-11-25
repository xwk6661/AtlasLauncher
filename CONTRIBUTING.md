# 参与 Atlas

感谢你有兴趣。Atlas 是单人维护的项目，合并速度取决于验证成本 —— 下面写清"什么样的贡献能最快被接受"。

## 先看这里

| 你想做什么 | 去哪 |
| --- | --- |
| 报告 bug（导入失败、进不去图、崩溃） | [Issues](https://github.com/xwk6661/AtlasLauncher/issues)，用 bug 模板 |
| 问"怎么用" / 提想法 | [Discussions](https://github.com/xwk6661/AtlasLauncher/discussions) |
| 报告安全漏洞 | **不要开公开 issue**，见 [`SECURITY.md`](SECURITY.md) |
| 改代码 | 先开 issue 说清"症状 → 根因 → 改法"，再提 PR |

提 issue 前请先看包内 `README-Atlas.txt` 的「已知限制」与「出问题了怎么办」——
一半的"进不去图"在那里已有答案。

## 提交 PR

### 验证档位（必须写清）

项目用四档描述"验到哪一步"，**写"通过"必须带档位和范围**：

| 档 | 含义 |
| --- | --- |
| **L0** | 编译通过 |
| **L1** | 单元测试通过（`ctest`，基线 33/33） |
| **L2** | 用真实产物验证（真实 client jar、真实网络、真实站点） |
| **L3** | 真机：在 `atlas.exe` 里操作，游戏真的启动 |

PR 模板里会要求你勾选档位。只到 L0 的改动也可以提，但要**明确写出来**，不要写成"已完成"。

### 改动纪律

这些规则是为了让上游修复还能 `cherry-pick` 进来（见 [`docs/atlas/01-设计.md` §13](docs/atlas/01-设计.md)）：

1. **新功能代码放 `launcher/maps/`**，不要往既有文件里塞大段逻辑；
2. **对既有上游文件只做加法**：不改已有函数签名；需要新 getter 就用新名字 + 默认参数；
3. **不动品牌与网络地址**：`Launcher_META_URL`、`org.prismlauncher.EntryPoint`、packwiz 的
   `x-prismlauncher-*` 三处是技术必需，其余上游 URL 已故意置空，不要"顺手恢复"。

### 代码风格

- 格式化与静态检查配置在仓库根：`.clang-format`、`.clang-tidy`、`.editorconfig`；
- **注释用中文**，与现有代码一致；注释解释"为什么"，不复述"做了什么"；
- 提交信息用中文，前缀沿用现有习惯：`feat(scope):` / `fix(scope):` / `docs:` / `chore:` / `refactor:`；
- 不要提交 `build/`、`dist/`、`.zip`、`.exe`。例外是 `launcher/resources/agents/*.jar` ——
  它们是**提交进仓库的构建产物**，改完源码必须重新出 jar（见 [`BUILDING.md`](BUILDING.md) §6）。

### 分支与合并

- `main` 是发布线，**禁止强推与删除**（仓库规则集强制）；
- 工作分支用 `feature/<主题>` 或 `fix/<主题>`，从 `main` 切出；
- PR 合并后自动删除 head 分支。

## 不接受的改动

- 引入新的联网行为（启动器内下载地图、自动上报、遥测）；
- 改动地图存档数据的自动流程（`notes.json` 是玩家数据，任何自动流程都不许写）；
- 把界面文案改成英文源串而不补翻译（当前源串是中文，国际化是独立议题）；
- 仅改格式的大规模重排（会让 `git blame` 失去意义）。

## 本地检查

```powershell
cmake --build --preset win-local
ctest --test-dir build/win-local -C Debug      # 期望 33/33
git status                                     # 工作区应干净
```

完整构建步骤见 [`BUILDING.md`](BUILDING.md)。
