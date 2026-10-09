### code structure

#### 3 layers:
Layer 1: API layer
```
file all start with hcom

include cxx api and c api

cxx api including
1 net related things
2 obj pool
3 ring buffer
4 lockless ring buffer
5 execution service
6 blocking ring buffer

c api only include
1 net related thing
2 and ring buffer and blocking queue with spinlock

```
 
Layer 2: logic layer and adaptive layer
```
which are plug-in for api layer
and also calling wrapper layer

net_rdma_* is plug-in for rdma

net_tcp_* is plug-in for tcp
```
 
Layer 3: Wrapper layer  
```
which is just simple wrapper raw api 

for example:

rdma related wrappers all start with rdma_

rdma_verbs_wrapper.* wrappers all rdma related functions

rdma_worker.* worker with polling thread

rdma_mr_pool.* memory region related 

rdma_composed_endpoint.* endpoints for async/sync/semi-sync 

```