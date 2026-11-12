# HTTP 客户端示例

`apis.py` 是通用 HTTP API 封装，`quote_cli.py --help` 显示命令行用法。

只读检查已启动服务的健康状态：

```sh
python3 main.py --host http://127.0.0.1:8080
```

模拟止盈止损示例无需启动服务器，运行仓库根目录 README 中的独立 CMake 测试即可。该测试直接构造行情快照，不需要账号、授权文件或商业 SDK。

`mock_data_samples` 中的数据仅用于数据格式演示。不要使用真实账号或生产日志作为测试夹具。
