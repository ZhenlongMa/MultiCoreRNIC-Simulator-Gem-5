
#include "dev/rdma/hangu_rnic.hh"

// #include "debug/DescScheduler.hh"
#include "base/trace.hh"
#include "debug/HanGu.hh"

using namespace HanGuRnicDef;
using namespace Net;
using namespace std;

HanGuRnic::WqeBufferManage::WqeBufferManage(HanGuRnic *rNic, const std::string name):
    rNic(rNic),
    _name(name),
    descBufferCap(descBufferCap),
    wqeReadReqProcessEvent({wqeReadReqProcess();}, name)
{
    for (int i = 1; i < descBufferCap; i++)
    {
        vacantAddr.push(i);
    }
}

void HanGuRnic::WqeBufferManage::wqeReadReqProcess()
{
    assert(rNic->descScheduler.wqeFetchInfoQue.size());
    uint32_t descNum = rNic->descScheduler.wqeFetchInfoQue.front().first;
    QPStatusPtr qpStatus = rNic->descScheduler.wqeFetchInfoQue.front().second;
    assert(wqeBufferMetadataTable[qpStatus->qpn].valid == 1);
    rNic->descScheduler.wqeFetchInfoQue.pop();
    assert(qpStatus->procState == PENDING); // WARNING
    if (descNum > wqeBufferMetadataTable[qpStatus->qpn].descNum)
    {
        int fetchNum;
        int fetchByte;
        int fetchOffset;
        fetchNum = descNum - wqeBufferMetadataTable[qpStatus->qpn].descNum;
        fetchByte = fetchNum * sizeof(TxDesc);
        // In descScheduler.wqePrefetch it is assured that fetchByte does not exceed the border of Work Queue
        fetchOffset = (qpStatus->tail_ptr + wqeBufferMetadataTable[qpStatus->qpn].descNum) * sizeof(TxDesc) % sqSize;
        MrReqRspPtr descReq = make_shared<MrReqRsp>(DMA_TYPE_RREQ, MR_RCHNL_TX_DESC_UPDATE, qpStatus->key, fetchByte, fetchOffset);
        descReq->txDescRsp = new TxDesc[descNum];
        rNic->descFetchFifo.push(descReq);
        if (!rNic->mrRescModule.transReqEvent.scheduled()) /* Schedule MrRescModule.transReqProcessing */
        { 
            rNic->schedule(rNic->mrRescModule.transReqEvent, curTick() + rNic->clockPeriod());
        }
        qpStatus->procState = WAITING_FETCH;
        std::tuple<uint32_t, uint32_t, QPStatusPtr> wqeMissInfoPair(descNum, fetchNum, qpStatus);
        wqeMissInfoQue.push(wqeMissInfoPair);
    }
    else // WQEs in buffer is enough
    {
        std::pair<uint32_t, QPStatusPtr> wqeHitInfoPair(descNum, qpStatus);
        wqeHitInfoQue.push(wqeHitInfoPair);
        int index = wqeBufferMetadataTable[qpStatus->qpn].head;
        for (int i = 0; i < descNum; i++)
        {
            assert(wqeBuffer[index].next != 0);
            TxDescPtr desc = wqeBuffer[index].desc;
            wqeHitQue.push(desc);
            index = wqeBuffer[index].next;
        }
        qpStatus->procState = PROCESSING;
        if (!rNic->descScheduler.wqeProcEvent.scheduled())
        {
            rNic->schedule(wqeProcEvent, curTick() + rNic->clockPeriod());
        }
    }
    if (rNic->descScheduler.wqeFetchInfoQue.size() != 0 && !wqeReadReqProcessEvent.scheduled())
    {
        rNic->schedule(wqeReadReqProcessEvent, cutTick() + rNic->clockPeriod());
    }
}

void HanGuRnic::WqeBufferManage::wqeReadRspProcess()
{
    assert(wqeMissInfoQue.size() != 0);
    uint32_t descNum = std::get<1>(wqeMissInfoQue.front);
    uint32_t fetchNum = std::get<2>(wqeMissInfoQue.front);
    QPStatusPtr qpStatus = std::get<3>(wqeMissInfoQue.front);
    wqeMissInfoQue.pop();
    assert(fetchNum <= rNic->txdescRspFifo.size());
    while (vacantAddr.size() < fetchNum)
    {
        randomClean();
    }
    std::queue<TxDescPtr> descQue;
    for (int i = 0; i < fetchNum; i++)
    {
        TxDescPtr desc = rNic->txdescRspFifo.front();
        rNic->txdescRspFifo.pop();
        descQue.push(desc);
    }
    append(qpStatus->qpn, descQue);
    // to do

}

void hanGuRnic::WqeBufferManage::wqeHitProc()
{

}

void HanGuRnic::WqeBufferManage::randomClean()
{

}

void HanGuRnic::WqeBufferManage::append(uint32_t qpn, std::queue<TxDescPtr>& descQue)
{
    assert(vacantAddr.size() != 0);
    uint16_t prevIdx;
    while(descQue.size() != 0)
    {
        uint16_t newIdx = vacantAddr.pop();
        TxDescPtr desc = descQue.front();
        prevIdx = wqeBufferMetadataTable[qpn]->tail;
        WqeBufferUnitPtr wqeBufferUnit = make_shared<WqeBufferUnit>(desc, prev, 0);
        wqeBuffer[newIdx] = wqeBufferUnit;
        wqeBuffer[prevIdx]->next = newIdx;
    }
}
