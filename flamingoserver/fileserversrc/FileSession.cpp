/**
 * FileSession.cpp
 * zhangyl, 2017.03.17
 **/
#include "FileSession.h"
#include <string.h>
#include <sstream>
#include <list>
#include "../net/TcpConnection.h"
#include "../net/ProtocolStream.h"
#include "../base/Logging.h"
#include "../base/Singleton.h"
#include "FileMsg.h"
#include "FileManager.h"

using namespace net;

//�ļ����������İ�50M
#define MAX_PACKAGE_SIZE    50 * 1024 * 1024

FileSession::FileSession(const std::shared_ptr<TcpConnection>& conn, const char* filebasedir) :
TcpSession(conn), 
m_id(0),
m_seq(0),
m_strFileBaseDir(filebasedir),
m_bFileUploading(false)
{
}

FileSession::~FileSession()
{

}

void FileSession::OnRead(const std::shared_ptr<TcpConnection>& conn, Buffer* pBuffer, Timestamp receivTime)
{
    while (true)
    {
        //����һ����ͷ��С
        if (pBuffer->readableBytes() < (size_t)sizeof(file_msg))
        {
            //LOG_INFO << "buffer is not enough for a package header, pBuffer->readableBytes()=" << pBuffer->readableBytes() << ", sizeof(msg)=" << sizeof(file_msg);
            return;
        }

        //����һ��������С
        file_msg header;
        memcpy(&header, pBuffer->peek(), sizeof(file_msg));

        //��ͷ�д��������ر�����
        if (header.packagesize <= 0 || header.packagesize > MAX_PACKAGE_SIZE)
        {
            //�ͻ��˷��Ƿ����ݰ��������������ر�֮
            LOG_ERROR << "Illegal package heade size, close TcpConnection, client: " << conn->peerAddress().toIpPort();
            conn->forceClose();
        }

        if (pBuffer->readableBytes() < (size_t)header.packagesize + sizeof(file_msg))
            return;

        pBuffer->retrieve(sizeof(file_msg));
        std::string inbuf;
        inbuf.append(pBuffer->peek(), header.packagesize);
        pBuffer->retrieve(header.packagesize);
        if (!Process(conn, inbuf.c_str(), inbuf.length()))
        {
            LOG_ERROR << "Process error, close TcpConnection, client: " << conn->peerAddress().toIpPort();
            conn->forceClose();
        }
    }// end while-loop

}

bool FileSession::Process(const std::shared_ptr<TcpConnection>& conn, const char* inbuf, size_t length)
{
    BinaryReadStream readStream(inbuf, length);
    int32_t cmd;
    if (!readStream.ReadInt32(cmd))
    {
        LOG_ERROR << "read cmd error, client: " << conn->peerAddress().toIpPort();
        return false;
    }

    //int seq;
    if (!readStream.ReadInt32(m_seq))
    {
        LOG_ERROR << "read seq error, client: " << conn->peerAddress().toIpPort();
        return false;
    }

    std::string filemd5;
    size_t md5length;
    if (!readStream.ReadString(&filemd5, 0, md5length) || md5length == 0)
    {
        LOG_ERROR << "read filemd5 error, client: " << conn->peerAddress().toIpPort();
        return false;
    }

    int64_t offset;
    if (!readStream.ReadInt64(offset))
    {
        LOG_ERROR << "read offset error, client: " << conn->peerAddress().toIpPort();
        return false;
    }

    int64_t filesize;
    if (!readStream.ReadInt64(filesize))
    {
        LOG_ERROR << "read filesize error, client: " << conn->peerAddress().toIpPort();
        return false;
    }

    string filedata;
    size_t filedatalength;
    if (!readStream.ReadString(&filedata, 0, filedatalength))
    {
        LOG_ERROR << "read filedata error, client: " << conn->peerAddress().toIpPort();
        return false;
    }
   
    LOG_INFO << "Request from client: cmd=" << cmd 
             << ", seq=" << m_seq              
             << ", filemd5=" << filemd5 
             << ", md5length=" << md5length
             << ", offset=" << offset
             << ", filesize=" << filesize
             << ", filedata length=" << filedata.length()
             << ", header.packagesize:" << length
             << ", client: " << conn->peerAddress().toIpPort();
    //LOG_DEBUG_BIN((unsigned char*)inbuf, length);

    switch (cmd)
    {
        //�ͻ����ļ��ϴ�
        case msg_type_upload_req:
            return OnUploadFileResponse(filemd5, offset, filesize, filedata, conn);

        //�ͻ����ļ�����
        case msg_type_download_req:         
            //�������أ��ͻ��˲�֪���ļ���С�� ����ֵ��0
            //if (filedatalength != 0)
            //    return false;
            return OnDownloadFileResponse(filemd5, conn);

        default:
            //pBuffer->retrieveAll();
            LOG_ERROR << "unsupport cmd, cmd:" << cmd << ", client:" << conn->peerAddress().toIpPort();
            //conn->forceClose();
            return false;
    }// end switch

    ++ m_seq;

    return true;
}

bool FileSession::OnUploadFileResponse(const std::string& filemd5, int64_t offset, int64_t filesize, const std::string& filedata, const std::shared_ptr<TcpConnection>& conn)
{
    if (filemd5.empty())
    {
        LOG_ERROR << "Empty filemd5, client:" << conn->peerAddress().toIpPort();
        return false;
    }
     
    //���������Ѿ����ڸ��ļ���ֱ�ӷ���(������ļ��Ǵ��ڴ�״̬˵�����������ϴ���״̬)
    if (Singleton<FileManager>::Instance().IsFileExsit(filemd5.c_str()) && !m_bFileUploading)
    {
        offset = filesize;      
        string dummyfiledata;      
        Send(msg_type_upload_resp, m_seq, file_msg_error_complete, filemd5, offset, filesize, dummyfiledata);
        
        
        LOG_INFO << "Response to client: cmd=msg_type_upload_resp, errorcode: file_msg_error_complete"
                 << ", filemd5: " << filemd5
                 << ", offset: " << offset
                 << ", filesize: " << filesize
                 << ", client:" << conn->peerAddress().toIpPort();

        return true;
    }
    
    if (offset == 0)
    {
        string filename = m_strFileBaseDir;
        filename += filemd5;
        m_fp = fopen(filename.c_str(), "w");
        if (m_fp == NULL)
        {
            LOG_ERROR << "fopen file error, filemd5=" << filemd5 << ", client:" << conn->peerAddress().toIpPort();
            return false;
        }

        //��ʶ���ļ������ϴ���
        m_bFileUploading = true;
    }
    else
    {
        if (m_fp == NULL)
        {
            ResetFile();
            LOG_ERROR << "file pointer should not be null, filemd5=" << filemd5 << ", offset=" << offset << ", client:" << conn->peerAddress().toIpPort();
            return false;
        }
    }

    if (fseek(m_fp, offset, SEEK_SET) == -1)
    {
        LOG_ERROR << "fseek error, filemd5: " << filemd5
                << ", errno: " << errno << ", errinfo: " << strerror(errno)
                << ", filedata.length(): " << filedata.length()
                << ", m_fp: " << m_fp
                << ", buffer size is 512*1024"
                << ", client:" << conn->peerAddress().toIpPort();

        ResetFile();
        return false;
    }

    if (fwrite(filedata.c_str(), filedata.length(), 1, m_fp) != 1)
    {
        ResetFile();
        LOG_ERROR << "fwrite error, filemd5: " << filemd5
					<< ", errno: " << errno << ", errinfo: " << strerror(errno)
					<< ", filedata.length(): " << filedata.length()
					<< ", m_fp: " << m_fp
					<< ", buffer size is 512*1024"
					<< ", client:" << conn->peerAddress().toIpPort();
        return false;
    }

    int32_t errorcode = file_msg_error_progress;

    //�ļ��ϴ��ɹ�
    if (offset + (int64_t)filedata.length() == filesize)
    {
        offset = filesize;
        errorcode = file_msg_error_complete;
        Singleton<FileManager>::Instance().addFile(filemd5.c_str());
        ResetFile();
    }

    string dummyfiledatax;
    Send(msg_type_upload_resp, m_seq, errorcode, filemd5, offset, filesize, dummyfiledatax);
       
    LOG_INFO << "Response to client: cmd=msg_type_upload_resp, errorcode: " 
             << errorcode << ", filemd5: " << filemd5
             << ", offset: " << offset
             << ", filesize: " << filesize
             << ", upload percent: " << (offset * 100 / filesize) << "%"
             << ", client:" << conn->peerAddress().toIpPort();

    return true;
}

bool FileSession::OnDownloadFileResponse(const std::string& filemd5, const std::shared_ptr<TcpConnection>& conn)
{
    if (filemd5.empty())
    {
        LOG_ERROR << "Empty filemd5, client:" << conn->peerAddress().toIpPort();
        return false;
    }
    
    if (!Singleton<FileManager>::Instance().IsFileExsit(filemd5.c_str()))
    {
        //�ͻ������ز����ڵ��ļ������߿ͻ��˲����ڸ��ļ�
        string dummyfiledata;
        //�ļ�������,������Ӧ����ƫ����offset���ļ���Сfilesize������Ϊ0
        int64_t notExsitFileOffset = 0;
        int64_t notExsitFileSize = 0;
        Send(msg_type_download_resp, m_seq, file_msg_error_not_exist, filemd5, notExsitFileOffset, notExsitFileSize, dummyfiledata);
        LOG_ERROR << "filemd5 not exsit, filemd5: " << filemd5 << ", client:" << conn->peerAddress().toIpPort();
        LOG_INFO << "Response to client: cmd=msg_type_download_resp, errorcode=file_msg_error_not_exist "
                 << ", filemd5: " << filemd5
                 << ", offset: 0"
                 << ", filesize: 0"
                 << ", filedataLength: 0"
                 << ", client:" << conn->peerAddress().toIpPort();
        return true;
    }

    //�ļ���δ��,���ȴ�
    if (m_fp == NULL)
    {
        string filename = m_strFileBaseDir;
        filename += filemd5;
        m_fp = fopen(filename.c_str(), "r+");
        if (m_fp == NULL)
        {
            LOG_ERROR << "fopen file error, filemd5: " << filemd5 << ", client:" << conn->peerAddress().toIpPort();
            return false;
        }
        fseek(m_fp, 0, SEEK_END);
        m_currentDownloadFileSize = ftell(m_fp);
        if (m_currentDownloadFileSize <= 0)
		{
            LOG_ERROR << "m_filesize: " << m_currentDownloadFileSize << ", errno: " << errno << ", filemd5: " << filemd5 << ", client : " << conn->peerAddress().toIpPort();
			return false;
		}
        fseek(m_fp, 0, SEEK_SET);
    }

    string filedata;
    
    //m_offset += offset;
    int64_t currentSendSize = 512 * 1024;
    char buffer[512 * 1024] = { 0 };
    if (m_currentDownloadFileSize <= m_currentDownloadFileOffset + currentSendSize)
    {
        currentSendSize = m_currentDownloadFileSize - m_currentDownloadFileOffset;
    }

    //TODO: ���fread()����ʱ���ܻ���ֱ���,���Ų�
	if (currentSendSize <= 0 || fread(buffer, currentSendSize, 1, m_fp) != 1)
	{
		LOG_ERROR << "fread error, filemd5: " << filemd5
					<< ", errno: " << errno << ", errinfo: " << strerror(errno)
					<< ", currentSendSize: " << currentSendSize
					<< ", m_fp: " << m_fp
					<< ", buffer size is 512*1024"
					<< ", connection name:" << conn->peerAddress().toIpPort();
	}

    //��Ҫ���͵�ƫ����
    int sendoffset = m_currentDownloadFileOffset;
    m_currentDownloadFileOffset += currentSendSize;
    filedata.append(buffer, currentSendSize);      

    int errorcode = file_msg_error_progress;
    //�ļ��Ѿ��������
    if (m_currentDownloadFileOffset == m_currentDownloadFileSize)
        errorcode = file_msg_error_complete;

    Send(msg_type_download_resp, m_seq, errorcode, filemd5, sendoffset, m_currentDownloadFileSize, filedata);

    LOG_INFO << "Response to client: cmd=msg_type_download_resp, errorcode: " << errorcode
             << ", filemd5: " << filemd5
             << ", offset: " << sendoffset
             << ", filesize: " << m_currentDownloadFileSize
             << ", filedataLength: " << filedata.length()
             << ", download percent: " << (sendoffset * 100 / m_currentDownloadFileSize) << "%"
             << ", client:" << conn->peerAddress().toIpPort();

    //�ļ����سɹ�,�����ļ�״̬
    if (errorcode == file_msg_error_complete)
        ResetFile();

     return true;
}

void FileSession::ResetFile()
{
    if (m_fp != NULL)
    {
        fclose(m_fp);
        m_currentDownloadFileOffset = 0;
        m_currentDownloadFileSize = 0;
		m_fp = NULL;
        m_bFileUploading = false;
    }
}