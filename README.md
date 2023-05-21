# FreeRTOS_research

## 已经实现：
  
  电脑端的mp3音频解码->PCM->播放->opus编码->opus解码->PCM->播放的过程 （完成测试）
  
  ESP32端联网->i2s初始化->消息接收->调用opus解码->i2s写入dac（播放） （未测试参数）

## 需要购买（不贵）：

  PCM5102(24.85):https://detail.tmall.com/item.htm?id=609136611914&spm=a1z09.2.0.0.78a42e8d5HIRN6&_u=u1me3j564efb&skuId=4700782880577
  
  喇叭（6.8）：https://item.taobao.com/item.htm?spm=a1z09.2.0.0.78a42e8d5HIRN6&id=662920761254&_u=u1me3j56f4d3
  
  PCM5102需要焊一下针脚座，如果没有烙铁，记着买个便宜的烙铁套装，如果没有杜邦线记得买几个母对母的连接esp32

## TODO:
- [ ] 发包测试，用电脑一秒给esp32发包400个，esp32设置每次接收usleep(2500)，其实就是2.5ms，打上时间戳看看是否稳定
- [ ] 外接DAC音频测试，I2S的config我没仔细测试，建议先找个头文件中为PCM输入的demo来测试DAC可以正常解码和通过喇叭发声
- [ ] DAC正常工作，证明可以正常接收PCM输入，然后就是把电脑端的opus编码结果通过tcp发送给esp32，使用opus解码器解码得到PCM再给DAC

## 以上依次完成后进入到调优过程
- [ ] 调优包括三方面：
- [ ] OPUS参数调优，涉及编码部分参数控制
- [ ] 发送接收过程和缓存调优
- [ ] FreeRTOS任务调优

## 第二阶段完成后进入第三阶段WAMR调试
- [ ] 写native api，这部分先等之前完成再看吧
