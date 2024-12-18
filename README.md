# DPU-BF3

## samples
- flow_drop: 基于DOCA Flow实现的丢包功能,同时监督两个端口上的包,丢掉符合预设IP五元组的包,剩下的从另一个端口再发回链路
- eth_txq_send_ethernet_frames: 基于DOCA ETH实现的发包功能,使用ARM核构造以太网帧并发包
- pcc: 基于DOCA PCC实现的自定义拥塞控制功能,在DPA上响应网卡事件并指使网卡调整速率和发送RTT探测包
