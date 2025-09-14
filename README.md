rewrite affnity service using C. more light way
# affinityServerC

`affinityServerC` 是一个轻量化的 Windows 平台的进程 CPU 亲和性管理工具，支持自动设置自身和指定进程的 CPU 核心亲和，同时支持从 [Process Lasso](https://bitsum.com/) 的特定配置片段转换为本程序使用的配置格式。

---

## 功能

- 设置自身 CPU 亲和掩码
- 遍历系统进程，根据配置文件设置指定进程的 CPU 亲和
- 如果有指定-find true，会找到亲和性为系统默认（全核心）的进程，并排除（如果指定了）黑名单然后输出
- 支持 ProcessLasso 配置文件转换为本程序的配置格式
- 支持日志记录（可选择输出到控制台，默认开启）
- 支持自定义遍历间隔时间
- 支持命令行参数控制所有功能

---

## 命令行参数

| 参数 | 说明 | 默认值 |
|------|------|--------|
| `-affinity <binary>` | 设置本程序自身的 CPU 亲和掩码，二进制字符串 | `0b00001111111100000000` |
| `-console` | 在控制台输出日志而非文件, 不需要额外参数 | - |
| `-find` | 寻找亲和性为系统默认（全核心）的进程, 不需要额外参数 | - |
| `-plfile <file>` | 指定 ProcessLasso 配置文件（DefaultAffinitiesEx=行之后的部分） | `prolasso.ini` |
| `-outfile <file>` | ProcessLasso 文件转换后的输出文件名 | `config.ini` |
| `-blacklist <file>` | 指定-find的黑名单 | - |
| `-convert` | 执行 ProcessLasso 文件转换并退出, 不需要额外参数 | - |
| `-interval <ms>` | 遍历进程的停滞时间间隔（毫秒） | `10000` |
| `-config <file>` | 指定本程序的配置文件 | `processAffinityServiceConfig.ini` |
| `-help` / `--help` / `/?` | 输出帮助信息 | - |

---

## 使用方法

### note:所有命令指定的参数都有默认值或者不启用，可以直接双击以空参数运行该程序

### 1. 设置自身亲和

```bash
affinityService.exe -affinity 0b0000_0000_0000_0000_1111_1111_0000_0000
```

> 二进制字符串表示 CPU 核心，低位开始对应核心编号，从cpu0开始，此处为8-15设定给本程序。

---

### 2. 设置遍历进程的间隔和日志输出

```bash
affinityService.exe -interval 5000 -console
```

* 每 5000 毫秒遍历一次进程
* 在控制台输出日志（不写入 logs 文件夹）

---

### 3. 从 ProcessLasso 文件转换配置

```bash
affinityService.exe -convert -plfile lasso.ini -outfile config.ini
```

* 从 `lasso.ini` 中读取Process Lasso的配置片段
* 生成本程序可用的配置文件 `config.ini`
* 执行完毕后程序退出
* Process Lasso的配置片段的文件请包含且只包含这一部分，不换行 eg.
```bash
steamwebhelper.exe,0,8-19,everything.exe,0,8-19
```


> 支持范围格式，例如 `1-7`，自动转换为 int32 CPU 亲和掩码。

---

### 4. 指定配置文件

```bash
affinityService.exe -config processAffinityServiceConfig.ini
```

* 根据指定配置文件读取每个进程的 CPU 亲和掩码
* 持续遍历系统进程并应用亲和设置

---

## 日志文件

* 默认输出到 `logs\YYYYMMDD.log`
* 选择输出到控制台（`-console`）时不输出到 `logs\YYYYMMDD.log`

---

## 配置文件格式

```txt
# 文件示例: processAffinityServiceConfig.ini
# 格式: 进程文件名,int32 CPU 亲和掩码
steamwebhelper.exe,254
everything.exe,65535
```

> CPU 亲和掩码是 int64，二进制表示核心，例如 254 对应 cores 1-7。

---

## 注意事项

* 建议以管理员权限运行，否则可能无法修改某些进程的亲和性

---


## 联系

项目由 `prohect@foxmail.com` 开发与维护。

----
