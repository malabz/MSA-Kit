# MSA-Kit

A reusable template for building multiple sequence alignment tools with consistent input/output conventions and command-line interfaces.

MSA-Kit aims to let developers reuse the surrounding application infrastructure and replace the alignment algorithm at a clearly documented boundary.

## Project scope

The planned template will provide:

- Sequence input parsing and validation.
- Alignment output serialization with documented format conventions.
- A consistent command-line interface, including help, error reporting, and exit codes.
- A clearly marked, replaceable alignment core.
- A simple star-alignment implementation as the reference core.
- Examples and tests for the input/output and algorithm interface contracts.

The star-alignment core will serve as an educational reference implementation. Its role is to demonstrate how an algorithm connects to the shared interfaces.

## Development status

This initial commit establishes the repository and project scope. The input/output layer, command-line interface, alignment core, and tests are planned and have not yet been implemented.

Implementation language, supported formats, and the precise interface contract will be documented as development proceeds.

## Intended developer workflow

1. Copy or clone the template.
2. Review the documented input/output and core interface contracts.
3. Replace the reference alignment core with your own algorithm.
4. Run the interface tests and add algorithm-specific tests.

This workflow describes the intended completed template.

## 项目简介

MSA-Kit 旨在提供可复用的多序列比对程序模板，统一输入解析、输出格式与命令行接口，并明确标注可替换的算法核心。开发者可以复用外围代码，集中实现自己的比对算法。计划使用简单的星比对作为参考核心。

当前仓库处于初始化阶段，尚未实现上述功能。
