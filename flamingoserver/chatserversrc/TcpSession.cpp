/**
 * TcpSession.cpp
 * zhangyl 2017.03.09
 **/
#include "../base/Logging.h"
#include "Msg.h"
#include "../net/ProtocolStream.h"
#include "../zlib1.2.11/ZlibUtil.h"
#include "TcpSession.h"

TcpSession::TcpSession(const std::weak_ptr<TcpConnection>& tmpconn) : tmpConn_(tmpconn)
{
    
}

TcpSession::~TcpSession()
{
    
}

void TcpSession::Send(int32_t cmd, int32_t seq, const std::string& data)
{
    Send(cmd, seq, data.c_str(), data.length());
}

void TcpSession::Send(int32_t cmd, int32_t seq, const char* data, int32_t dataLength)
{
    std::string outbuf;
    net::BinaryWriteStream writeStream(&outbuf);
    writeStream.WriteInt32(cmd);
    writeStream.WriteInt32(seq);
    writeStream.WriteCString(data, dataLength);
    writeStream.Flush();

    SendPackage(outbuf.c_str(), outbuf.length());
}

void TcpSession::Send(const std::string& p)
{
    SendPackage(p.c_str(), p.length());
}

void TcpSession::Send(const char* p, int32_t length)
{
    SendPackage(p, length);
}

void TcpSession::SendPackage(const char* p, int32_t length)
{   
    string srcbuf(p, length);
    string destbuf;
    if (!ZlibUtil::CompressBuf(srcbuf, destbuf))
    {
        LOG_ERROR << "compress buf error";
        return;
    }
 
    string strPackageData;
    msg header;
    header.compressflag = 1;
    header.compresssize = destbuf.length();
    header.originsize = length;

    //LOG_INFO << "Send data, header length:" << sizeof(header) << ", body length:" << outbuf.length();
    //����һ����ͷ
    strPackageData.append((const char*)&header, sizeof(header));
    strPackageData.append(destbuf);

    //TODO: ��ЩSession��connection�������������Ҫ�ú�����һ��
    if (tmpConn_.expired())
    {
        //FIXME: ��������������Ҫ�Ų�
        LOG_ERROR << "Tcp connection is destroyed , but why TcpSession is still alive ?";
        return;
    }

    std::shared_ptr<TcpConnection> conn = tmpConn_.lock();
    if (conn)
    {
        //size_t length = strPackageData.length();
        //LOG_INFO << "Send data, length:" << length;
        //LOG_DEBUG_BIN((unsigned char*)strSendData.c_str(), length);
        conn->send(strPackageData);
    }
}