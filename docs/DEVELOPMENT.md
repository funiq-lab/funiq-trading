# 开发指南

推荐先用 Docker 编译和运行，再根据需要修改代码。这样不用在自己的电脑上逐个安装编译工具和依赖，环境也更容易保持一致。

## 准备服务商 SDK

完整系统依赖 ATP 交易 SDK 和 KSQMD 行情 SDK。仓库不附带服务商的头文件、动态库、公钥或授权文件；编译前请向对应服务商获取与现有适配代码兼容的 Linux x86_64 版本。

文件清单与放置路径分别见 [ATP SDK 说明](../lib/atp/README.md)和 [KSQMD SDK 说明](../lib/ksqmd/README.md)。这些本地文件已由 Git 忽略，不要强制加入提交。未准备 SDK 时，完整系统构建会提示缺少依赖。

策略示例的独立测试不依赖这些服务商 SDK：

```bash
cmake -S tests -B build/example-tests -DCMAKE_BUILD_TYPE=Debug
cmake --build build/example-tests
ctest --test-dir build/example-tests --output-on-failure
```

该测试需要 C++17 编译器、CMake、nlohmann_json 及项目公共头文件所依赖的 SQLiteCpp 头文件。

## 使用 Docker 编译

先安装并启动 Docker，再下载项目：

```bash
git clone https://github.com/funiq-lab/funiq-trading.git
cd funiq-trading

# 首次使用：准备开发环境
(cd docker && bash build.sh)

# 编译项目
bash docker/run.sh build
```

两个脚本的作用：

- `docker/build.sh`：创建开发用的 Docker 镜像，安装编译器和依赖。需要在 `docker` 目录中运行。
- `docker/run.sh`：创建或进入容器，也提供编译、调试和清理命令。可以在项目根目录运行。

平时修改代码后，再执行一次 `bash docker/run.sh build` 即可，不必每次重建镜像。

### 常用命令

以下命令都在电脑终端中执行，运行前先进入项目目录：

| 命令 | 作用 |
| --- | --- |
| `bash docker/run.sh build` | 编译 Release 版本 |
| `bash docker/run.sh build-debug` | 编译 Debug 版本，便于检查问题 |
| `bash docker/run.sh` | 进入容器 |
| `bash docker/run.sh debug` | 进入容器，使用 GDB 等调试工具 |
| `bash docker/run.sh clean` | 删除上次的编译结果 |
| `bash docker/run.sh --help` | 查看完整命令 |

切换 Debug 和 Release 前，可以先执行 `clean`，避免沿用上次的编译设置。

编译结果位于：

```text
build/build_x64/Release/bin/ft_trading_client  # Release 主程序
build/build_x64/Release/lib/                  # Release 所需的库
build/build_x64/Debug/bin/ft_trading_client    # Debug 主程序
build/build_x64/Debug/lib/                    # Debug 所需的库
```

## 准备配置

首次使用，在项目根目录复制配置模板。下面的命令不会覆盖已有文件：

```bash
cp -n .env.example .env
cp -n src/system_config.example.json system_config.json
cp -n src/quote/config/quote_config.example.json quote_config.json
cp -n src/trade/config/trade_config.example.json trade_config.json
cp -n src/strategy/config/strategy_config.example.json strategy_config.json
cp -n src/quote/ksqmd_quote_parser/config/api_config_ex.example.xml api_config_ex.xml
cp -n src/trade/atp_trade_handler/config/atp_quant_api.example.xml atp_quant_api.xml
mkdir -p data/backup
```

按自己的环境填写行情连接、交易账户和文件路径。`DEMO_*`、`CHANGE_ME*` 等是占位内容，不能用于登录。使用数据回放时，还需要把 `mock_data_file` 改成实际数据路径。

各配置项的含义见[在线配置文档](https://funiq-trading.funiqlab.com/getting-started/configuration)。自己的账号、密码、日志和数据库不要提交到仓库。

## 启动程序

先在自己的电脑终端中进入容器：

```bash
bash docker/run.sh
```

然后在容器里运行：

```bash
cd /app
export LD_LIBRARY_PATH=/app/build/build_x64/Release/lib
./build/build_x64/Release/bin/ft_trading_client
```

`LD_LIBRARY_PATH` 告诉程序去哪里找运行所需的库。运行 Debug 版本时，将上面两处 `Release` 改为 `Debug`。

从 `/app` 启动，是为了让程序找到项目根目录的 `.env` 和配置文件。业务服务会根据接口调用和交易时间设置启动、停止。

如果要从自己的电脑访问容器里的 HTTP 服务，在本地 `.env` 中把 `HTTP_SERVER_ADDRESS` 设置为 `0.0.0.0`，然后访问：

```bash
curl http://127.0.0.1:8080/health
```

## 修改代码

电脑上的项目目录与容器里的 `/app` 是同一份文件。可以直接在自己的编辑器中修改文件，再用快捷命令重新编译。

| 要做什么 | 从哪里看起 |
| --- | --- |
| 了解程序如何启动 | `src/main.cpp` |
| 开发策略 | `include/strategy/base_strategy.h`、`src/strategy/` |
| 增加行情源 | `include/quote/`、`src/quote/` |
| 增加交易接口 | `include/trade/`、`src/trade/` |
| 修改 HTTP 接口 | `src/network/handler/http/` |
| 修改 TWAP 拆单 | `src/algorithm/twap.cpp` |
| 修改数据库读写 | `src/database/` |
| 修改 Python 客户端 | `examples/client_test/apis.py` |

### 添加自己的策略

1. 在 `src/strategy/strategies/` 下新建目录，参考现有示例，继承 `BaseStrategy`。
2. 实现初始化、启动、停止、释放等方法，按需要处理行情、订单和成交回报。
3. 为新策略添加 `CMakeLists.txt`，并在上级 CMake 文件中加入新目录和库。
4. 在 `src/strategy/strategy_adapter.cpp` 中包含头文件，并将策略加入 `_create_builtin_strategy_factory()` 和 `get_builtin_strategy_modules()`。
5. 在本地 `strategy_config.json` 中填写策略名称和参数，再重新编译。

当前策略需要在代码中注册，单独添加文件或修改 JSON 不会自动生效。可先用模拟行情或回放数据检查调用是否正常。

**现有策略示例只展示调用方式，不具备业务含义，不发送真实订单。**

### 增加行情或交易接口

- 行情接入参考 `src/quote/ksqmd_quote_parser/`，继承 `BaseQuoteParser`。
- 交易接入参考 `src/trade/atp_trade_handler/`，继承 `BaseTradeHandler`。
- 按现有模块实现连接、启停、消息处理和导出函数，并在 CMake 中加入新模块。
- 编译后，在本地配置中填写新模块的名称。

## 手动编译（不使用 Docker）

以下命令适用于 Linux x86_64。需要支持 C++17 的 GCC／Clang、CMake 3.16+ 和 Git；现有 SDK 动态库也需要与系统匹配。

Ubuntu／Debian 安装依赖：

```bash
sudo apt-get update
sudo apt-get install -y \
  build-essential cmake git pkg-config \
  libboost-all-dev nlohmann-json3-dev libsqlite3-dev
```

在项目根目录编译：

```bash
cmake -S . -B build/local -DCMAKE_BUILD_TYPE=Release -DBUILD_PRODUCTION=OFF
cmake --build build/local --parallel 2
```

结果在 `build/local/build_x64/Release/`。准备配置后，从项目根目录运行：

```bash
LD_LIBRARY_PATH="$PWD/build/local/build_x64/Release/lib" \
  ./build/local/build_x64/Release/bin/ft_trading_client
```

首次编译时，CMake 会从 GitHub 下载固定版本的 SQLiteCpp。如果使用 CMake 4，遇到旧版本兼容性提示，可在配置命令中增加 `-DCMAKE_POLICY_VERSION_MINIMUM=3.5`。

## 常见问题

| 问题 | 怎么处理 |
| --- | --- |
| Docker 提示无法连接 | 确认 Docker 已启动 |
| 镜像或依赖下载失败 | 检查网络，以及 Docker／Git 的代理设置 |
| 修改代码后没有生效 | 重新运行 `bash docker/run.sh build` |
| 切换编译模式后出错 | 先运行 `clean`，再编译需要的版本 |
| 找不到 `.env` 或配置文件 | 从项目根目录启动；容器内对应 `/app` |
| 找不到 `.so` 文件 | 检查 `LD_LIBRARY_PATH` 是否指向当前版本的 `lib` 目录 |
| 新策略无法使用 | 检查是否加入 CMake、策略注册代码及本地配置 |

更多配置和 API 用法见[在线文档](https://funiq-trading.funiqlab.com)。
