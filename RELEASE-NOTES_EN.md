1.0.0
Initial version

`HCOM` supports the following features:

1. The underlying layer of `HCOM` supports multiple types of NIC hardware and communication protocols (such as `RDMA`, `TCP`, `SHM`, and `UB`). It shields the differences between different types of hardware or transmission protocols and provides a uniform API for developers. In addition, `HCOM` provides `QoS` capabilities (such as flow control, fault detection, and message retransmission) and authentication and encryption capabilities for developers to use.

2. `HCOM` achieves ultimate performance through the combination of software and hardware. For some common NIC models (such as MLX5), the hardware acceleration feature is enabled. For different scenarios, the software implements acceleration features such as multi-thread management, `RNDV` (Rendezvous protocol, used in large-packet scenarios), and `MultiRail` (network port aggregation to fully utilize network bandwidth).
