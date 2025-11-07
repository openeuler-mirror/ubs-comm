### service layer includes
- multiple rails
- RR mode
- QoS
- etc  
1 多平面 p0
- 单性多通道 (比如数据面的通道有n个通道, 时序问题)
- 多性质通道 (控制面消息面 数据消息面)
- 以整体通道channel
- 多卡 (p2)
```
FAQ1: 时序问题?
- API不保证数据在多通道到上的时序问题，由上层保证；有时序要求的消息，下一命令需要等上一个完成
- SendAfterWrite() 要特殊考虑

FAQ2: 通道选择问题?
- Round-Robin 默认

FAQ3: 建链?
- 所有数量成功后才成功
- 使用过程中部分链路断链后行为, 有3种行为; 由上层配置模式
  - 断开其他的，且通知上层链路断开
  - 重连已断开的，a) 如果重连成功, 打印message, 上层不感知; b) 如果重连失败, 打印error msg, 通知上层, 且关闭其他链接
  - 重连但不成功, 保留剩余的链接; 打印message 且通知上层

FAQ4: 多性质多通道?
- 单性质多通道, 为一个channel, 一个channel有多个ep
- 多性质多通道, 为多个channel, 一次创建多个, 返回channel数据; 
  如
  NetService::Instance()
  NetService::Start()/Stop()
  NetService::CreateChannels()
  NetChannelPtr chs[2], NetChannelPtr ctrlCh = chs[0]; NetChannelPtr dataCh = chs[1];

FAQ5: callback register
- 第1种, channel new/broken, 为service级
- 第2种, idle callback, 为service级别
- 第3种, 收到消息 opCode级别, 为service级别; 2种模式, 可以为统一的, 也可以为per opCode, 但二者不能共存
Notes: 注册函数不可动态修改

FAQ6: channel中多ep如何分布在worker上?
- RR策略
- Hash策略
- 所有ep在同一个worker上? (P1)

FAQ7: 不同的channl要不要 workerGroups?
- 创建service和创建channels指定参数 

```

2 ReqResp模式 p0
- 双向
- 只有Req
- 同步, CV/sem
  self polling
- 异步, 函数级别callback?

```
FAQ1: one way
- 暴露一个api

FAQ2: 异步RR, 什么级别的callback?
- 2种, per call 和 per channel
  case1: 如果有per call, 使用per call
  case2: 如果没有per call, 有per channel, 使用per channel的
  case3: 都没有报错 

FAQ3: 同步API有2种模式,
- 非self polling的, CV/SEM, 细节待定
- self polling的 (p1,部分细节再讨论)
  case 1: 多ep per channel
  = rdma需要把多个qp放到一个cq
  = 需要把多fd放到一个poll
  case 2: 1 ep per channel, 直接使用 (p0)
 
FAQ4: timeout 
```

3 QoS:
给上层一个确定的结果，无法恢复了，屏蔽闪断的情况
超时 p0
重连 p0
重发 p0

反压 p1, 给上层调用者一个反馈

4 IO优先级 (P2)

5 单/双边操作的内存分配 (p1, transport层)
- 大页注册
- 较大块内存的不同size的分配
- 可动态增大

6 callback per opcode (p0)