/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
 */
#include "include/multicast_subscriber.h"

namespace ock {
namespace hcom {
/**
 * @brief 用于在回调函数里回复订阅者发布的消息
 * @param req       [in]  发送组播消息请求
 * @return 成功返回0，失败返回错误码
 */
SerResult SubscriberContext::Reply(const MultiRequest &req)
{
    NN_ASSERT_LOG_RETURN(mEp != nullptr, SER_INVALID_PARAM);

    UBSHcomNetTransRequest netReq;
    netReq.lAddress = reinterpret_cast<uintptr_t>(req.data);
    netReq.size = req.size;

    // using send raw allows publisher to skip header validation and assignment when receiving messages,
    // total performance better than send inline
    return mEp->PostSendRaw(netReq, mSeqNo);
}

void Subscriber::Close()
{
    if (mEp != nullptr) {
        mEp->Close();
    }
}
}
}
