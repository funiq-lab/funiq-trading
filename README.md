# Funiq Trading

基于 C++17 的行情、交易接入与策略执行框架。模块包括行情解析、交易通道、策略管理、共享内存、SQLite 持久化与 HTTP API。

## 策略示例

内置 `StopProfitLossStrategy` 是教学用的纯模拟持仓退出示例。它接收行情，计算配置持仓相对 `entry_price` 的变化，达到止盈或止损阈值后记录一次模拟退出信号。它不调用交易通道，不创建真实订单，也不模拟撮合、交易费用、A股 T+1 或可卖数量限制。

配置见 `src/strategy/config/strategy_config.example.json`。`market` 为 1（上交所）或 2（深交所），`profit_threshold`、`loss_threshold` 单位均为百分比。通过 `get_strategy_data` 查询 `status`、`positions`、`signals`。停止后可以恢复；重新初始化会重置模拟持仓与信号。运行中不能更新配置。

无需商业 SDK 即可编译并运行独立示例测试（需要 CMake、C++17 编译器、nlohmann_json 和 SQLiteCpp 头文件）：

```sh
cmake -S tests -B build/example-tests -DCMAKE_BUILD_TYPE=Debug
cmake --build build/example-tests
ctest --test-dir build/example-tests --output-on-failure
```

## 完整系统构建

完整系统主要面向 Linux，依赖 Boost、SQLite、nlohmann_json 与对应行情/交易 SDK。根目录 CMake 构建包含原有厂商接入模块，独立示例测试不验证完整实盘系统。

```sh
cmake -S . -B build/full -DCMAKE_BUILD_TYPE=Debug
cmake --build build/full
```

部署前根据各模块的 `*.example.json`、`*.example.xml`、`.env.example` 创建本地配置。示例中的 `DEMO_*`、`CHANGE_ME*`、`0000` 是占位值，必须替换为自己的配置，不代表可登录账号。不要提交真实配置、日志、数据库或授权材料。

使用 `.env.example` 中的回环地址配置运行 HTTP 服务；完整系统的统一鉴权和日志脱敏仍需在部署时审查。不要将交易接口直接暴露到公网。

## Python 客户端

`examples/client_test/apis.py` 提供 HTTP API 封装，`quote_cli.py` 提供命令行客户端。只读健康检查示例：

```sh
python3 examples/client_test/main.py --host http://127.0.0.1:8080
```

## 许可

项目自有代码按 [MIT License](LICENSE) 发布，版权归 funiq-lab contributors。第三方库、厂商 SDK 及二进制保留其原始许可；项目 MIT 不改变其授权条件。见 [第三方声明](THIRD_PARTY_NOTICES.txt)。厂商 SDK 保留其原始授权条件。
