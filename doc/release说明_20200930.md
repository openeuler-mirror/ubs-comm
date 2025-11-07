## Release notes:
### HCOM 22.0.0
Date: 2022/09/30  
Summary: HCOM is an easy to use, high performance library for various hardware including RoCE/IB, Eth, UB etc. This is the first RC release for RDMA protocol, which hides all the complexities of RoCE and IB etc, and also provides high performance.     
Major features:
- RDMA two side operation with/without opcode
- RDMA one side operation including memory region registering
- RDMA Endpoint establishing with OOB socket
- Multiple threads support including groups
- Busy polling and event polling support
- Cpu binding support
- Self polling endpoint support
- Dynamically load verbs library
- OOB connection support with both TCP and UDS
- Multiple OOB listeners support
- Load balance support including Round-Robin and Hash policy
- Heartbeat support for RDMA
- Connection version support
- Data crypt for two side operation, OOB uses TLS 1.3 and AES_128_GCM for data crypt over RDMA
- Dynamically load openssl library, 1.1.1f and later version
- Two side TLS verification support
- External log function support
- Both C and C++ API support
- Providing includes files, .a and .so library 

Note: This is a limited release to DCS for internal integration only, not suitable for production and PoC externally.