#include "Channel.h"
#include <sstream>
#include <assert.h>
#include <poll.h>
#include "../base/Logging.h"
#include "EventLoop.h"

using namespace net;

const int Channel::kNoneEvent = 0;
const int Channel::kReadEvent = POLLIN | POLLPRI;
const int Channel::kWriteEvent = POLLOUT;

Channel::Channel(EventLoop* loop, int fd__)
: loop_(loop),
fd_(fd__),
events_(0),
revents_(0),
index_(-1),
logHup_(true),
tied_(false)/*,
eventHandling_(false),
addedToLoop_(false)
*/
{
}

Channel::~Channel()
{
	//assert(!eventHandling_);
	//assert(!addedToLoop_);
	if (loop_->isInLoopThread())
	{
		//assert(!loop_->hasChannel(this));
	}
}

void Channel::tie(const std::shared_ptr<void>& obj)
{
	tie_ = obj;
	tied_ = true;
}

bool Channel::enableReading() 
{ 
    events_ |= kReadEvent;
    return update();
}

bool Channel::disableReading()
{
    events_ &= ~kReadEvent; 
    
    return update();
}

bool Channel::enableWriting() 
{
    events_ |= kWriteEvent; 
    
    return update(); 
}

bool Channel::disableWriting()
{ 
    events_ &= ~kWriteEvent; 
    return update();
}

bool Channel::disableAll()
{ 
    events_ = kNoneEvent; 
    return update(); 
}

bool Channel::update()
{
	//addedToLoop_ = true;
	return loop_->updateChannel(this);
}

void Channel::remove()
{
	assert(isNoneEvent());
	//addedToLoop_ = false;
	loop_->removeChannel(this);
}

void Channel::handleEvent(Timestamp receiveTime)
{
	std::shared_ptr<void> guard;
	if (tied_)
	{
		guard = tie_.lock();
		if (guard)
		{
			handleEventWithGuard(receiveTime);
		}
	}
	else
	{
		handleEventWithGuard(receiveTime);
	}
}

void Channel::handleEventWithGuard(Timestamp receiveTime)
{
	//eventHandling_ = true;
    /*
    POLLIN �����¼�
    POLLPRI�����¼�������ʾ�������ݣ�����tcp socket�Ĵ�������
    POLLRDNORM , ���¼�����ʾ����ͨ���ݿɶ�������
    POLLRDBAND ,�����¼�����ʾ���������ݿɶ���������
    POLLOUT��д�¼�
    POLLWRNORM , д�¼�����ʾ����ͨ���ݿ�д
    POLLWRBAND ,��д�¼�����ʾ���������ݿ�д������   ��������
    POLLRDHUP (since Linux 2.6.17)��Stream socket��һ�˹ر������ӣ�ע����stream socket������֪������raw socket,dgram socket����������д�˹ر������ӣ����Ҫʹ������¼������붨��_GNU_SOURCE �ꡣ����¼����������ж���·�Ƿ����쳣����Ȼ��ͨ�õķ�����ʹ���������ƣ���Ҫʹ������¼�������������ͷ�ļ���
    ����#define _GNU_SOURCE
      ����#include <poll.h>
    POLLERR���������ں����ô�������revents����ʾ�豸��������
    POLLHUP���������ں����ô�������revents����ʾ�豸���������poll������fd��socket����ʾ���socket��û���������Ͻ������ӣ�����˵ֻ������socket()����������û�н���connect��
    POLLNVAL���������ں����ô�������revents����ʾ�Ƿ������ļ�������fdû�д�
    */
	LOG_TRACE << reventsToString();
	if ((revents_ & POLLHUP) && !(revents_ & POLLIN))
	{
		if (logHup_)
		{
			LOG_WARN << "Channel::handle_event() POLLHUP";
		}
		if (closeCallback_) closeCallback_();
	}

	if (revents_ & POLLNVAL)
	{
		LOG_WARN << "Channel::handle_event() POLLNVAL";
	}

	if (revents_ & (POLLERR | POLLNVAL))
	{
		if (errorCallback_) errorCallback_();
	}
    
	if (revents_ & (POLLIN | POLLPRI | POLLRDHUP))
	{
		//��������socketʱ��readCallback_ָ��Acceptor::handleRead
        //���ǿͻ���socketʱ������TcpConnection::handleRead 
        if (readCallback_) readCallback_(receiveTime);
	}

	if (revents_ & POLLOUT)
	{
		//���������״̬����socket����writeCallback_ָ��Connector::handleWrite()
        if (writeCallback_) writeCallback_();
	}
	//eventHandling_ = false;
}

string Channel::reventsToString() const
{
	std::ostringstream oss;
	oss << fd_ << ": ";
	if (revents_ & POLLIN)
		oss << "IN ";
	if (revents_ & POLLPRI)
		oss << "PRI ";
	if (revents_ & POLLOUT)
		oss << "OUT ";
	if (revents_ & POLLHUP)
		oss << "HUP ";
	if (revents_ & POLLRDHUP)
		oss << "RDHUP ";
	if (revents_ & POLLERR)
		oss << "ERR ";
	if (revents_ & POLLNVAL)
		oss << "NVAL ";

	return oss.str().c_str();
}
