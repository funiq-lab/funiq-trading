# KSQMD 行情 SDK（自行获取）

此目录只保留项目自有的构建配置。本仓库不分发 KSQMD 的 SDK 头文件、动态库或授权材料。

请向为你提供 KSQMD 行情接入的服务商获取与项目适配代码兼容的 Linux x86_64 SDK、接入配置和使用授权。SDK、GCC 与运行环境的兼容性以服务商说明为准。

## 本地目录

在此目录下创建 `include/`，复制服务商提供的头文件，并将匹配当前编译环境的动态库放在本目录：

```text
lib/ksqmd/
├── CMakeLists.txt
├── README.md
├── include/
│   ├── KsHSDef.h
│   ├── KsHSInterfaceEx.h
│   ├── SseExanicStruct.h
│   └── SzseExanicStruct.h
└── libksHsUdp.so
```

服务商若提供多个 GCC 版本的动态库，请按其说明选择，将使用的版本放在上述根目录位置；不要随意混用。

运行配置可参考 `src/quote/ksqmd_quote_parser/config/api_config_ex.example.xml`。用户名、密码、鉴权地址和网卡配置应按服务商提供的实际参数填写到本地配置中。

SDK 头文件、动态库及本地授权材料已被 Git 忽略。不要使用 `git add -f` 提交 SDK、真实账号、密码或授权文件。项目的 MIT 许可不覆盖另行取得的服务商 SDK。
