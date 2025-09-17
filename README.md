rewrite affnity service using C. more light way
# affinityServerC

`affinityServerC` 是一个轻量化的 Windows 平台进程 CPU 亲和性管理工具，用 C 语言编写。它可以自动设置自身以及配置文件中指定的进程的 CPU 核心亲和性。此外，它还支持将 **Process Lasso** 的特定配置片段转换为本程序所需的格式。

-----

### 功能

  * 设置程序自身的 CPU 亲和掩码。
  * 根据配置文件，遍历系统进程并设置其 CPU 亲和性。
  * 支持 **Process Lasso** 配置文件格式的转换，以便在本程序中使用。
  * 日志记录功能，默认输出到文件，也可通过命令行参数切换至控制台输出。
  * 自动检测程序是否以管理员权限运行，并提供相应的提示。
  * 可自定义遍历进程的时间间隔。
  * 可查找并列出当前 CPU 亲和性为系统默认（全核心）的进程，并支持指定黑名单来排除特定进程。

-----

### 命令行参数

| 参数 | 说明 | 默认值 |
| :--- | :--- | :--- |
| `-affinity <number>` | 设置本程序自身的 CPU 亲和掩码。支持二进制（`0b`）、十六进制（`0x`）和十进制格式，例如 `0b11110000` 或 `254`。 | `0` |
| `-console` | 在控制台输出日志。 | 默认输出到文件 |
| `-find` | 寻找亲和性为系统默认（全核心）的进程。 | 默认不启用 |
| `-convert` | 执行 **Process Lasso** 文件转换并退出。 | 默认不启用 |
| `-plfile <file>` | 指定 **Process Lasso** 的配置源文件，必须为 UTF-8 编码。此文件应包含 `DefaultAffinitiesEx=` 后的配置片段。 | `prolasso.ini` |
| `-out <file>` | **Process Lasso** 文件转换后的输出文件名。 | `output.ini` |
| `-blacklist <file>` | 指定 `-find` 功能的黑名单配置文件。 | 默认不启用 |
| `-interval <ms>` | 遍历进程的停滞时间间隔（毫秒）。 | `10000` |
| `-config <file>` | 指定本程序的配置文件。 | `config.ini` |
| `-help` / `--help` / `/?` | 输出帮助信息并退出。 | - |

-----

### 使用方法

**注意：** 所有命令参数都有默认值或默认不启用。你可以直接双击运行程序，它会根据默认设置运行(如果默认的配置文件名的文件存在且内容不为空)。

#### 1\. 设置自身亲和性

```bash
affinityService.exe -affinity 0b11110000
```

> 二进制字符串表示 CPU 核心，低位开始对应核心编号（从 `cpu0` 开始）。此示例将 `cpu8` 到 `cpu15` 核心的亲和性分配给本程序。

#### 2\. 设置遍历间隔与日志输出

```bash
affinityService.exe -interval 5000 -console
```

  * 程序将每隔 5000 毫秒遍历一次进程。
  * 日志信息将输出到控制台，而不是写入日志文件。

#### 3\. 从 Process Lasso 配置转换

```bash
affinityService.exe -convert -plfile lasso.ini -out out.ini
```

  * 此命令将从 `lasso.ini` 读取 **Process Lasso** 的配置片段。
  * 然后生成本程序可用的配置文件 `out.ini`。
  * 转换完成后，程序将自动退出。
  * **Process Lasso** 配置源文件应只包含不换行的单行配置片段，例如：
    ```bash
    steamwebhelper.exe,0,8-19,everything.exe,0,8-19
    ```

#### 4\. 指定配置文件

```bash
affinityService.exe -config processAffinityServiceConfig.ini
```

  * 程序将根据 `processAffinityServiceConfig.ini` 文件中的设置，持续遍历系统进程并应用 CPU 亲和性。

-----

### 日志文件

  * 默认日志文件路径为 `logs\YYYYMMDD.log`。
  * 如果使用 `-console` 参数，日志将只输出到控制台，不会生成日志文件。
  * 日志系统会避免连续输出重复信息。

-----

### 配置文件格式

```txt
# 文件示例: processAffinityServiceConfig.ini
# 格式: 进程文件名, CPU 亲和掩码
# 支持 # 开头的注释行
steamwebhelper.exe,254
everything.exe,65535
```

> CPU 亲和掩码是 64 位整型（`int64`）。例如，`254` 对应的二进制为 `0b11111110`，表示核心 `1` 到 `7`。

-----

### 注意事项

  * 建议以管理员权限运行程序，否则可能无法修改某些进程的亲和性。

-----

### 联系

项目由 `prohect@foxmail.com` 开发与维护。

----
