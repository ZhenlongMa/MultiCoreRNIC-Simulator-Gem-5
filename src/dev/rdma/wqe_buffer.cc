
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
    descBufferCap(descBufferCap)
{
    for (int i = 0; i < descBufferCap; i++)
    {
        vacantAddr.push(i);
    }
}
