# MSA-Kit 0.3.0 验证记录

[English](validation.md) | **简体中文**

## 独立算法目录验证（2026-11-21）

[algorithm/align.cpp](../algorithm/align.cpp) 的默认实现取自此前全量实测的精修版，原始核心源码的 SHA-256 为 `ae9a9abf2cfefa7d057d5294168d30374e7e6e24e69d7b0f219beec5e643cc18`。双序列线性缺口罚分为 −3，合并插入槽后执行一轮半径为 4 列的精修。

- GCC 9.5 Release、GCC 13.1 Release 和 GCC 13.1 Debug 加 ASan/UBSan 三种构建各通过全部四项 CTest。独立穷举检查复现了 160 组合成短序列的精修结果，其中 32 组在精修后消除了空列。
- 评测适配器通过六项单元测试。用 `benchmark/build.sh` 重新构建，复跑下表八份输入，输出 FASTA 与历史精修版逐字节一致。RNA255 第 1 号样本的 SHA-256 为 `a7098e81c816d17ef4d3850b226ec5eaf5f98e6f28bcea80a20587ec1950d489`，也与全量实测时一致。
- 在独立副本中，将 [examples/minimal_core.cpp](../examples/minimal_core.cpp) 复制到 `algorithm/align.cpp`，仅替换这一份源码。两项 `contract` 测试均通过。对 `examples/tiny.fasta`，命令行和 `benchmark/run.sh` 均输出了示例约定的三行右侧补缺口结果。
- 此前在完整 EasyMSA 冻结面板上，该精修版完成了 26 组中的 24 组。1,000 条和 10,000 条的真实线粒体数据均达到 1,800 秒时限。20 组有参考比对的数据全部完成，按数据集等权计算的平均 Q 为 0.978992，平均 TC 为 0.723065。

## 参考核心的准确率与开销（2026-11-21）

基线为提交 `1c2593e`，使用线性 gap −4 和普通星比对。新核心使用线性 gap −3，合并后做一轮半径为 4 列的精修。Q 为恢复的参考残基对比例，TC 为完整恢复的参考列比例，分母取至少含两个残基的参考列。评分器按标题对应各行，检查去 gap 后的序列，用整数累计分子和分母。

| 输入 | 分组 | 基线 Q | 新 Q | 基线 TC | 新 TC |
| --- | --- | ---: | ---: | ---: | ---: |
| RNA255 #1 | 选参 | 0.980157 | 0.992436 | 0.672067 | 0.825016 |
| mt80 | 选参 | 0.904122 | 0.944396 | 0.558023 | 0.688395 |
| mt95 | 选参 | 0.993966 | 0.994676 | 0.956750 | 0.963958 |
| RNA255 #3 | 留出 | 0.985906 | 0.992189 | 0.645578 | 0.796643 |
| RNA255 #7 | 留出 | 0.987628 | 0.993292 | 0.631783 | 0.750000 |
| mt70 | 留出 | 0.846211 | 0.894734 | 0.394656 | 0.502290 |
| mt85 | 留出 | 0.940959 | 0.967944 | 0.684100 | 0.799861 |
| mt99 | 留出 | 0.999919 | 0.999919 | 0.998696 | 0.998696 |

[RNA 数据包](https://lab.malab.cn/soft/WMSA2/data/RNA-255.zip)中的 `steps-128` 样本均使用完整的 255 条序列。[模拟 mtDNA 数据包](https://lab.malab.cn/~tfr/HAlign3_testdata/mt_like_diff_similarity.tar.xz)用于构造有参考比对的小规模 DNA 片段。各相似度组按原始顺序取第 0、3、…、93 行，共 32 条。选参使用第 1 号样本的参考列 [2000, 3536)，留出使用第 2 号样本的参考列 [8000, 9536)，下标从 0 开始。去掉 gap 后作为输入，对应的带 gap 行单独用于评分。

选参集上比较了 2 至 6 分的五种线性 gap 代价，以及两种区分转换与颠换的参数（匹配 +2、转换 −1、颠换 −2、gap −6 或 −8），每种均比较有无精修。先在每组内平均 Q 和 TC，再按数据类型平均，DNA 与 RNA 等权。冻结参数后运行五组留出数据。在 EasyMSA 的 RNA255 输入上，仅将 −4 改为 −3 得到 Q 0.975293、TC 0.640959。保留 −4 并做精修得到 Q 0.992376、TC 0.819831。最终组合完整恢复 1,543 个可评分参考列中的 1,273 列，基线为 1,037 列。

| 端到端开销，三次中位数 | 基线耗时 | 新耗时 | 基线峰值 RSS | 新峰值 RSS |
| --- | ---: | ---: | ---: | ---: |
| RNA255 #1 | 1.592 秒 | 1.417 秒 | 40.81 MiB | 40.81 MiB |
| mt80 片段 | 0.261 秒 | 0.275 秒 | 32.54 MiB | 32.54 MiB |

在共享服务器上绑定同一 CPU，交错运行基线与新版本。两者使用 GCC 13.1 Release 和同一 EasyMSA 适配器，计时包含掩码、比对、字符恢复和文件读写，评分在计时后执行。新实现的两两比对内循环使用替换得分查表，在精修前释放星比对的插入槽数据。

正式程序在八组输入上的输出均与选定候选逐字节一致。仓库的 `benchmark/build.sh` 和 `benchmark/run.sh` 也复现了 RNA255 的结果。GCC 9.5 Release、GCC 13.1 Release 和 GCC 13.1 Debug + ASan/UBSan 三种构建的四组 CTest 均通过。另用独立穷举程序验证 160 组短序列，最终各行完全吻合，其中 32 组在精修后消除了空列。服务器与本地副本在 `build-star-tuning/` 中保留了输入清单、选参结果、输出校验值、逐次资源测量和测试日志。

## C++17 迁移（2026-10-27）

本记录对应 2026-10-27 在 Linux 服务器实际执行的 C++17 迁移验收，起点提交为 `a880fa7787e5a603c7b5db62924a972d8681e03e`。公开核心参数改为 const vector 引用，派生项目需同步签名并重建；七文件结构和输入输出行为不变。这里记录历史执行结果，本次文档本地化不表示重新执行了这些实验。

## 环境与源码

| 项目 | 观察值 |
| --- | --- |
| 平台 | Linux 6.8.0-136-generic，x86_64，glibc 2.35 |
| 编译器 | GCC 13.1.0、GCC 9.5.0 |
| 语言 | `-std=c++17`，标准必需、禁用扩展 |
| CMake | 3.26.4 |
| 系统 zlib | 1.2.11 |
| 测试 Python | 3.10.12 |
| 依赖 | CLI11 2.6.1、spdlog 1.16.0、doctest 2.4.12、固定 kseq 及安全补丁 |
| 安装版本 | `msakit 0.3.0` |

HAlign-5 基线 `b0d5608811d44e30bc323da54f0583ceaaf684d2`。[依赖清单](../extern/provenance.json) 的 138 个头文件/许可证哈希已核对，含原始/分发 kseq 校验值与修改说明。

src 恰为 main、cli、input、output、validation、align 六个 cpp 加 internal.hpp。核心、回调、验证和输出使用不复制记录的 const vector 引用；其余兼容编辑为前后缀函数和常量表达式数组填充的 C++17 写法。评分、回溯、槽合并、依赖、输入夹具及算法断言未改变；测试编辑仅涉及签名、前缀检查和版本预期。

三个测试构建各 11 个编译单元在 compile_commands.json 中均恰使用 `-std=c++17`，runtime-only 另查，核心无第三方 include 目录。实际验证只覆盖上述两版 GCC，不推断任意 C++17 编译器。

## 已完成构建与测试

| CTest | 用例/方法数 | GCC 13.1 Release | GCC 13.1 Debug+ASan/UBSan | GCC 9.5 Release |
| --- | ---: | --- | --- | --- |
| `unit.contract` | 29 | 通过 | 通过 | 通过 |
| `unit.reference` | 8 | 通过 | 通过 | 通过 |
| `integration.contract` | 63 | 通过 | 通过 | 通过 |
| `integration.reference` | 2 | 通过 | 通过 | 通过 |
| 合计 | **102** | **4/4** | **4/4** | **4/4** |

每构建 37 个 doctest 共 8,863 断言：contract 3,888、reference 4,975。Python 方法含参数化子场景，无跳过；doctest 的 skipped 是互补标签过滤，完整运行已覆盖所有用例。

CTest 时长依次为 8.21、39.36、8.58 秒，是测试套件时长而非比对性能。均新目录、零失败、首次编译/测试通过，Sanitizer 无报错；保留既有行为断言和对齐答案。

其他实际检查：contract 2/2；右补 gap 的替代核心通过全部 63 Python contract 方法；GCC 13.1 `BUILD_TESTING=OFF` 不查 Python，完成隔离前缀安装、版本、小型 FASTA，FASTQ stdin `@one description\nacTu\n+\n!!!!\n` 得 `>one description\nACTU\n`。

三个构建各测七个短输出名 `x`、`gz`、`.g`、`.gz`、`x.gz`、`x.GZ`、`x.gzx`，21 场景均通过，仅 `.gz`、`x.gz` 压缩并独立解压，检查后缀大小写、输入不变、quiet、临时清理；不计入 CTest 用例数。另查链接、依赖哈希、七文件和本地/服务器副本。

## 输入行为覆盖

- 32 合法目录例×四输入×三输出为 384 组合；含 FASTA/FASTQ/混合、多行、标题、前导内容、CR/EOF、重复、清洗。
- 22 内容错误×四输入为 88；所选失败另加 80 命名输出生命周期组合；检查新目标无、force 旧目标及输入不变、stdout 空、无临时残留。
- FASTQ 专项另有 16 质量不变、36 gzip 字段切分、12 大字段边界。
- gzip 保留 8 合法、13 损坏、26 截断前缀；识别后的 gzip 错误 4，完整解压但内容错 3；零/一字节魔数按普通输入。
- 模块枚举 256 字节；结构标记/CR 单测；80 个 16 KiB/64 KiB 缓冲边界；FASTA/FASTQ 短读、EINTR、读错和关闭故障。
- 六个 kseq 专项检查部分初始化、长 FASTQ 每次分配、失败 realloc 所有权、长度/算术、每字节读错、短读；资源映射 5，不做巨型分配。
- 输入生成保留 seed 20261026、32 组；CLI、输出发布、链接/同文件、错误核心、参考算法持续运行。

13 组 MSA：六固定各六条，七生成 seed 20261027、最多 128 条；127 通道/重排/折行场景在三构建及替代核心均通过。独立检查标题、位置、数量、清洗后残基、非空等宽、合法字符、无全 gap 列；检查器自测接受两种合法布局，拒绝 16 错误和 2 折行错误。最大评分表 263,682 格，约 2.1 MB；大字节输入单序列，避免大 DP。这些是合成软件测试。

## 保留的解析器比较

此前 kseq 迁移用原始和补丁头对相同输入比较：seed 20261026 生成 100,000 短字节流，各以 1、3、17、16,384 字节块读取，400,000 次返回值/name/comment/sequence/quality 均一致，涵盖前导内容、标记、空白、CR、NUL、高位字节。C++17 迁移未重跑这一历史比较，因为解析器未改；新模块套件仍跑分配、读错和字节清洗。结论仅覆盖已测输入。

比较源码、原始/补丁头、日志在服务器和本地忽略目录 `build-kseq-review/`。

## 复现命令

以下命令从仓库根目录运行。编译器绝对路径须对应本机已有版本；不修改系统软件。

```bash
cmake -S . -B build-cpp17-gcc13-release -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_CXX_COMPILER=/usr/bin/g++-13 -DBUILD_TESTING=ON
cmake --build build-cpp17-gcc13-release --parallel 2
(cd build-cpp17-gcc13-release && ctest --output-on-failure)

cmake -S . -B build-cpp17-gcc13-sanitize -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_CXX_COMPILER=/usr/bin/g++-13 \
  -DBUILD_TESTING=ON -DMSAKIT_ENABLE_SANITIZERS=ON
cmake --build build-cpp17-gcc13-sanitize --parallel 2
(cd build-cpp17-gcc13-sanitize && ctest --output-on-failure)

cmake -S . -B build-cpp17-gcc9-release -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_CXX_COMPILER=/usr/bin/g++-9 -DBUILD_TESTING=ON
cmake --build build-cpp17-gcc9-release --parallel 2
(cd build-cpp17-gcc9-release && ctest --output-on-failure)

(cd build-cpp17-gcc13-release && ctest -L contract --output-on-failure)

cmake -S . -B build-cpp17-runtime -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_CXX_COMPILER=/usr/bin/g++-13 -DBUILD_TESTING=OFF
cmake --build build-cpp17-runtime --parallel 2
cmake --install build-cpp17-runtime --prefix "$PWD/build-cpp17-runtime/install"
./build-cpp17-runtime/install/bin/msakit --version
```

```bash
mkdir -p build-cpp17-evidence
printf '#!/bin/sh\nexec "%s/build-cpp17-gcc13-release/msakit_test_driver" valid "$@"\n' "$PWD" \
  > build-cpp17-evidence/alternate-core
chmod +x build-cpp17-evidence/alternate-core
python3 tests/integration.py --binary build-cpp17-evidence/alternate-core \
  --test-driver build-cpp17-gcc13-release/msakit_test_driver --suite contract
```

记录时另外使用观察版 CMake 支持的 `--output-junit cpp17-test-results.xml`，并开 CMAKE_EXPORT_COMPILE_COMMANDS。报告选项不改变声明的 CMake 3.18 最低版本；CXX_STANDARD_REQUIRED=ON、CXX_EXTENSIONS=OFF。替代核心命令也包含在上方。

完整原始日志在 `build-cpp17-evidence/gcc13-release-full.log`、`gcc13-sanitize-full.log`、`gcc9-release-full.log`，不被后续标签运行覆盖。同目录还有 run_validation.py、构建/CTest/contract/替代核心/runtime 日志、commands.json、环境、编译标准、短输出及前后哈希。JUnit 在各构建目录，本地忽略目录保留证据副本。

## Sanitizer 与历史

仅 Sanitizer 保留 `-fno-pie`/`-no-pie`。0.1.0 阶段空 PIE 检查程序曾复现间歇启动崩溃，non-PIE 对照未复现；Release 保留正常布局。ASan 泄漏和立即失败、UBSan 立即失败和堆栈保持启用，无抑制列表或主机配置修改。

0.1.0 输入/MSA 日志保留在 `build-input-tests-evidence/`、`build-msa-evidence/`，后者含 13 数据集的早期输入输出与宽度表；kseq 和模块拆分记录在 `build-kseq-evidence/`、`build-modular-evidence/`。C++17 验收使用本记录的新运行结果，不用历史结果替代。

## 解释与限制

验证软件约定和有界参考核心正确性，不测生物准确率或大规模性能。kseq 原生语义与 0.1.0 的差异见 [设计](design.zh-CN.md#4-input-contract)。蛋白/profile/并行/非 Linux 不在范围；声明最低 CMake 3.18，实际使用 3.26.4。此次历史实现交付没有提交、推送、标签或发行；本地化未改变该历史状态。
