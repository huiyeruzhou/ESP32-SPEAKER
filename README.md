## ESP侧的扬声器代码(含能力框架)

```bash
├── components              
│   ├── ability_framework   # 能力框架(ability_context为扬声器版本)
│   ├── speaker_driver      # 扬声器驱动程序(包含i2s,opus和tcp服务器逻辑, 提供一个speaker_task供能力框架调用)
│   ├── opus                # opus编解码库
│   ├── udp_broadcast       # udp广播
│   └── wifi_config         # wifi配置
└── main                    # 主程序
```
