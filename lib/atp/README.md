# ATP 交易 SDK（自行获取）

此目录只保留项目自有的构建配置。本仓库不分发 ATP / Archforce 的 SDK 头文件、动态库、公钥、配置或授权文件。

请向华锐或为你提供 ATP 交易接入的服务商获取与项目 `atp_quant_api` 适配代码兼容的 Linux x86_64 SDK，并确认使用和分发范围。其他 ATP 产品或版本不一定兼容；以服务商提供的文档为准。

## 本地目录

在此目录下创建 `include/` 和 `lib/`，将服务商提供的文件按以下结构放置：

```text
lib/atp/
├── CMakeLists.txt
├── README.md
├── include/
│   ├── atp_quant_api.h
│   ├── atp_quant_msg.h
│   ├── atp_quant_constant.h
│   └── atp_quant_export.h
└── lib/
    ├── libatpquantapi.so
    ├── rsa_public_key.pem     # 按服务商说明提供
    └── atp_quant_api.lic      # 按服务商说明提供
```

SDK 若还有依赖文件，应遵循对应版本的服务商说明。不要根据文件名自行生成或冒用公钥、授权材料。

运行配置可参考项目的 `src/trade/atp_trade_handler/config/atp_quant_api.example.xml`；其中用户名、密码和地址均为占位内容，需复制为本地配置并自行填写。

`include/`、`lib/` 及本地授权材料已被 Git 忽略。不要使用 `git add -f` 提交 SDK、真实账号、密码或授权文件。项目的 MIT 许可不覆盖另行取得的服务商 SDK。
