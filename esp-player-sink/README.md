## ESP侧的麦克风代码(含能力框架)

```bash
├── components              
│   ├── ability_framework   # 能力框架(ability_context为麦克风版本)
│   ├── mic_driver          # 麦克风驱动程序(包含i2s,opus和tcp服务器逻辑, 提供一个microphone_task供能力调用)
│   ├── opus                # opus编解码库
│   ├── udp_broadcast       # udp广播
│   └── wifi_config         # wifi配置
└── main                    # 主程序
```

