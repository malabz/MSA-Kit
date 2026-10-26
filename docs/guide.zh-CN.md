# MSA-Kit 中文开发指南

MSA-Kit 0.1.0 是可复制的 DNA/RNA 多序列比对程序模板：复用输入输出、命令行和结果验证代码，把开发工作集中在比对算法上。

仓库已包含 C++20 实现、参考星比对、小型示例和测试。实际执行的构建与测试结果见 [验证记录](validation.md)。

完整接口定义见 [英文设计文档](design.md)，项目概览见 [README](../README.md)。本指南解释如何理解框架和替换核心，不另行定义一套规则。

## 1. 程序分成哪些部分

数据依次经过五个部分：

```text
命令行参数解析
    ↓
FASTA 读取、字符清洗与输入校验
    ↓
可替换的比对核心
    ↓
统一结果验证
    ↓
FASTA / gzip 写出
```

| 部分 | 负责什么 | 替换算法时的处理 |
|---|---|---|
| 命令行层 | 参数、帮助、版本、日志和退出码 | 复用 |
| 输入层 | 普通文件、gzip、管道、标题保存和字符清洗 | 复用 |
| 比对核心 | 接收序列，生成对齐行 | 替换这里 |
| 结果验证层 | 检查行数、顺序、等长和碱基保留 | 复用 |
| 输出层 | 标题恢复、折行、压缩和写入完成检查 | 复用 |

核心拿到的是已准备好的内存数据，不需要知道序列来自文件还是管道，也不需要决定输出格式。

## 2. 核心接收和返回什么

公开接口位于 [include/msakit/core.hpp](../include/msakit/core.hpp)：

```cpp
namespace msakit {

struct SequenceRecord {
    std::string header;
    std::string sequence;
};

struct Alignment {
    std::vector<std::string> rows;
};

Alignment align(std::span<const SequenceRecord> input);

}
```

`header` 是去掉开头 `>` 和行结束符后的完整标题，包括标题内部及两端的空格。输入集合至少包含一条记录；每条 `sequence` 已完成清洗，非空且没有 gap。`std::span<const SequenceRecord>` 表示核心可以读取这些记录，但不能修改它们。

返回值中的 `rows[i]` 对应 `input[i]`。例如第 3 条输入必须仍然对应第 3 条输出；核心内部即使排序或分组，也必须恢复这个对应关系。

记录位置是内部身份，标题可以重复。不要用标题作为唯一键，否则同名记录可能被覆盖。输出层使用原输入的标题，因此核心只需返回对齐后的序列字符串。

核心不能自行解析参数、读写文件或输出 FASTA。失败时抛出异常，由统一入口记录错误并返回退出码；不要返回不完整的结果作为成功输出。

## 3. 进入核心前，序列会怎样变化

首版面向 DNA 和 RNA，采用 HAlign 风格的字符清洗。标题不参与清洗。

| 序列内容 | 处理方式 |
|---|---|
| `A/C/G/T/U/N` | 保留 |
| `a/c/g/t/u/n` | 转为对应大写字符 |
| 其他字符，包括其他 IUPAC 模糊碱基 | 转为 `N` |
| `-` 或 `.` | 报错，要求输入未比对序列 |

FASTA 的 LF 或真正的 CRLF 行结束符不算序列内容。文件末尾没有 LF 的单独 CR 仍是序列字节，会转为 `N`。序列行中的其他字节均按清洗表处理；空格和制表符也会转为 `N`，不会自动删去。空行贡献零个碱基，多行序列拼接后仍须非空。

例如 `acguR` 清洗后是 `ACGUN`。日志会汇总大小写转换数量和其他字符转为 `N` 的数量；已有 `N` 或小写 `n` 不计入未知字符替换数量。

输入必须是 FASTA，第一个非空行必须以 `>` 开始。真正的空行忽略，仅含空格或制表符的行不算空行。空文件、没有非空白内容的标题、空序列或不合法的 FASTA 结构都会报错。支持 LF/CRLF、多行序列和文件末尾无换行；重复标题和重复序列均完整保留，输入顺序不变。FASTQ、URL 下载、蛋白质及已对齐 profile 不在首版支持范围内。

这里的关键是：**结果去 gap 后，必须等于进入核心的清洗后序列。** 对上例而言，校验目标是 `ACGUN`，而不是原来的 `acguR`。

T 与 U 始终保留各自字符。参考核心仅在评分时把它们视为同类，不把 RNA 的 U 写成 T，也不把 T 写成 U。

## 4. 构建与使用

首版支持 Linux，需要 C++20 编译器、CMake ≥ 3.18 和系统 zlib 开发文件。CLI11、spdlog 和 doctest 已随仓库固定版本附带，正常配置和构建不会下载依赖。启用测试时还需要 Python ≥ 3.8；运行 `msakit` 不依赖 Python。依赖版本、来源和许可见 [第三方声明](../THIRD_PARTY_NOTICES.md)。

在仓库根目录构建并运行测试：

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON
cmake --build build --parallel 2
(cd build && ctest --output-on-failure)
```

生成的程序位于 `build/msakit`。以下命令使用仓库中的 [小型合成输入](../examples/tiny.fasta)，并把结果写到标准输出，可以重复运行：

```bash
./build/msakit --version
./build/msakit -i examples/tiny.fasta -o -
```

以下文件输出示例使用已有的 `build` 目录，并显式允许替换同名示例结果：

```bash
./build/msakit -i examples/tiny.fasta -o build/tiny.aligned.fasta --force
./build/msakit -i examples/tiny.fasta -o build/tiny.aligned.fasta.gz --force
```

普通输入和 gzip 输入均可通过管道传入。以下 Bash 示例的第二条管道需要系统 `gzip` 工具：

```bash
set -o pipefail
cat examples/tiny.fasta | ./build/msakit -i - -o -
gzip -c examples/tiny.fasta | ./build/msakit -i - -o -
```

如果只构建程序、不运行测试，可以关闭测试，此时不需要 Python：

```bash
cmake -S . -B build-app -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=OFF
cmake --build build-app --parallel 2
./build-app/msakit -i examples/tiny.fasta -o -
```

输入是否压缩根据 gzip 文件头识别，不依赖扩展名；标准输入也遵循该规则。支持串接的 gzip 成员，成员内容连续组成一个 FASTA 字节流；截断、CRC 错误或末尾非 gzip 数据会报错。输出文件名以小写 `.gz` 结尾时压缩，`-o -` 始终输出普通 FASTA。需要压缩管道结果时，可在程序之后连接压缩工具。

程序只把对齐结果写到运行时的标准输出，日志和错误写到标准错误，避免日志混入 FASTA。

首版参数为 `-i/--input`、`-o/--output`、`--line-width`、`--force`、`--quiet`、`-h/--help` 和 `-v/--version`。输入和输出参数必填；帮助和版本可独立运行，不读取数据或创建输出。

输出默认每行 80 个字符，统一 LF，并保留文件末尾换行；`--line-width` 接受非负十进制整数，`0` 表示不折行。`--quiet` 关闭常规日志，但保留错误。参考核心为单线程固定参数，首版不提供线程、参考序列或评分矩阵选项。

已有输出文件默认受到保护，只有 `--force` 允许替换；输入和输出指向同一文件时始终按参数冲突报错，返回 `2`，包括符号链接和硬链接别名。输出父目录须已存在；拒绝覆盖已有文件和父目录缺失均返回 `4`。文件结果先写入同目录独占临时文件，写入、压缩结束和关闭成功后再发布到目标路径。默认通过原子无覆盖操作保护目标；`--force` 也只用完整结果替换目标。失败会清理本次创建的临时文件并保留原目标，不承诺断电后的持久化恢复。

标准输出不能回滚，管道写入失败时可能已经产生部分字节，调用方必须检查退出码。在 Bash 中连接多个程序时，先设置 `set -o pipefail`，避免末端压缩程序的成功状态掩盖比对程序失败。管道失败仍可能留下重定向创建的部分文件，不能仅凭文件存在判断成功；同目录临时文件保护适用于通过 `-o` 指定的文件输出。

| 退出码 | 含义 |
|---|---|
| `0` | 成功，或成功显示帮助/版本 |
| `2` | 命令行参数错误 |
| `3` | 输入内容不合法 |
| `4` | 文件读取、解压或写入失败 |
| `5` | 核心执行、资源分配或结果约定检查失败 |

## 5. 如何替换成自己的算法

1. 复制或克隆 MSA-Kit，阅读英文设计中的输入、核心和输出约定。
2. 打开 [include/msakit/core.hpp](../include/msakit/core.hpp)，确认 `SequenceRecord`、`Alignment` 和 `align` 的含义。
3. 在 [src/core/star.cpp](../src/core/star.cpp) 中找到标注为 **Reference star alignment — replaceable core** 的实现入口，用自己的算法替换 `msakit::align` 的实现。保持文件位置和函数签名时，无需调整外围代码；拆分或重命名源文件时，相应更新 CMake 的 `msakit_core` 目标。
4. 保持输入只读，并按输入位置填充 `Alignment::rows`。算法内部可以使用不同的数据结构、评分或中心选择方式。
5. 保留通用约定测试，为新算法补充小型正确性测试，并调整参考算法专属断言。参考算法的最优分数、固定 gap 布局和并列规则测试分布在 [核心测试](../tests/test_core.cpp) 和 [集成测试](../tests/integration.py)，新算法不必复现这些布局；重新构建与测试的命令见第 4、7 节。
6. 更新项目名称、算法说明和适用范围，保留实际复用代码的许可及来源声明。

如果确实需要新增参数，应通过命令行层增加明确的配置并传入核心，而不是在核心内部重新读取命令行。这样的扩展属于派生项目的接口变化，需要同步文档和测试。

## 6. 参考星比对具体做什么

参考实现使用第一条输入作为中心，逐条执行 Needleman–Wunsch 全局比对，完整消费中心和查询序列。

评分为匹配 `+1`、错配 `-1`、每个 gap 字符 `-1`。只要一侧为 `N` 就记 `0`，包括 `N/N`；这条规则先于字符相等判断。T/U 按匹配计分，但回溯结果保留原字符。

并列回溯按固定顺序选择：同时消费中心和查询；仅消费中心、查询填 gap；仅消费查询、中心填 gap。这个顺序让参考结果可重复。

长度为 L 的中心具有 L+1 个插入槽，包括第一个碱基之前、相邻碱基之间和最后一个碱基之后。每个槽取所有查询在该处插入长度的最大值，插入串左对齐，右侧补 gap；中心在插入槽中全部填 gap。

例如，仓库中的 [examples/tiny.fasta](../examples/tiny.fasta) 包含以下合成数据：

```fasta
>center
AC
>q1
ATC
>q2
AGGC
```

参考核心按上述规则输出：

```fasta
>center
A--C
>q1
AT-C
>q2
AGGC
```

中心的 A 和 C 之间需要容纳最长的两个插入字符；q1 的一个插入字符 T 在这个槽中左对齐。槽内插入串不会再互相比对，因此该规则只是易读、确定的参考合并方法，不保证最优多序列比对。

两两计算时间为 `O(L × Σmᵢ)`，还需构造并验证最终矩阵。动态规划表逐条查询分配并释放，其峰值取决于中心与最长查询的长度；最终对齐矩阵和插入信息也占用内存。计算和分配前检查尺寸、字节数、评分范围与输出长度溢出。该核心用于小型演示和接口验证，不承诺 HAlign-5 的处理规模。

## 7. 替换后如何确认没有破坏约定

通用接口测试与参考算法测试有独立的 CTest 标签。替换核心后，可先运行：

```bash
(cd build && ctest -L contract --output-on-failure)
```

`reference` 标签用于星比对的特定评分、结果和回溯检查；替换算法时应更新这组测试。未指定标签的 `ctest` 会运行两组。

框架在输出前检查以下条件：

- 输出行数与输入记录数相同，行与输入位置对应。
- 所有对齐行等长且宽度大于零。
- 每行去掉 `-` 后，逐字符等于对应的清洗后输入。
- 输出只包含允许的碱基字符及 `-`，没有全 gap 列。

通用测试覆盖普通 FASTA、gzip、管道、LF/CRLF、末尾无换行、重复标题、清洗规则、已有 gap 拒绝、覆盖保护和写入失败。测试核心故意返回缺行、异长、改写碱基等错误结果或抛出异常，检查框架能否拒绝；内部文件操作替身用于模拟写入、关闭、发布失败和无覆盖竞态。生产程序没有这些测试替身的公开参数或环境变量入口。

参考核心另有小型合成测试，覆盖单条序列、重复序列、前缀/内部/后缀插入、删除、N、T/U、不同插入长度和并列回溯。独立穷举路径用于检查短序列两两最优分数，生成的小型多序列数据用于检查结果属性与可重复性。具体 gap 布局只用于参考核心测试，不能作为所有新算法的唯一正确答案。

CTest 的 `unit` 入口运行 doctest 模块测试，`integration` 入口通过 Python 标准库运行真实子进程、压缩、管道和文件生命周期测试。实际执行结果见 [验证记录](validation.md)。

输入处理用例集中保存在 [输入用例表](../tests/input_cases.py)，每例都有稳定编号、原始字节和独立的预期结果。新增一种输入情况时，可按 [测试矩阵与扩展指南](testing.md) 添加用例，复用文件、标准输入及 gzip 通道检查。失败报告包含用例编号、输入输出通道和错误信息；生成测试使用固定种子，便于复现。

通道一致性检查关注标题、清洗后序列、转换计数和结果约定。不同核心的合法 gap 布局可以不同；压缩输出先独立解压再检查。内部读取操作替身还用于稳定模拟短读、中断和读取、关闭失败，生产命令行没有对应的测试开关。

替换核心后，先运行第 4 节的 Release 构建和完整测试，再运行地址和未定义行为检查：

```bash
cmake -S . -B build-sanitize \
  -DCMAKE_BUILD_TYPE=Debug \
  -DBUILD_TESTING=ON \
  -DMSAKIT_ENABLE_SANITIZERS=ON
cmake --build build-sanitize --parallel 2
(cd build-sanitize && ctest --output-on-failure)
```

## 8. HAlign-5 来源与许可

设计固定参考 [HAlign-5 提交 b0d5608](https://github.com/pinglu-zhang/HAlign-5/tree/b0d5608811d44e30bc323da54f0583ceaaf684d2)，完整提交号为 `b0d5608811d44e30bc323da54f0583ceaaf684d2`。

MSA-Kit 沿用其 C++20、CMake、CLI11、字符查表清洗及缓冲读写思路，并重新明确核心与外围的边界。输入 gap 拒绝、完整标题与位置身份、标准错误日志和输出完成检查属于 MSA-Kit 明确规定的行为；具体来源和改写范围见 [英文设计文档](design.md)。

HAlign-5 的 WFA2、KSW2、MinHash、外部 MSA 命令和参考 profile 流程没有进入首版基础模板。HAlign-5 及依赖的版本、来源和许可说明见 [第三方声明](../THIRD_PARTY_NOTICES.md)。

MSA-Kit 采用 [MIT 许可](../LICENSE)。复制模板开发新项目时，应保留实际复用内容及第三方依赖的版权、许可和来源声明。
