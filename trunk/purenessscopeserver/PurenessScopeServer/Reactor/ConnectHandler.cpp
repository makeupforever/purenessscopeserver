#include "ConnectHandler.h"

Mutex_Allocator _msg_send_mb_allocator;

CConnectHandler::CConnectHandler(void)
{
	m_szError[0]          = '\0';
	m_u4ConnectID         = 0;
	m_u2SendCount         = 0;
	m_u4AllRecvCount      = 0;
	m_u4AllSendCount      = 0;
	m_u4AllRecvSize       = 0;
	m_u4AllSendSize       = 0;
	m_nIOCount            = 1;
	m_u4SendThresHold     = MAX_MSG_SNEDTHRESHOLD;
	m_u2SendQueueMax      = MAX_MSG_SENDPACKET;
	m_u1ConnectState      = CONNECT_INIT;
	m_u1SendBuffState     = CONNECT_SENDNON;
	m_pTCClose            = NULL;
	m_pCurrMessage        = NULL;
	m_pBlockMessage       = NULL;
	m_pPacketParse        = NULL;
	m_u4CurrSize          = 0;
	m_u4HandlerID         = 0;
	m_u2MaxConnectTime    = 0;
	m_u4MaxPacketSize     = MAX_MSG_PACKETLENGTH;
	m_blBlockState        = false;
	m_nBlockCount         = 0;
	m_nBlockSize          = MAX_BLOCK_SIZE;
	m_nBlockMaxCount      = MAX_BLOCK_COUNT;
	m_u4BlockTimerID      = 0;
	m_u8RecvQueueTimeCost = 0;
	m_u4RecvQueueCount    = 0;
	m_u8SendQueueTimeCost = 0;
	m_u4ReadSendSize      = 0;
	m_u4SuccessSendSize   = 0;
	m_u8SendQueueTimeout  = MAX_QUEUE_TIMEOUT * 1000 * 1000;  //Ŀǰ��Ϊ��¼��������
	m_u8RecvQueueTimeout  = MAX_QUEUE_TIMEOUT * 1000 * 1000;  //Ŀǰ��Ϊ��¼��������
	m_u2TcpNodelay        = TCP_NODELAY_ON;
}

CConnectHandler::~CConnectHandler(void)
{
	//OUR_DEBUG((LM_INFO, "[CConnectHandler::~CConnectHandler].\n"));
	SAFE_DELETE(m_pTCClose);
	if(NULL != m_pBlockMessage)
	{
		m_pBlockMessage->release();
		m_pBlockMessage = NULL;
	}
	//OUR_DEBUG((LM_INFO, "[CConnectHandler::~CConnectHandler]End.\n"));
}

const char* CConnectHandler::GetError()
{
	return m_szError;
}

bool CConnectHandler::Close(int nIOCount)
{
	m_ThreadLock.acquire();
	if(nIOCount > m_nIOCount)
	{
		m_nIOCount = 0;
	}

	if(m_nIOCount > 0)
	{
		m_nIOCount -= nIOCount;
	}
	m_ThreadLock.release();

	//OUR_DEBUG((LM_ERROR, "[CConnectHandler::Close]ConnectID=%d,m_nIOCount=%d.\n", GetConnectID(), m_nIOCount));

	//�ӷ�Ӧ��ע���¼�
	if(m_nIOCount == 0)
	{
		//�鿴�Ƿ���IP׷����Ϣ�������¼
		App_IPAccount::instance()->CloseIP((string)m_addrRemote.get_host_addr(), m_addrRemote.get_port_number(), m_u4AllRecvSize, m_u4AllSendSize);

		//OUR_DEBUG((LM_ERROR, "[CConnectHandler::Close]ConnectID=%d,m_nIOCount=%d.\n", GetConnectID(), m_nIOCount));

		//���������������ʱ����ȡ��֮
		if(m_u4BlockTimerID != 0)
		{
			App_TimerManager::instance()->cancel(m_u4BlockTimerID);
			m_u4BlockTimerID = 0;
		}

		//ɾ�����󻺳��PacketParse
		if(m_pCurrMessage != NULL)
		{
			m_pCurrMessage->release();
		}

		//�������ӶϿ���Ϣ
		CPacketParse objPacketParse;
		objPacketParse.DisConnect(GetConnectID());

		if(m_u1ConnectState != CONNECT_SERVER_CLOSE)
		{
			//���Ϳͻ������ӶϿ���Ϣ��
			if(false == App_MakePacket::instance()->PutMessageBlock(GetConnectID(), PACKET_CDISCONNECT, NULL))
			{
				OUR_DEBUG((LM_ERROR, "[CProConnectHandle::open] ConnectID = %d, PACKET_CONNECT is error.\n", GetConnectID()));
			}
		}

		//msg_queue()->deactivate();
		shutdown();
		OUR_DEBUG((LM_ERROR, "[CConnectHandler::Close]Close(%d),m_u4RecvQueueCount=%d,m_u8SendQueueTimeCost=%Q OK.\n", GetConnectID(), m_u4RecvQueueCount, m_u8SendQueueTimeCost));
		AppLogManager::instance()->WriteLog(LOG_SYSTEM_CONNECT, "Close Connection from [%s:%d] RecvSize = %d, RecvCount = %d, SendSize = %d, SendCount = %d, m_u8RecvQueueTimeCost = %dws, m_u4RecvQueueCount = %d, m_u8SendQueueTimeCost = %dws.",m_addrRemote.get_host_addr(), m_addrRemote.get_port_number(), m_u4AllRecvSize, m_u4AllRecvCount, m_u4AllSendSize, m_u4AllSendCount, (uint32)m_u8RecvQueueTimeCost, m_u4RecvQueueCount, (uint32)m_u8SendQueueTimeCost);

		//ɾ�����Ӷ���
		App_ConnectManager::instance()->CloseConnect(GetConnectID());

		//�ع��ù���ָ��
		App_ConnectHandlerPool::instance()->Delete(this);
		return true;
	}

	return false;
}

void CConnectHandler::Init(uint16 u2HandlerID)
{
	m_u4HandlerID      = u2HandlerID;
	m_u2MaxConnectTime = App_MainConfig::instance()->GetMaxConnectTime();
	m_u4SendThresHold  = App_MainConfig::instance()->GetSendTimeout();
	m_u2SendQueueMax   = App_MainConfig::instance()->GetSendQueueMax();
	m_u4MaxPacketSize  = App_MainConfig::instance()->GetRecvBuffSize();
	m_u2TcpNodelay     = App_MainConfig::instance()->GetTcpNodelay();

	m_u8SendQueueTimeout = App_MainConfig::instance()->GetSendQueueTimeout() * 1000 * 1000;
	if(m_u8SendQueueTimeout == 0)
	{
		m_u8SendQueueTimeout = MAX_QUEUE_TIMEOUT * 1000 * 1000;
	}

	m_u8RecvQueueTimeout = App_MainConfig::instance()->GetRecvQueueTimeout() * 1000 * 1000;
	if(m_u8RecvQueueTimeout <= 0)
	{
		m_u8RecvQueueTimeout = MAX_QUEUE_TIMEOUT * 1000 * 1000;
	}

	//m_pBlockMessage    = App_MessageBlockManager::instance()->Create(m_u4MaxPacketSize);
	m_pBlockMessage      = new ACE_Message_Block(m_u4MaxPacketSize);
}

bool CConnectHandler::ServerClose()
{
	OUR_DEBUG((LM_ERROR, "[CConnectHandler::ServerClose]Close(%d) OK.\n", GetConnectID()));
	//AppLogManager::instance()->WriteLog(LOG_SYSTEM_CONNECT, "Close Connection from [%s:%d] RecvSize = %d, RecvCount = %d, SendSize = %d, SendCount = %d, m_u8RecvQueueTimeCost = %d, m_u4RecvQueueCount = %d, m_u8SendQueueTimeCost = %d.",m_addrRemote.get_host_addr(), m_addrRemote.get_port_number(), m_u4AllRecvSize, m_u4AllRecvCount, m_u4AllSendSize, m_u4AllSendCount, m_u8RecvQueueTimeCost, m_u4RecvQueueCount, m_u8SendQueueTimeCost);

	//���Ϳͻ������ӶϿ���Ϣ��
	if(false == App_MakePacket::instance()->PutMessageBlock(GetConnectID(), PACKET_SDISCONNECT, NULL))
	{
		OUR_DEBUG((LM_ERROR, "[CProConnectHandle::open] ConnectID = %d, PACKET_CONNECT is error.\n", GetConnectID()));
	}

	//msg_queue()->deactivate();
	shutdown();

	//�������ӶϿ���Ϣ
	CPacketParse objPacketParse;
	objPacketParse.DisConnect(GetConnectID());

	m_u1ConnectState = CONNECT_SERVER_CLOSE;

	return true;
}

void CConnectHandler::SetConnectID(uint32 u4ConnectID)
{
	m_u4ConnectID = u4ConnectID;
}

uint32 CConnectHandler::GetConnectID()
{
	return m_u4ConnectID;
}

int CConnectHandler::open(void*)
{
	m_nIOCount            = 1;
	m_blBlockState        = false;
	m_nBlockCount         = 0;
	m_u8SendQueueTimeCost = 0;
	m_blIsLog             = false;
	m_szConnectName[0]    = '\0';

	//���û�����
	m_pBlockMessage->reset();

	//���Զ�����ӵ�ַ�Ͷ˿�
	if(this->peer().get_remote_addr(m_addrRemote) == -1)
	{
		OUR_DEBUG((LM_ERROR, "[CConnectHandler::open]this->peer().get_remote_addr error.\n"));
		sprintf_safe(m_szError, MAX_BUFF_500, "[CConnectHandler::open]this->peer().get_remote_addr error.");
		return -1;
	}

	if(App_ForbiddenIP::instance()->CheckIP(m_addrRemote.get_host_addr()) == false)
	{
		//�ڽ�ֹ�б��У�����������
		OUR_DEBUG((LM_ERROR, "[CConnectHandler::open]IP Forbidden(%s).\n", m_addrRemote.get_host_addr()));
		return -1;
	}

	//��鵥λʱ�����Ӵ����Ƿ�ﵽ����
	if(false == App_IPAccount::instance()->AddIP((string)m_addrRemote.get_host_addr(), m_addrRemote.get_port_number()))
	{
		OUR_DEBUG((LM_ERROR, "[CConnectHandler::open]IP connect frequently.\n", m_addrRemote.get_host_addr()));
		App_ForbiddenIP::instance()->AddTempIP(m_addrRemote.get_host_addr(), App_MainConfig::instance()->GetIPAlert()->m_u4IPTimeout);

		//���͸澯�ʼ�
		AppLogManager::instance()->WriteToMail(LOG_SYSTEM_CONNECT, 
			App_MainConfig::instance()->GetIPAlert()->m_u4MailID,
			(char* )"Alert IP",
			"[CConnectHandler::open] IP is more than IP Max,");

		return -1;
	}

	//��ʼ�������
	m_TimeConnectInfo.Init(App_MainConfig::instance()->GetClientDataAlert()->m_u4RecvPacketCount, 
		App_MainConfig::instance()->GetClientDataAlert()->m_u4RecvDataMax, 
		App_MainConfig::instance()->GetClientDataAlert()->m_u4SendPacketCount,
		App_MainConfig::instance()->GetClientDataAlert()->m_u4SendDataMax);

	int nRet = ACE_Svc_Handler<ACE_SOCK_STREAM, ACE_MT_SYNCH>::open();
	if(nRet != 0)
	{
		OUR_DEBUG((LM_ERROR, "[CConnectHandler::open]ACE_Svc_Handler<ACE_SOCK_STREAM, ACE_MT_SYNCH>::open() error [%d].\n", nRet));
		sprintf_safe(m_szError, MAX_BUFF_500, "[CConnectHandler::open]ACE_Svc_Handler<ACE_SOCK_STREAM, ACE_MT_SYNCH>::open() error [%d].", nRet);
		return -1;
	}

	//��������Ϊ������ģʽ
	if (this->peer().enable(ACE_NONBLOCK) == -1)
	{
		OUR_DEBUG((LM_ERROR, "[CConnectHandler::open]this->peer().enable  = ACE_NONBLOCK error.\n"));
		sprintf_safe(m_szError, MAX_BUFF_500, "[CConnectHandler::open]this->peer().enable  = ACE_NONBLOCK error.");
		return -1;
	}

	//����Ĭ�ϱ���
	SetConnectName(m_addrRemote.get_host_addr());
	OUR_DEBUG((LM_INFO, "[CConnectHandler::open] Connection from [%s:%d]\n",m_addrRemote.get_host_addr(), m_addrRemote.get_port_number()));

	//��ʼ����ǰ���ӵ�ĳЩ����
	m_atvConnect          = ACE_OS::gettimeofday();
	m_atvInput            = ACE_OS::gettimeofday();
	m_atvOutput           = ACE_OS::gettimeofday();
	m_atvSendAlive        = ACE_OS::gettimeofday();

	m_u4AllRecvCount      = 0;
	m_u4AllSendCount      = 0;
	m_u4AllRecvSize       = 0;
	m_u4AllSendSize       = 0;
	m_u8RecvQueueTimeCost = 0;
	m_u4RecvQueueCount    = 0;
	m_u8SendQueueTimeCost = 0;

	m_u4ReadSendSize      = 0;
	m_u4SuccessSendSize   = 0;

	//���ý��ջ���صĴ�С
	int nTecvBuffSize = MAX_MSG_SOCKETBUFF;
	//ACE_OS::setsockopt(this->get_handle(), SOL_SOCKET, SO_RCVBUF, (char* )&nTecvBuffSize, sizeof(nTecvBuffSize));
	ACE_OS::setsockopt(this->get_handle(), SOL_SOCKET, SO_SNDBUF, (char* )&nTecvBuffSize, sizeof(nTecvBuffSize));

	if(m_u2TcpNodelay == TCP_NODELAY_OFF)
	{
		//��������˽���Nagle�㷨��������Ҫ���á�
		int nOpt=1; 
		ACE_OS::setsockopt(this->get_handle(), IPPROTO_TCP, TCP_NODELAY, (char* )&nOpt, sizeof(int)); 
	}

	//int nOverTime = MAX_MSG_SENDTIMEOUT;
	//ACE_OS::setsockopt(this->get_handle(), SOL_SOCKET, SO_SNDTIMEO, (char* )&nOverTime, sizeof(nOverTime));

	m_pPacketParse = App_PacketParsePool::instance()->Create();
	if(NULL == m_pPacketParse)
	{
		OUR_DEBUG((LM_DEBUG,"[%t|CConnectHandle::open] Open(%d) m_pPacketParse new error.\n", GetConnectID()));
		return -1;
	}

	//����PacketParse����Ӧ����
	_ClientIPInfo objClientIPInfo;
	sprintf_safe(objClientIPInfo.m_szClientIP, MAX_BUFF_20, "%s", m_addrRemote.get_host_addr());
	objClientIPInfo.m_nPort = m_addrRemote.get_port_number();
	m_pPacketParse->Connect(GetConnectID(), objClientIPInfo);

	//����ͷ�Ĵ�С��Ӧ��mb
	if(m_pPacketParse->GetPacketMode() == PACKET_WITHHEAD)
	{
		m_pCurrMessage = App_MessageBlockManager::instance()->Create(m_pPacketParse->GetPacketHeadLen());
	}
	else
	{
		m_pCurrMessage = App_MessageBlockManager::instance()->Create(App_MainConfig::instance()->GetServerRecvBuff());
	}

	if(m_pCurrMessage == NULL)
	{
		//AppLogManager::instance()->WriteLog(LOG_SYSTEM_CONNECT, "Close Connection from [%s:%d] RecvSize = %d, RecvCount = %d, SendSize = %d, SendCount = %d, m_u8RecvQueueTimeCost = %d, m_u4RecvQueueCount = %d, m_u8SendQueueTimeCost = %d.",m_addrRemote.get_host_addr(), m_addrRemote.get_port_number(), m_u4AllRecvSize, m_u4AllRecvCount, m_u4AllSendSize, m_u4AllSendCount, m_u8RecvQueueTimeCost, m_u4RecvQueueCount, m_u8SendQueueTimeCost);
		OUR_DEBUG((LM_ERROR, "[CConnectHandle::RecvClinetPacket] pmb new is NULL.\n"));

		App_ConnectManager::instance()->Close(GetConnectID());
		return -1;
	}

	//��������ӷ������ӿ�
	if(false == App_ConnectManager::instance()->AddConnect(this))
	{
		OUR_DEBUG((LM_ERROR, "%s.\n", App_ConnectManager::instance()->GetError()));
		sprintf_safe(m_szError, MAX_BUFF_500, "%s", App_ConnectManager::instance()->GetError());
		return -1;
	}
	else
	{
		OUR_DEBUG((LM_DEBUG,"[CConnectHandle::open] Open ConnectID[%d].\n", GetConnectID()));	
	}

	AppLogManager::instance()->WriteLog(LOG_SYSTEM_CONNECT, "Connection from [%s:%d].",m_addrRemote.get_host_addr(), m_addrRemote.get_port_number());

	//�������ӽ�����Ϣ��
	if(false == App_MakePacket::instance()->PutMessageBlock(GetConnectID(), PACKET_CONNECT, NULL))
	{
		OUR_DEBUG((LM_ERROR, "[CProConnectHandle::open] ConnectID = %d, PACKET_CONNECT is error.\n", GetConnectID()));
	}

	m_u1ConnectState = CONNECT_OPEN;

	nRet = this->reactor()->register_handler(this, ACE_Event_Handler::READ_MASK);

	return nRet;
}

//��������
int CConnectHandler::handle_input(ACE_HANDLE fd)
{
	//OUR_DEBUG((LM_ERROR, "[CConnectHandler::handle_input]ConnectID=%d,m_nIOCount=%d.\n", GetConnectID(), m_nIOCount));

	m_atvInput = ACE_OS::gettimeofday();

	if(fd == ACE_INVALID_HANDLE)
	{
		m_u4CurrSize = 0;
		OUR_DEBUG((LM_ERROR, "[CConnectHandler::handle_input]fd == ACE_INVALID_HANDLE.\n"));
		sprintf_safe(m_szError, MAX_BUFF_500, "[CConnectHandler::handle_input]fd == ACE_INVALID_HANDLE.");

		//���Ϳͻ������ӶϿ���Ϣ��
		if(false == App_MakePacket::instance()->PutMessageBlock(GetConnectID(), PACKET_CDISCONNECT, NULL))
		{
			OUR_DEBUG((LM_ERROR, "[CProConnectHandle::open] ConnectID = %d, PACKET_CONNECT is error.\n", GetConnectID()));
		}

		return -1;
	}

	//�ж����ݰ��ṹ�Ƿ�ΪNULL
	if(m_pPacketParse == NULL)
	{
		m_u4CurrSize = 0;
		OUR_DEBUG((LM_ERROR, "[CConnectHandler::handle_input]m_pPacketParse == NULL.\n"));
		sprintf_safe(m_szError, MAX_BUFF_500, "[CConnectHandler::handle_input]m_pPacketParse == NULL.");

		//���Ϳͻ������ӶϿ���Ϣ��
		if(false == App_MakePacket::instance()->PutMessageBlock(GetConnectID(), PACKET_CDISCONNECT, NULL))
		{
			OUR_DEBUG((LM_ERROR, "[CProConnectHandle::open] ConnectID = %d, PACKET_CONNECT is error.\n", GetConnectID()));
		}

		return -1;
	}

	//��л���ʯ�Ľ���
	//���￼�Ǵ����etģʽ��֧��
	if(App_MainConfig::instance()->GetNetworkMode() != (uint8)NETWORKMODE_RE_EPOLL_ET)
	{
		return RecvData();
	}
	else
	{
		return RecvData_et();
	}
}

//����������ݴ���
int CConnectHandler::RecvData()
{
	m_ThreadLock.acquire();
	m_nIOCount++;
	m_ThreadLock.release();	

	ACE_Time_Value nowait(0, MAX_QUEUE_TIMEOUT);

	//�жϻ����Ƿ�ΪNULL
	if(m_pCurrMessage == NULL)
	{
		m_u4CurrSize = 0;
		OUR_DEBUG((LM_ERROR, "[CConnectHandler::handle_input]m_pCurrMessage == NULL.\n"));
		sprintf_safe(m_szError, MAX_BUFF_500, "[CConnectHandler::handle_input]m_pCurrMessage == NULL.");

		//�رյ�ǰ��PacketParse
		ClearPacketParse();

		Close();
		return -1;
	}

	int nCurrCount = (uint32)m_pCurrMessage->size() - m_u4CurrSize;
	//������Ҫ��m_u4CurrSize���м�顣
	if(nCurrCount < 0)
	{
		OUR_DEBUG((LM_ERROR, "[CConnectHandler::handle_input][%d] nCurrCount < 0 m_u4CurrSize = %d.\n", GetConnectID(), m_u4CurrSize));
		m_u4CurrSize = 0;

		//�رյ�ǰ��PacketParse
		ClearPacketParse();

		Close();
		return -1;
	}

	int nDataLen = this->peer().recv(m_pCurrMessage->wr_ptr(), nCurrCount, MSG_NOSIGNAL, &nowait);
	if(nDataLen <= 0)
	{
		m_u4CurrSize = 0;
		uint32 u4Error = (uint32)errno;
		OUR_DEBUG((LM_ERROR, "[CConnectHandler::handle_input] ConnectID = %d, recv data is error nDataLen = [%d] errno = [%d].\n", GetConnectID(), nDataLen, u4Error));
		sprintf_safe(m_szError, MAX_BUFF_500, "[CConnectHandler::handle_input] ConnectID = %d, recv data is error[%d].\n", GetConnectID(), nDataLen);

		//�رյ�ǰ��PacketParse
		ClearPacketParse();

		Close();
		return -1;
	}

	//�����DEBUG״̬����¼��ǰ���ܰ��Ķ���������
	if(App_MainConfig::instance()->GetDebug() == DEBUG_ON || m_blIsLog == true)
	{
		char szDebugData[MAX_BUFF_1024] = {'\0'};
		char szLog[10]  = {'\0'};
		int  nDebugSize = 0; 
		bool blblMore   = false;

		if(nDataLen >= MAX_BUFF_200)
		{
			nDebugSize = MAX_BUFF_200;
			blblMore   = true;
		}
		else
		{
			nDebugSize = nDataLen;
		}

		char* pData = m_pCurrMessage->wr_ptr();
		for(int i = 0; i < nDebugSize; i++)
		{
			sprintf_safe(szLog, 10, "0x%02X ", (unsigned char)pData[i]);
			sprintf_safe(szDebugData + 5*i, MAX_BUFF_1024 - 5*i, "%s", szLog);
		}

		if(blblMore == true)
		{
			AppLogManager::instance()->WriteLog(LOG_SYSTEM_DEBUG_CLIENTRECV, "[(%s)%s:%d]%s.(���ݰ�����ֻ��¼ǰ200�ֽ�)", m_szConnectName, m_addrRemote.get_host_addr(), m_addrRemote.get_port_number(), szDebugData);
		}
		else
		{
			AppLogManager::instance()->WriteLog(LOG_SYSTEM_DEBUG_CLIENTRECV, "[(%s)%s:%d]%s.", m_szConnectName, m_addrRemote.get_host_addr(), m_addrRemote.get_port_number(), szDebugData);
		}
	}

	m_u4CurrSize += nDataLen;

	m_pCurrMessage->wr_ptr(nDataLen);

	if(m_pPacketParse->GetPacketMode() == PACKET_WITHHEAD)
	{
		//���û�ж��꣬�̶�
		if(m_pCurrMessage->size() > m_u4CurrSize)
		{
			Close();
			return 0;
		}
		else if(m_pCurrMessage->length() == m_pPacketParse->GetPacketHeadLen() && m_pPacketParse->GetIsHead() == false)
		{
			bool blStateHead = m_pPacketParse->SetPacketHead(GetConnectID(), m_pCurrMessage, App_MessageBlockManager::instance());
			if(false == blStateHead)
			{
				m_u4CurrSize = 0;
				OUR_DEBUG((LM_ERROR, "[CConnectHandler::handle_input]SetPacketHead is false.\n"));

				//�رյ�ǰ��PacketParse
				ClearPacketParse();

				Close();
				return -1;
			}

			uint32 u4PacketBodyLen = m_pPacketParse->GetPacketBodyLen();
			m_u4CurrSize = 0;


			//��������ֻ������ͷ������
			//�������ֻ�а�ͷ������Ҫ���壬�����������һЩ������������ֻ������ͷ���ӵ�DoMessage()
			if(u4PacketBodyLen == 0)
			{
				//ֻ�����ݰ�ͷ
				if(false == CheckMessage())
				{
					Close();
					return -1;
				}

				m_u4CurrSize = 0;

				//�����µİ�
				m_pPacketParse = App_PacketParsePool::instance()->Create();
				if(NULL == m_pPacketParse)
				{
					OUR_DEBUG((LM_DEBUG,"[%t|CConnectHandle::open] Open(%d) m_pPacketParse new error.\n", GetConnectID()));
					Close();
					return -1;
				}

				//����ͷ�Ĵ�С��Ӧ��mb
				m_pCurrMessage = App_MessageBlockManager::instance()->Create(m_pPacketParse->GetPacketHeadLen());
				if(m_pCurrMessage == NULL)
				{
					AppLogManager::instance()->WriteLog(LOG_SYSTEM_CONNECT, "Close Connection from [%s:%d] RecvSize = %d, RecvCount = %d, SendSize = %d, SendCount = %d, m_u8RecvQueueTimeCost = %dws, m_u4RecvQueueCount = %d, m_u8SendQueueTimeCost = %dws.",m_addrRemote.get_host_addr(), m_addrRemote.get_port_number(), m_u4AllRecvSize, m_u4AllRecvCount, m_u4AllSendSize, m_u4AllSendCount, (uint32)m_u8RecvQueueTimeCost, m_u4RecvQueueCount, (uint32)m_u8SendQueueTimeCost);
					OUR_DEBUG((LM_ERROR, "[CConnectHandle::RecvClinetPacket] pmb new is NULL.\n"));

					//���Ϳͻ������ӶϿ���Ϣ��
					if(false == App_MakePacket::instance()->PutMessageBlock(GetConnectID(), PACKET_CDISCONNECT, NULL))
					{
						OUR_DEBUG((LM_ERROR, "[CProConnectHandle::open] ConnectID = %d, PACKET_CONNECT is error.\n", GetConnectID()));
					}

					Close();
					return -1;
				}
			}
			else
			{
				//����������������ȣ�Ϊ�Ƿ�����
				if(u4PacketBodyLen >= m_u4MaxPacketSize)
				{
					m_u4CurrSize = 0;
					OUR_DEBUG((LM_ERROR, "[CConnectHandler::handle_input]u4PacketHeadLen(%d) more than %d.\n", u4PacketBodyLen, m_u4MaxPacketSize));

					//�رյ�ǰ��PacketParse
					ClearPacketParse();

					Close();
					return -1;
				}
				else
				{
					//OUR_DEBUG((LM_ERROR, "[CConnectHandle::RecvClinetPacket] m_pPacketParse->GetPacketBodyLen())=%d.\n", m_pPacketParse->GetPacketBodyLen()));
					//����ͷ�Ĵ�С��Ӧ��mb
					m_pCurrMessage = App_MessageBlockManager::instance()->Create(m_pPacketParse->GetPacketBodyLen());
					if(m_pCurrMessage == NULL)
					{
						m_u4CurrSize = 0;
						//AppLogManager::instance()->WriteLog(LOG_SYSTEM_CONNECT, "Close Connection from [%s:%d] RecvSize = %d, RecvCount = %d, SendSize = %d, SendCount = %d, m_u8RecvQueueTimeCost = %d, m_u4RecvQueueCount = %d, m_u8SendQueueTimeCost = %d.",m_addrRemote.get_host_addr(), m_addrRemote.get_port_number(), m_u4AllRecvSize, m_u4AllRecvCount, m_u4AllSendSize, m_u4AllSendCount, m_u8RecvQueueTimeCost, m_u4RecvQueueCount, m_u8SendQueueTimeCost);
						OUR_DEBUG((LM_ERROR, "[CConnectHandle::RecvClinetPacket] pmb new is NULL.\n"));

						//�رյ�ǰ��PacketParse
						ClearPacketParse();

						Close();
						return -1;
					}
				}
			}

		}
		else
		{
			//��������������ɣ���ʼ�����������ݰ�
			bool blStateBody = m_pPacketParse->SetPacketBody(GetConnectID(), m_pCurrMessage, App_MessageBlockManager::instance());
			if(false == blStateBody)
			{
				//������ݰ����Ǵ���ģ���Ͽ�����
				m_u4CurrSize = 0;
				OUR_DEBUG((LM_ERROR, "[CConnectHandle::RecvClinetPacket]SetPacketBody is false.\n"));

				//�رյ�ǰ��PacketParse
				ClearPacketParse();

				Close();
				return -1;
			}

			if(false == CheckMessage())
			{
				Close();
				return -1;
			}

			m_u4CurrSize = 0;

			//�����µİ�
			m_pPacketParse = App_PacketParsePool::instance()->Create();
			if(NULL == m_pPacketParse)
			{
				OUR_DEBUG((LM_DEBUG,"[%t|CConnectHandle::open] Open(%d) m_pPacketParse new error.\n", GetConnectID()));
				Close();
				return -1;
			}

			//����ͷ�Ĵ�С��Ӧ��mb
			m_pCurrMessage = App_MessageBlockManager::instance()->Create(m_pPacketParse->GetPacketHeadLen());
			if(m_pCurrMessage == NULL)
			{
				AppLogManager::instance()->WriteLog(LOG_SYSTEM_CONNECT, "Close Connection from [%s:%d] RecvSize = %d, RecvCount = %d, SendSize = %d, SendCount = %d, m_u8RecvQueueTimeCost = %dws, m_u4RecvQueueCount = %d, m_u8SendQueueTimeCost = %dws.",m_addrRemote.get_host_addr(), m_addrRemote.get_port_number(), m_u4AllRecvSize, m_u4AllRecvCount, m_u4AllSendSize, m_u4AllSendCount, (uint32)m_u8RecvQueueTimeCost, m_u4RecvQueueCount, (uint32)m_u8SendQueueTimeCost);
				OUR_DEBUG((LM_ERROR, "[CConnectHandle::RecvClinetPacket] pmb new is NULL.\n"));

				//���Ϳͻ������ӶϿ���Ϣ��
				if(false == App_MakePacket::instance()->PutMessageBlock(GetConnectID(), PACKET_CDISCONNECT, NULL))
				{
					OUR_DEBUG((LM_ERROR, "[CProConnectHandle::open] ConnectID = %d, PACKET_CONNECT is error.\n", GetConnectID()));
				}

				Close();
				return -1;
			}
		}
	}
	else
	{
		//����ģʽ����
		while(true)
		{
			uint8 n1Ret = m_pPacketParse->GetPacketStream(GetConnectID(), m_pCurrMessage, (IMessageBlockManager* )App_MessageBlockManager::instance());
			if(PACKET_GET_ENOUGTH == n1Ret)
			{
				if(false == CheckMessage())
				{
					Close();
					return -1;
				}

				m_u4CurrSize = 0;

				//�����µİ�
				m_pPacketParse = App_PacketParsePool::instance()->Create();
				if(NULL == m_pPacketParse)
				{
					OUR_DEBUG((LM_DEBUG,"[%t|CConnectHandle::open] Open(%d) m_pPacketParse new error.\n", GetConnectID()));
					Close();
					return -1;
				}

				//�����Ƿ���������
				if(m_pCurrMessage->length() == 0)
				{
					m_pCurrMessage->release();
					break;
				}
				else
				{
					//�������ݣ���������
					continue;
				}

			}
			else if(PACKET_GET_NO_ENOUGTH == n1Ret)
			{
				break;
			}
			else
			{
				m_pPacketParse->Clear();

				AppLogManager::instance()->WriteLog(LOG_SYSTEM_CONNECT, "Close Connection from [%s:%d] RecvSize = %d, RecvCount = %d, SendSize = %d, SendCount = %d, m_u8RecvQueueTimeCost = %dws, m_u4RecvQueueCount = %d, m_u8SendQueueTimeCost = %dws.",m_addrRemote.get_host_addr(), m_addrRemote.get_port_number(), m_u4AllRecvSize, m_u4AllRecvCount, m_u4AllSendSize, m_u4AllSendCount, (uint32)m_u8RecvQueueTimeCost, m_u4RecvQueueCount, (uint32)m_u8SendQueueTimeCost);
				OUR_DEBUG((LM_ERROR, "[CConnectHandle::RecvClinetPacket] pmb new is NULL.\n"));

				//���Ϳͻ������ӶϿ���Ϣ��
				if(false == App_MakePacket::instance()->PutMessageBlock(GetConnectID(), PACKET_CDISCONNECT, NULL))
				{
					OUR_DEBUG((LM_ERROR, "[CProConnectHandle::open] ConnectID = %d, PACKET_CONNECT is error.\n", GetConnectID()));
				}

				Close();
				return -1;
			}
		}

		App_MessageBlockManager::instance()->Close(m_pCurrMessage);
		m_u4CurrSize = 0;

		//����ͷ�Ĵ�С��Ӧ��mb
		m_pCurrMessage = App_MessageBlockManager::instance()->Create(App_MainConfig::instance()->GetServerRecvBuff());
		if(m_pCurrMessage == NULL)
		{
			AppLogManager::instance()->WriteLog(LOG_SYSTEM_CONNECT, "Close Connection from [%s:%d] RecvSize = %d, RecvCount = %d, SendSize = %d, SendCount = %d, m_u8RecvQueueTimeCost = %dws, m_u4RecvQueueCount = %d, m_u8SendQueueTimeCost = %dws.",m_addrRemote.get_host_addr(), m_addrRemote.get_port_number(), m_u4AllRecvSize, m_u4AllRecvCount, m_u4AllSendSize, m_u4AllSendCount, (uint32)m_u8RecvQueueTimeCost, m_u4RecvQueueCount, (uint32)m_u8SendQueueTimeCost);
			OUR_DEBUG((LM_ERROR, "[CConnectHandle::RecvClinetPacket] pmb new is NULL.\n"));

			//���Ϳͻ������ӶϿ���Ϣ��
			if(false == App_MakePacket::instance()->PutMessageBlock(GetConnectID(), PACKET_CDISCONNECT, NULL))
			{
				OUR_DEBUG((LM_ERROR, "[CProConnectHandle::open] ConnectID = %d, PACKET_CONNECT is error.\n", GetConnectID()));
			}

			Close();
			return -1;
		}
	}

	Close();
	return 0;	
}

//etģʽ��������
int CConnectHandler::RecvData_et()
{
	m_ThreadLock.acquire();
	m_nIOCount++;
	m_ThreadLock.release();

	while(true)
	{
		//OUR_DEBUG((LM_ERROR, "[CConnectHandler::RecvData_et]m_nIOCount=%d.\n", m_nIOCount));

		//�жϻ����Ƿ�ΪNULL
		if(m_pCurrMessage == NULL)
		{
			m_u4CurrSize = 0;
			OUR_DEBUG((LM_ERROR, "[CConnectHandler::handle_input]m_pCurrMessage == NULL.\n"));
			sprintf_safe(m_szError, MAX_BUFF_500, "[CConnectHandler::handle_input]m_pCurrMessage == NULL.");

			//�رյ�ǰ��PacketParse
			ClearPacketParse();

			Close();
			return -1;
		}

		int nCurrCount = (uint32)m_pCurrMessage->size() - m_u4CurrSize;
		//������Ҫ��m_u4CurrSize���м�顣
		if(nCurrCount < 0)
		{
			OUR_DEBUG((LM_ERROR, "[CConnectHandler::handle_input][%d] nCurrCount < 0 m_u4CurrSize = %d.\n", GetConnectID(), m_u4CurrSize));
			m_u4CurrSize = 0;

			//�رյ�ǰ��PacketParse
			ClearPacketParse();

			Close();

			return -1;
		}

		int nDataLen = this->peer().recv(m_pCurrMessage->wr_ptr(), nCurrCount, MSG_NOSIGNAL);
		//OUR_DEBUG((LM_ERROR, "[CConnectHandler::handle_input] ConnectID=%d, GetData=[%d],errno=[%d].\n", GetConnectID(), nDataLen, errno));
		if(nDataLen <= 0)
		{
			m_u4CurrSize = 0;
			uint32 u4Error = (uint32)errno;

			//�����-1 ��Ϊ11�Ĵ��󣬺���֮
			if(nDataLen == -1 && u4Error == EAGAIN)
			{
				break;
			}

			OUR_DEBUG((LM_ERROR, "[CConnectHandler::handle_input] ConnectID = %d, recv data is error nDataLen = [%d] errno = [%d] EAGAIN=[%d].\n", GetConnectID(), nDataLen, u4Error, EAGAIN));
			sprintf_safe(m_szError, MAX_BUFF_500, "[CConnectHandler::handle_input] ConnectID = %d,nDataLen = [%d],recv data is error[%d].\n", GetConnectID(), nDataLen, u4Error);

			//�رյ�ǰ��PacketParse
			ClearPacketParse();

			Close();
			return -1;
		}

		//�����DEBUG״̬����¼��ǰ���ܰ��Ķ���������
		if(App_MainConfig::instance()->GetDebug() == DEBUG_ON || m_blIsLog == true)
		{
			char szDebugData[MAX_BUFF_1024] = {'\0'};
			char szLog[10]  = {'\0'};
			int  nDebugSize = 0; 
			bool blblMore   = false;

			if(nDataLen >= MAX_BUFF_200)
			{
				nDebugSize = MAX_BUFF_200;
				blblMore   = true;
			}
			else
			{
				nDebugSize = nDataLen;
			}

			char* pData = m_pCurrMessage->wr_ptr();
			for(int i = 0; i < nDebugSize; i++)
			{
				sprintf_safe(szLog, 10, "0x%02X ", (unsigned char)pData[i]);
				sprintf_safe(szDebugData + 5*i, MAX_BUFF_1024 - 5*i, "%s", szLog);
			}

			if(blblMore == true)
			{
				AppLogManager::instance()->WriteLog(LOG_SYSTEM_DEBUG_CLIENTRECV, "[(%s)%s:%d]%s.(���ݰ�����ֻ��¼ǰ200�ֽ�)", m_szConnectName, m_addrRemote.get_host_addr(), m_addrRemote.get_port_number(), szDebugData);
			}
			else
			{
				AppLogManager::instance()->WriteLog(LOG_SYSTEM_DEBUG_CLIENTRECV, "[(%s)%s:%d]%s.", m_szConnectName, m_addrRemote.get_host_addr(), m_addrRemote.get_port_number(), szDebugData);
			}
		}

		m_u4CurrSize += nDataLen;

		m_pCurrMessage->wr_ptr(nDataLen);

		if(m_pPacketParse->GetPacketMode() == PACKET_WITHHEAD)
		{
			//���û�ж��꣬�̶�
			if(m_pCurrMessage->size() > m_u4CurrSize)
			{
				Close();
				return 0;
			}
			else if(m_pCurrMessage->length() == m_pPacketParse->GetPacketHeadLen() && m_pPacketParse->GetIsHead() == false)
			{
				bool blStateHead = m_pPacketParse->SetPacketHead(GetConnectID(), m_pCurrMessage, App_MessageBlockManager::instance());
				if(false == blStateHead)
				{
					m_u4CurrSize = 0;
					OUR_DEBUG((LM_ERROR, "[CConnectHandler::handle_input]SetPacketHead is false.\n"));

					//�رյ�ǰ��PacketParse
					ClearPacketParse();

					Close();
					return -1;
				}

				uint32 u4PacketBodyLen = m_pPacketParse->GetPacketBodyLen();
				m_u4CurrSize = 0;


				//��������ֻ������ͷ������
				//�������ֻ�а�ͷ������Ҫ���壬�����������һЩ������������ֻ������ͷ���ӵ�DoMessage()
				if(u4PacketBodyLen == 0)
				{
					//ֻ�����ݰ�ͷ
					if(false == CheckMessage())
					{
						Close();
						return -1;
					}

					m_u4CurrSize = 0;

					//�����µİ�
					m_pPacketParse = App_PacketParsePool::instance()->Create();
					if(NULL == m_pPacketParse)
					{
						OUR_DEBUG((LM_DEBUG,"[%t|CConnectHandle::open] Open(%d) m_pPacketParse new error.\n", GetConnectID()));
						Close();
						return -1;
					}

					//����ͷ�Ĵ�С��Ӧ��mb
					m_pCurrMessage = App_MessageBlockManager::instance()->Create(m_pPacketParse->GetPacketHeadLen());
					if(m_pCurrMessage == NULL)
					{
						AppLogManager::instance()->WriteLog(LOG_SYSTEM_CONNECT, "Close Connection from [%s:%d] RecvSize = %d, RecvCount = %d, SendSize = %d, SendCount = %d, m_u8RecvQueueTimeCost = %dws, m_u4RecvQueueCount = %d, m_u8SendQueueTimeCost = %dws.",m_addrRemote.get_host_addr(), m_addrRemote.get_port_number(), m_u4AllRecvSize, m_u4AllRecvCount, m_u4AllSendSize, m_u4AllSendCount, (uint32)m_u8RecvQueueTimeCost, m_u4RecvQueueCount, (uint32)m_u8SendQueueTimeCost);
						OUR_DEBUG((LM_ERROR, "[CConnectHandle::RecvClinetPacket] pmb new is NULL.\n"));

						//���Ϳͻ������ӶϿ���Ϣ��
						if(false == App_MakePacket::instance()->PutMessageBlock(GetConnectID(), PACKET_CDISCONNECT, NULL))
						{
							OUR_DEBUG((LM_ERROR, "[CProConnectHandle::open] ConnectID = %d, PACKET_CONNECT is error.\n", GetConnectID()));
						}

						Close();
						return -1;
					}
				}
				else
				{
					//����������������ȣ�Ϊ�Ƿ�����
					if(u4PacketBodyLen >= m_u4MaxPacketSize)
					{
						m_u4CurrSize = 0;
						OUR_DEBUG((LM_ERROR, "[CConnectHandler::handle_input]u4PacketHeadLen(%d) more than %d.\n", u4PacketBodyLen, m_u4MaxPacketSize));

						Close();
						//�رյ�ǰ��PacketParse
						ClearPacketParse();

						return -1;
					}
					else
					{
						//OUR_DEBUG((LM_ERROR, "[CConnectHandle::RecvClinetPacket] m_pPacketParse->GetPacketBodyLen())=%d.\n", m_pPacketParse->GetPacketBodyLen()));
						//����ͷ�Ĵ�С��Ӧ��mb
						m_pCurrMessage = App_MessageBlockManager::instance()->Create(m_pPacketParse->GetPacketBodyLen());
						if(m_pCurrMessage == NULL)
						{
							m_u4CurrSize = 0;
							//AppLogManager::instance()->WriteLog(LOG_SYSTEM_CONNECT, "Close Connection from [%s:%d] RecvSize = %d, RecvCount = %d, SendSize = %d, SendCount = %d, m_u8RecvQueueTimeCost = %d, m_u4RecvQueueCount = %d, m_u8SendQueueTimeCost = %d.",m_addrRemote.get_host_addr(), m_addrRemote.get_port_number(), m_u4AllRecvSize, m_u4AllRecvCount, m_u4AllSendSize, m_u4AllSendCount, m_u8RecvQueueTimeCost, m_u4RecvQueueCount, m_u8SendQueueTimeCost);
							OUR_DEBUG((LM_ERROR, "[CConnectHandle::RecvClinetPacket] pmb new is NULL.\n"));

							Close();
							//�رյ�ǰ��PacketParse
							ClearPacketParse();

							return -1;
						}
					}
				}
			}
			else
			{
				//��������������ɣ���ʼ�����������ݰ�
				bool blStateBody = m_pPacketParse->SetPacketBody(GetConnectID(), m_pCurrMessage, App_MessageBlockManager::instance());
				if(false == blStateBody)
				{
					//������ݰ����Ǵ���ģ���Ͽ�����
					m_u4CurrSize = 0;
					OUR_DEBUG((LM_ERROR, "[CConnectHandle::RecvClinetPacket]SetPacketBody is false.\n"));

					Close();	
					//�رյ�ǰ��PacketParse
					ClearPacketParse();

					return -1;
				}

				if(false == CheckMessage())
				{
					Close();
					return -1;
				}

				m_u4CurrSize = 0;

				//�����µİ�
				m_pPacketParse = App_PacketParsePool::instance()->Create();
				if(NULL == m_pPacketParse)
				{
					OUR_DEBUG((LM_DEBUG,"[%t|CConnectHandle::open] Open(%d) m_pPacketParse new error.\n", GetConnectID()));
					Close();
					return -1;
				}

				//����ͷ�Ĵ�С��Ӧ��mb
				m_pCurrMessage = App_MessageBlockManager::instance()->Create(m_pPacketParse->GetPacketHeadLen());
				if(m_pCurrMessage == NULL)
				{
					AppLogManager::instance()->WriteLog(LOG_SYSTEM_CONNECT, "Close Connection from [%s:%d] RecvSize = %d, RecvCount = %d, SendSize = %d, SendCount = %d, m_u8RecvQueueTimeCost = %dws, m_u4RecvQueueCount = %d, m_u8SendQueueTimeCost = %dws.",m_addrRemote.get_host_addr(), m_addrRemote.get_port_number(), m_u4AllRecvSize, m_u4AllRecvCount, m_u4AllSendSize, m_u4AllSendCount, (uint32)m_u8RecvQueueTimeCost, m_u4RecvQueueCount, (uint32)m_u8SendQueueTimeCost);
					OUR_DEBUG((LM_ERROR, "[CConnectHandle::RecvClinetPacket] pmb new is NULL.\n"));

					//���Ϳͻ������ӶϿ���Ϣ��
					if(false == App_MakePacket::instance()->PutMessageBlock(GetConnectID(), PACKET_CDISCONNECT, NULL))
					{
						OUR_DEBUG((LM_ERROR, "[CProConnectHandle::open] ConnectID = %d, PACKET_CONNECT is error.\n", GetConnectID()));
					}

					Close();
					return -1;
				}
			}
		}
		else
		{
			//����ģʽ����
			while(true)
			{
				uint8 n1Ret = m_pPacketParse->GetPacketStream(GetConnectID(), m_pCurrMessage, (IMessageBlockManager* )App_MessageBlockManager::instance());
				if(PACKET_GET_ENOUGTH == n1Ret)
				{
					if(false == CheckMessage())
					{
						Close();
						return -1;
					}

					m_u4CurrSize = 0;

					//�����µİ�
					m_pPacketParse = App_PacketParsePool::instance()->Create();
					if(NULL == m_pPacketParse)
					{
						OUR_DEBUG((LM_DEBUG,"[%t|CConnectHandle::open] Open(%d) m_pPacketParse new error.\n", GetConnectID()));
						return -1;
					}

					//�����Ƿ���������
					if(m_pCurrMessage->length() == 0)
					{
						m_pCurrMessage->release();
						break;
					}
					else
					{
						//�������ݣ���������
						continue;
					}

				}
				else if(PACKET_GET_NO_ENOUGTH == n1Ret)
				{
					break;
				}
				else
				{
					m_pPacketParse->Clear();

					AppLogManager::instance()->WriteLog(LOG_SYSTEM_CONNECT, "Close Connection from [%s:%d] RecvSize = %d, RecvCount = %d, SendSize = %d, SendCount = %d, m_u8RecvQueueTimeCost = %dws, m_u4RecvQueueCount = %d, m_u8SendQueueTimeCost = %dws.",m_addrRemote.get_host_addr(), m_addrRemote.get_port_number(), m_u4AllRecvSize, m_u4AllRecvCount, m_u4AllSendSize, m_u4AllSendCount, (uint32)m_u8RecvQueueTimeCost, m_u4RecvQueueCount, (uint32)m_u8SendQueueTimeCost);
					OUR_DEBUG((LM_ERROR, "[CConnectHandle::RecvClinetPacket] pmb new is NULL.\n"));

					//���Ϳͻ������ӶϿ���Ϣ��
					if(false == App_MakePacket::instance()->PutMessageBlock(GetConnectID(), PACKET_CDISCONNECT, NULL))
					{
						OUR_DEBUG((LM_ERROR, "[CProConnectHandle::open] ConnectID = %d, PACKET_CONNECT is error.\n", GetConnectID()));
					}

					Close();
					return -1;
				}
			}

			App_MessageBlockManager::instance()->Close(m_pCurrMessage);
			m_u4CurrSize = 0;

			//����ͷ�Ĵ�С��Ӧ��mb
			m_pCurrMessage = App_MessageBlockManager::instance()->Create(App_MainConfig::instance()->GetServerRecvBuff());
			if(m_pCurrMessage == NULL)
			{
				AppLogManager::instance()->WriteLog(LOG_SYSTEM_CONNECT, "Close Connection from [%s:%d] RecvSize = %d, RecvCount = %d, SendSize = %d, SendCount = %d, m_u8RecvQueueTimeCost = %dws, m_u4RecvQueueCount = %d, m_u8SendQueueTimeCost = %dws.",m_addrRemote.get_host_addr(), m_addrRemote.get_port_number(), m_u4AllRecvSize, m_u4AllRecvCount, m_u4AllSendSize, m_u4AllSendCount, (uint32)m_u8RecvQueueTimeCost, m_u4RecvQueueCount, (uint32)m_u8SendQueueTimeCost);
				OUR_DEBUG((LM_ERROR, "[CConnectHandle::RecvClinetPacket] pmb new is NULL.\n"));

				//���Ϳͻ������ӶϿ���Ϣ��
				if(false == App_MakePacket::instance()->PutMessageBlock(GetConnectID(), PACKET_CDISCONNECT, NULL))
				{
					OUR_DEBUG((LM_ERROR, "[CProConnectHandle::open] ConnectID = %d, PACKET_CONNECT is error.\n", GetConnectID()));
				}

				Close();
				return -1;
			}
		}
	}

	Close();
	return 0;		
}

//�ر�����
int CConnectHandler::handle_close(ACE_HANDLE h, ACE_Reactor_Mask mask)
{
	if(h == ACE_INVALID_HANDLE)
	{
		OUR_DEBUG((LM_DEBUG,"[CConnectHandler::handle_close] h is NULL mask=%d.\n", (int)mask));
	}

	OUR_DEBUG((LM_DEBUG,"[CConnectHandler::handle_close]Connectid=[%d] begin(%d)...\n",GetConnectID(), errno));
	App_ConnectManager::instance()->Close(GetConnectID());
	OUR_DEBUG((LM_DEBUG,"[CConnectHandler::handle_close] Connectid=[%d] finish ok...\n", GetConnectID()));
	Close(2);

	return 0;
}

bool CConnectHandler::CheckAlive()
{
	ACE_Time_Value tvNow = ACE_OS::gettimeofday();
	ACE_Time_Value tvIntval(tvNow - m_atvInput);
	if(tvIntval.sec() > m_u2MaxConnectTime)
	{
		//������������ʱ�䣬��������ر�����
		OUR_DEBUG ((LM_ERROR, "[CConnectHandle::CheckAlive] Connectid=%d Server Close!\n", GetConnectID()));

		//�������ӶϿ���Ϣ
		CPacketParse objPacketParse;
		objPacketParse.DisConnect(GetConnectID());		

		//�����﷢��һ�����ӶϿ���Ϣ���߼�
		if(false == App_MakePacket::instance()->PutMessageBlock(GetConnectID(), PACKET_CDISCONNECT, NULL))
		{
			OUR_DEBUG((LM_ERROR, "[CProConnectHandle::open] ConnectID = %d, PACKET_CONNECT is error.\n", GetConnectID()));
		}

		ServerClose();
		return false;
	}
	else
	{
		return true;
	}
}

bool CConnectHandler::SetRecvQueueTimeCost(uint32 u4TimeCost)
{
	m_ThreadLock.acquire();
	m_nIOCount++;
	m_ThreadLock.release();

	//���������ֵ�����¼����־��ȥ
	if((uint32)(m_u8RecvQueueTimeout * 1000) <= u4TimeCost)
	{
		AppLogManager::instance()->WriteLog(LOG_SYSTEM_RECVQUEUEERROR, "[TCP]IP=%s,Prot=%d, m_u8RecvQueueTimeout=[%d], Timeout=[%d].", GetClientIPInfo().m_szClientIP, GetClientIPInfo().m_nPort, (uint32)m_u8RecvQueueTimeout, u4TimeCost);
	}

	m_u4RecvQueueCount++;
	//m_u8RecvQueueTimeCost += u4TimeCost;

	Close();
	return true;
}

bool CConnectHandler::SetSendQueueTimeCost(uint32 u4TimeCost)
{
	m_ThreadLock.acquire();
	m_nIOCount++;
	m_ThreadLock.release();

	//���������ֵ�����¼����־��ȥ
	if((uint32)(m_u8SendQueueTimeout) <= u4TimeCost)
	{
		AppLogManager::instance()->WriteLog(LOG_SYSTEM_SENDQUEUEERROR, "[TCP]IP=%s,Prot=%d,m_u8SendQueueTimeout = [%d], Timeout=[%d].", GetClientIPInfo().m_szClientIP, GetClientIPInfo().m_nPort, (uint32)m_u8SendQueueTimeout, u4TimeCost);
	}

	//m_u8SendQueueTimeCost += u4TimeCost;

	Close();
	return true;
}

uint8 CConnectHandler::GetConnectState()
{
	return m_u1ConnectState;
}

uint8 CConnectHandler::GetSendBuffState()
{
	return m_u1SendBuffState;
}

bool CConnectHandler::SendMessage(uint16 u2CommandID, IBuffPacket* pBuffPacket, bool blState, uint8 u1SendType, uint32& u4PacketSize, bool blDelete)
{
	m_ThreadLock.acquire();
	m_nIOCount++;
	m_ThreadLock.release();	
	//OUR_DEBUG((LM_DEBUG,"[CConnectHandler::SendMessage]Connectid=%d,m_nIOCount=%d.\n", GetConnectID(), m_nIOCount));
	uint32 u4SendSuc = pBuffPacket->GetPacketLen();

	ACE_Message_Block* pMbData = NULL;

	//�������ֱ�ӷ������ݣ���ƴ�����ݰ�
	if(blState == PACKET_SEND_CACHE)
	{
		//���ж�Ҫ���͵����ݳ��ȣ������Ƿ���Է��뻺�壬�����Ƿ��Ѿ�������
		uint32 u4SendPacketSize = 0;
		if(u1SendType == SENDMESSAGE_NOMAL)
		{
			u4SendPacketSize = m_objSendPacketParse.MakePacketLength(GetConnectID(), pBuffPacket->GetPacketLen(), u2CommandID);
		}
		else
		{
			u4SendPacketSize = (uint32)pBuffPacket->GetPacketLen();
		}
		u4PacketSize = u4SendPacketSize;

		if(u4SendPacketSize + (uint32)m_pBlockMessage->length() >= m_u4MaxPacketSize)
		{
			OUR_DEBUG((LM_DEBUG,"[CConnectHandler::SendMessage] Connectid=[%d] m_pBlockMessage is not enougth.\n", GetConnectID()));
			if(blDelete == true)
			{					
				App_BuffPacketManager::instance()->Delete(pBuffPacket);
			}
			Close();
			return false;
		}
		else
		{
			//���ӽ�������
			//ACE_Message_Block* pMbBufferData = NULL;

			//SENDMESSAGE_NOMAL����Ҫ��ͷ��ʱ�򣬷��򣬲����ֱ�ӷ���
			if(u1SendType == SENDMESSAGE_NOMAL)
			{
				//������ɷ������ݰ�
				m_objSendPacketParse.MakePacket(GetConnectID(), pBuffPacket->GetData(), pBuffPacket->GetPacketLen(), m_pBlockMessage, u2CommandID);
			}
			else
			{
				//�������SENDMESSAGE_NOMAL����ֱ�����
				ACE_OS::memcpy(m_pBlockMessage->wr_ptr(), pBuffPacket->GetData(), pBuffPacket->GetPacketLen());
				m_pBlockMessage->wr_ptr(pBuffPacket->GetPacketLen());
			}
		}

		if(blDelete == true)
		{
			//ɾ���������ݰ� 
			App_BuffPacketManager::instance()->Delete(pBuffPacket);
		}
		Close();

		//������ɣ��������˳�
		return true;
	}
	else
	{
		//���ж��Ƿ�Ҫ��װ��ͷ�������Ҫ������װ��m_pBlockMessage��
		uint32 u4SendPacketSize = 0;
		if(u1SendType == SENDMESSAGE_NOMAL)
		{
			u4SendPacketSize = m_objSendPacketParse.MakePacketLength(GetConnectID(), pBuffPacket->GetPacketLen(), u2CommandID);
			m_objSendPacketParse.MakePacket(GetConnectID(), pBuffPacket->GetData(), pBuffPacket->GetPacketLen(), m_pBlockMessage, u2CommandID);
			//����MakePacket�Ѿ��������ݳ��ȣ����������ﲻ��׷��
		}
		else
		{
			u4SendPacketSize = (uint32)pBuffPacket->GetPacketLen();
			ACE_OS::memcpy(m_pBlockMessage->wr_ptr(), pBuffPacket->GetData(), pBuffPacket->GetPacketLen());
			m_pBlockMessage->wr_ptr(pBuffPacket->GetPacketLen());
		}

		//���֮ǰ�л������ݣ���ͻ�������һ����
		u4PacketSize = m_pBlockMessage->length();

		//����϶������0
		if(m_pBlockMessage->length() > 0)
		{
			//��Ϊ���첽���ͣ����͵�����ָ�벻���������ͷţ�������Ҫ�����ﴴ��һ���µķ������ݿ飬�����ݿ���
			pMbData = App_MessageBlockManager::instance()->Create((uint32)m_pBlockMessage->length());
			if(NULL == pMbData)
			{
				OUR_DEBUG((LM_DEBUG,"[CConnectHandler::SendMessage] Connectid=[%d] pMbData is NULL.\n", GetConnectID()));
				if(blDelete == true)
				{
					//ɾ���������ݰ� 
					App_BuffPacketManager::instance()->Delete(pBuffPacket);
				}
				Close();
				return false;
			}

			ACE_OS::memcpy(pMbData->wr_ptr(), m_pBlockMessage->rd_ptr(), m_pBlockMessage->length());
			pMbData->wr_ptr(m_pBlockMessage->length());
			//������ɣ�����ջ������ݣ�ʹ�����
			m_pBlockMessage->reset();
		}

		if(blDelete == true)
		{
			//ɾ���������ݰ� 
			App_BuffPacketManager::instance()->Delete(pBuffPacket);
		}

		bool blRet = PutSendPacket(pMbData);
		if(true == blRet)
		{
			//��¼�ɹ������ֽ�
			m_u4SuccessSendSize += u4SendSuc;
		}

		return blRet;
	}
}

bool CConnectHandler::PutSendPacket(ACE_Message_Block* pMbData)
{

	//�����DEBUG״̬����¼��ǰ���Ͱ��Ķ���������
	if(App_MainConfig::instance()->GetDebug() == DEBUG_ON || m_blIsLog == true)
	{
		char szDebugData[MAX_BUFF_1024] = {'\0'};
		char szLog[10]  = {'\0'};
		int  nDebugSize = 0; 
		bool blblMore   = false;

		if(pMbData->length() >= MAX_BUFF_200)
		{
			nDebugSize = MAX_BUFF_200;
			blblMore   = true;
		}
		else
		{
			nDebugSize = (int)pMbData->length();
		}

		char* pData = pMbData->rd_ptr();
		for(int i = 0; i < nDebugSize; i++)
		{
			sprintf_safe(szLog, 10, "0x%02X ", (unsigned char)pData[i]);
			sprintf_safe(szDebugData + 5*i, MAX_BUFF_1024 - 5*i, "%s", szLog);
		}

		if(blblMore == true)
		{
			AppLogManager::instance()->WriteLog(LOG_SYSTEM_DEBUG_CLIENTSEND, "[(%s)%s:%d]%s.(���ݰ�����ֻ��¼ǰ200�ֽ�)", m_szConnectName, m_addrRemote.get_host_addr(), m_addrRemote.get_port_number(), szDebugData);
		}
		else
		{
			AppLogManager::instance()->WriteLog(LOG_SYSTEM_DEBUG_CLIENTSEND, "[(%s)%s:%d]%s.", m_szConnectName, m_addrRemote.get_host_addr(), m_addrRemote.get_port_number(), szDebugData);
		}
	}

	//ͳ�Ʒ�������
	ACE_Date_Time dtNow;
	if(false == m_TimeConnectInfo.SendCheck((uint8)dtNow.minute(), 1, pMbData->length()))
	{
		//�������޶��ķ�ֵ����Ҫ�ر����ӣ�����¼��־
		AppLogManager::instance()->WriteToMail(LOG_SYSTEM_CONNECTABNORMAL, 
			App_MainConfig::instance()->GetClientDataAlert()->m_u4MailID, 
			(char* )"Alert",
			"[TCP]IP=%s,Prot=%d,SendPacketCount=%d, SendSize=%d.", 
			m_addrRemote.get_host_addr(), 
			m_addrRemote.get_port_number(), 
			m_TimeConnectInfo.m_u4SendPacketCount, 
			m_TimeConnectInfo.m_u4SendSize);

		//���÷��ʱ��
		App_ForbiddenIP::instance()->AddTempIP(m_addrRemote.get_host_addr(), App_MainConfig::instance()->GetIPAlert()->m_u4IPTimeout);
		OUR_DEBUG((LM_ERROR, "[CConnectHandler::PutSendPacket] ConnectID = %d, Send Data is more than limit.\n", GetConnectID()));
		//�Ͽ�����
		Close(2);
		return false;
	}

	//���ͳ�ʱʱ������
	ACE_Time_Value	nowait(0, m_u4SendThresHold*MAX_BUFF_1000);

	if(NULL == pMbData)
	{
		OUR_DEBUG((LM_ERROR, "[CConnectHandler::SendPacket] ConnectID = %d, get_handle() == ACE_INVALID_HANDLE.\n", GetConnectID()));
		Close();
		return false;
	}

	if(get_handle() == ACE_INVALID_HANDLE)
	{
		OUR_DEBUG((LM_ERROR, "[CConnectHandler::SendPacket] ConnectID = %d, get_handle() == ACE_INVALID_HANDLE.\n", GetConnectID()));
		sprintf_safe(m_szError, MAX_BUFF_500, "[CConnectHandler::SendPacket] ConnectID = %d, get_handle() == ACE_INVALID_HANDLE.\n", GetConnectID());
		pMbData->release();
		Close();
		return false;
	}

	//��������
	char* pData = pMbData->rd_ptr();
	if(NULL == pData)
	{
		OUR_DEBUG((LM_ERROR, "[CConnectHandler::SendPacket] ConnectID = %d, pData is NULL.\n", GetConnectID()));
		pMbData->release();
		Close();
		return false;
	}

	int nSendPacketLen = (int)pMbData->length();
	int nIsSendSize    = 0;

	//ѭ�����ͣ�ֱ�����ݷ�����ɡ�
	while(true)
	{
		if(nSendPacketLen <= 0)
		{
			OUR_DEBUG((LM_ERROR, "[CConnectHandler::SendPacket] ConnectID = %d, nCurrSendSize error is %d.\n", GetConnectID(), nSendPacketLen));
			pMbData->release();
			return false;
		}

		int nDataLen = this->peer().send(pMbData->rd_ptr(), nSendPacketLen - nIsSendSize, &nowait);	

		if(nDataLen <= 0)
		{
			OUR_DEBUG((LM_ERROR, "[CConnectHandler::SendPacket] ConnectID = %d, error = %d.\n", GetConnectID(), errno));
			pMbData->release();
			m_atvOutput      = ACE_OS::gettimeofday();
			App_ConnectManager::instance()->Close(GetConnectID());
			Close();
			return false;
		}
		else if(nDataLen >= nSendPacketLen - nIsSendSize)   //�����ݰ�ȫ��������ϣ���ա�
		{
			//OUR_DEBUG((LM_ERROR, "[CConnectHandler::handle_output] ConnectID = %d, send (%d) OK.\n", GetConnectID(), msg_queue()->is_empty()));
			m_u4AllSendCount    += 1;
			m_u4AllSendSize     += (uint32)pMbData->length();
			pMbData->release();
			m_atvOutput      = ACE_OS::gettimeofday();

			//�����Ҫͳ����Ϣ
			App_IPAccount::instance()->UpdateIP((string)m_addrRemote.get_host_addr(), m_addrRemote.get_port_number(), m_u4AllRecvSize, m_u4AllSendSize);

			Close();
			return true;
		}
		else
		{
			pMbData->rd_ptr(nDataLen);
			nIsSendSize      += nDataLen;
			m_atvOutput      = ACE_OS::gettimeofday();
			continue;
		}
	}

	return true;
}

bool CConnectHandler::CheckMessage()
{	
	if(m_pPacketParse->GetMessageBody() == NULL)
	{
		m_u4AllRecvSize += (uint32)m_pPacketParse->GetMessageHead()->length();
	}
	else
	{
		m_u4AllRecvSize += (uint32)m_pPacketParse->GetMessageHead()->length() + (uint32)m_pPacketParse->GetMessageBody()->length();
	}

	m_u4AllRecvCount++;

	//�����Ҫͳ����Ϣ
	App_IPAccount::instance()->UpdateIP((string)m_addrRemote.get_host_addr(), m_addrRemote.get_port_number(), m_u4AllRecvSize, m_u4AllSendSize);

	ACE_Date_Time dtNow;
	if(false == m_TimeConnectInfo.RecvCheck((uint8)dtNow.minute(), 1, m_u4AllRecvSize))
	{
		//�������޶��ķ�ֵ����Ҫ�ر����ӣ�����¼��־
		AppLogManager::instance()->WriteToMail(LOG_SYSTEM_CONNECTABNORMAL, 
			App_MainConfig::instance()->GetClientDataAlert()->m_u4MailID,
			(char* )"Alert", 
			"[TCP]IP=%s,Prot=%d,PacketCount=%d, RecvSize=%d.", 
			m_addrRemote.get_host_addr(), 
			m_addrRemote.get_port_number(), 
			m_TimeConnectInfo.m_u4RecvPacketCount, 
			m_TimeConnectInfo.m_u4RecvSize);

		App_PacketParsePool::instance()->Delete(m_pPacketParse);
		//���÷��ʱ��
		App_ForbiddenIP::instance()->AddTempIP(m_addrRemote.get_host_addr(), App_MainConfig::instance()->GetIPAlert()->m_u4IPTimeout);
		OUR_DEBUG((LM_ERROR, "[CConnectHandle::CheckMessage] ConnectID = %d, PutMessageBlock is check invalid.\n", GetConnectID()));
		return false;
	}

	//������Buff������Ϣ����
	if(false == App_MakePacket::instance()->PutMessageBlock(GetConnectID(), PACKET_PARSE, m_pPacketParse))
	{
		App_PacketParsePool::instance()->Delete(m_pPacketParse);
		OUR_DEBUG((LM_ERROR, "[CConnectHandle::CheckMessage] ConnectID = %d, PutMessageBlock is error.\n", GetConnectID()));
	}

	/*
	//���Դ���
	m_pPacketParse->GetMessageHead()->release();
	m_pPacketParse->GetMessageBody()->release();
	App_PacketParsePool::instance()->Delete(m_pPacketParse);
	*/

	return true;
}

_ClientConnectInfo CConnectHandler::GetClientInfo()
{
	_ClientConnectInfo ClientConnectInfo;

	ClientConnectInfo.m_blValid             = true;
	ClientConnectInfo.m_u4ConnectID         = GetConnectID();
	ClientConnectInfo.m_addrRemote          = m_addrRemote;
	ClientConnectInfo.m_u4RecvCount         = m_u4AllRecvCount;
	ClientConnectInfo.m_u4SendCount         = m_u4AllSendCount;
	ClientConnectInfo.m_u4AllRecvSize       = m_u4AllSendSize;
	ClientConnectInfo.m_u4AllSendSize       = m_u4AllSendSize;
	ClientConnectInfo.m_u4BeginTime         = (uint32)m_atvConnect.sec();
	ClientConnectInfo.m_u4AliveTime         = (uint32)(ACE_OS::gettimeofday().sec() -  m_atvConnect.sec());
	ClientConnectInfo.m_u4RecvQueueCount    = m_u4RecvQueueCount;
	ClientConnectInfo.m_u8RecvQueueTimeCost = m_u8RecvQueueTimeCost;
	ClientConnectInfo.m_u8SendQueueTimeCost = m_u8SendQueueTimeCost;

	return ClientConnectInfo;
}

_ClientIPInfo  CConnectHandler::GetClientIPInfo()
{
	_ClientIPInfo ClientIPInfo;
	sprintf_safe(ClientIPInfo.m_szClientIP, MAX_BUFF_20, "%s", m_addrRemote.get_host_addr());
	ClientIPInfo.m_nPort = (int)m_addrRemote.get_port_number();
	return ClientIPInfo;
}

bool CConnectHandler::CheckSendMask(uint32 u4PacketLen)
{
	m_u4ReadSendSize += u4PacketLen;
	//OUR_DEBUG ((LM_ERROR, "[CConnectHandler::CheckSendMask]GetSendDataMask = %d, m_u4ReadSendSize=%d, m_u4SuccessSendSize=%d.\n", App_MainConfig::instance()->GetSendDataMask(), m_u4ReadSendSize, m_u4SuccessSendSize));
	if(m_u4ReadSendSize - m_u4SuccessSendSize >= App_MainConfig::instance()->GetSendDataMask())
	{
		OUR_DEBUG ((LM_ERROR, "[CConnectHandler::CheckSendMask]ConnectID = %d, SingleConnectMaxSendBuffer is more than(%d)!\n", GetConnectID(), m_u4ReadSendSize - m_u4SuccessSendSize));
		AppLogManager::instance()->WriteLog(LOG_SYSTEM_SENDQUEUEERROR, "]Connection from [%s:%d], SingleConnectMaxSendBuffer is more than(%d)!.", m_addrRemote.get_host_addr(), m_addrRemote.get_port_number(), m_u4ReadSendSize - m_u4SuccessSendSize);
		return false;
	}
	else
	{
		return true;
	}
}

void CConnectHandler::ClearPacketParse()
{
	if(m_pPacketParse->GetMessageHead() != NULL)
	{
		m_pPacketParse->GetMessageHead()->release();
	}

	if(m_pPacketParse->GetMessageBody() != NULL)
	{
		m_pPacketParse->GetMessageBody()->release();
	}

	if(m_pCurrMessage != NULL && m_pPacketParse->GetMessageBody() != m_pCurrMessage && m_pPacketParse->GetMessageHead() != m_pCurrMessage)
	{
		m_pCurrMessage->release();
	}
	m_pCurrMessage = NULL;

	App_PacketParsePool::instance()->Delete(m_pPacketParse);
}

void CConnectHandler::SetConnectName(const char* pName)
{
	sprintf_safe(m_szConnectName, MAX_BUFF_100, "%s", pName);
}

void CConnectHandler::SetIsLog(bool blIsLog)
{
	m_blIsLog = blIsLog;
}

char* CConnectHandler::GetConnectName()
{
	return m_szConnectName;
}

bool CConnectHandler::GetIsLog()
{
	return m_blIsLog;
}

//***************************************************************************
CConnectManager::CConnectManager(void)
{
	m_u4TimeCheckID      = 0;
	m_szError[0]         = '\0';

	m_pTCTimeSendCheck   = NULL;
	m_tvCheckConnect     = ACE_OS::gettimeofday();
	m_blRun              = false;

	m_u4TimeConnect      = 0;
	m_u4TimeDisConnect   = 0;

	//��ʼ�����Ͷ����
	m_SendMessagePool.Init();
}

CConnectManager::~CConnectManager(void)
{
	OUR_DEBUG((LM_INFO, "[CConnectManager::~CConnectManager].\n"));
	//m_blRun = false;
	//CloseAll();
}

void CConnectManager::CloseAll()
{
	//ACE_Guard<ACE_Recursive_Thread_Mutex> WGrard(m_ThreadWriteLock);
	msg_queue()->deactivate();

	KillTimer();

	for(mapConnectManager::iterator b = m_mapConnectManager.begin(); b != m_mapConnectManager.end();)
	{
		mapConnectManager::iterator itr = b;
		CConnectHandler* pConnectHandler = (CConnectHandler* )itr->second;
		if(pConnectHandler != NULL)
		{
			if(true == pConnectHandler->Close())
			{
				itr++;
				b = itr;
			}
			else
			{
				b++;
			}
			m_u4TimeDisConnect++;

			//��������ͳ�ƹ���
			App_ConnectAccount::instance()->AddDisConnect();
		}
		else
		{
			b++;
		}

	}

	m_mapConnectManager.clear();
}

bool CConnectManager::Close(uint32 u4ConnectID)
{
	ACE_Guard<ACE_Recursive_Thread_Mutex> WGuard(m_ThreadWriteLock);
	mapConnectManager::iterator f = m_mapConnectManager.find(u4ConnectID);

	if(f != m_mapConnectManager.end())
	{
		CConnectHandler* pConnectHandler = (CConnectHandler* )f->second;
		if(pConnectHandler != NULL)
		{
			m_mapConnectManager.erase(f);
			m_u4TimeDisConnect++;
		}

		//��������ͳ�ƹ���
		App_ConnectAccount::instance()->AddDisConnect();

		return true;
	}
	else
	{
		sprintf_safe(m_szError, MAX_BUFF_500, "[CConnectManager::Close] ConnectID[%d] is not find.", u4ConnectID);
		return true;
	}
}

bool CConnectManager::CloseConnect(uint32 u4ConnectID)
{
	//ACE_Guard<ACE_Recursive_Thread_Mutex> WGuard(m_ThreadWriteLock);
	mapConnectManager::iterator f = m_mapConnectManager.find(u4ConnectID);

	if(f != m_mapConnectManager.end())
	{
		CConnectHandler* pConnectHandler = (CConnectHandler* )f->second;
		if(pConnectHandler != NULL)
		{
			pConnectHandler->ServerClose();
			m_u4TimeDisConnect++;

			//��������ͳ�ƹ���
			App_ConnectAccount::instance()->AddDisConnect();
		}
		m_mapConnectManager.erase(f);
		return true;
	}
	else
	{
		sprintf_safe(m_szError, MAX_BUFF_500, "[CConnectManager::CloseConnect] ConnectID[%d] is not find.", u4ConnectID);
		return true;
	}
}

bool CConnectManager::AddConnect(uint32 u4ConnectID, CConnectHandler* pConnectHandler)
{
	//ACE_Guard<ACE_Recursive_Thread_Mutex> WGuard(m_ThreadWriteLock);
	if(pConnectHandler == NULL)
	{
		sprintf_safe(m_szError, MAX_BUFF_500, "[CConnectManager::AddConnect] pConnectHandler is NULL.");
		return false;		
	}

	mapConnectManager::iterator f = m_mapConnectManager.find(u4ConnectID);
	if(f != m_mapConnectManager.end())
	{
		sprintf_safe(m_szError, MAX_BUFF_500, "[CConnectManager::AddConnect] ConnectID[%d] is exist.", u4ConnectID);
		return false;
	}

	pConnectHandler->SetConnectID(u4ConnectID);
	//����map
	m_mapConnectManager.insert(mapConnectManager::value_type(u4ConnectID, pConnectHandler));
	m_u4TimeConnect++;

	//��������ͳ�ƹ���
	App_ConnectAccount::instance()->AddConnect();

	return true;
}

bool CConnectManager::SendMessage(uint32 u4ConnectID, IBuffPacket* pBuffPacket, uint16 u2CommandID, bool blSendState, uint8 u1SendType, ACE_Time_Value& tvSendBegin, bool blDelete)
{
	//ACE_Guard<ACE_Recursive_Thread_Mutex> WGuard(m_ThreadWriteLock);
	//��Ϊ�Ƕ��е��ã��������ﲻ��Ҫ�����ˡ�
	if(NULL == pBuffPacket)
	{
		sprintf_safe(m_szError, MAX_BUFF_500, "[CConnectManager::SendMessage] ConnectID[%d] pBuffPacket is NULL.", u4ConnectID);
		return false;
	}

	mapConnectManager::iterator f = m_mapConnectManager.find(u4ConnectID);

	if(f != m_mapConnectManager.end())
	{
		CConnectHandler* pConnectHandler = (CConnectHandler* )f->second;

		if(NULL != pConnectHandler)
		{
			uint32 u4PacketSize = 0;
			pConnectHandler->SendMessage(u2CommandID, pBuffPacket, blSendState, u1SendType, u4PacketSize, blDelete);

			//��¼��Ϣ��������ʱ��
			ACE_Time_Value tvInterval = ACE_OS::gettimeofday() - tvSendBegin;
			uint32 u4SendCost = (uint32)(tvInterval.msec());
			pConnectHandler->SetSendQueueTimeCost(u4SendCost);
			//App_CommandAccount::instance()->SaveCommandData_Mutex(u2CommandID, (uint8)u4SendCost, PACKET_TCP, u4PacketSize, u4CommandSize, COMMAND_TYPE_OUT);
			return true;
		}
		else
		{
			sprintf_safe(m_szError, MAX_BUFF_500, "[CConnectManager::SendMessage] ConnectID[%d] is not find.", u4ConnectID);
			App_BuffPacketManager::instance()->Delete(pBuffPacket);
			return true;
		}
	}
	else
	{
		sprintf_safe(m_szError, MAX_BUFF_500, "[CConnectManager::SendMessage] ConnectID[%d] is not find.", u4ConnectID);
		App_BuffPacketManager::instance()->Delete(pBuffPacket);
		m_ThreadWriteLock.release();
		return true;
	}

	return true;
}

bool CConnectManager::PostMessage(uint32 u4ConnectID, IBuffPacket* pBuffPacket, uint8 u1SendType, uint16 u2CommandID, bool blSendState, bool blDelete)
{
	//OUR_DEBUG((LM_INFO, "[CConnectManager::PostMessage]Begin.\n"));
	//ACE_Message_Block* mb = App_MessageBlockManager::instance()->Create(sizeof(_SendMessage*));
	ACE_Message_Block* mb = NULL;

	//�ж��Ƿ�ﵽ�˷��ͷ�ֵ������ﵽ�ˣ���ֱ�ӶϿ����ӡ�
	mapConnectManager::iterator f = m_mapConnectManager.find(u4ConnectID);

	if(f != m_mapConnectManager.end())
	{
		CConnectHandler* pConnectHandler = (CConnectHandler* )f->second;
		if(NULL != pConnectHandler)
		{
			bool blState = pConnectHandler->CheckSendMask(pBuffPacket->GetPacketLen());
			if(false == blState)
			{
				if(blDelete == true)
				{
					//�����˷�ֵ����ر�����
					App_BuffPacketManager::instance()->Delete(pBuffPacket);
				}
				return false;
			}
		}
	}
	else
	{
		//�������ѹ���Ͳ����ڣ��򲻽������ݶ��У�ֱ�Ӷ����������ݣ�������ʧ�ܡ�
		OUR_DEBUG((LM_ERROR,"[CMessageService::PutMessage] u4ConnectID(%d) is not exist.\n", u4ConnectID));
		if(blDelete == true)
		{
			App_BuffPacketManager::instance()->Delete(pBuffPacket);
		}
		return false;
	}

	ACE_NEW_MALLOC_NORETURN(mb, 
		static_cast<ACE_Message_Block*>(_msg_send_mb_allocator.malloc(sizeof(ACE_Message_Block))),
		ACE_Message_Block(sizeof(_SendMessage*), // size
		ACE_Message_Block::MB_DATA, // type
		0,
		0,
		&_msg_send_mb_allocator, // allocator_strategy
		0, // locking strategy
		ACE_DEFAULT_MESSAGE_BLOCK_PRIORITY, // priority
		ACE_Time_Value::zero,
		ACE_Time_Value::max_time,
		&_msg_send_mb_allocator,
		&_msg_send_mb_allocator
		));

	if(NULL != mb)
	{
		//���뷢�Ͷ���
		_SendMessage* pSendMessage = m_SendMessagePool.Create();

		if(NULL == pSendMessage)
		{
			OUR_DEBUG((LM_ERROR,"[CConnectManager::PutMessage] new _SendMessage is error.\n"));
			return false;
		}

		pSendMessage->m_u4ConnectID = u4ConnectID;
		pSendMessage->m_pBuffPacket = pBuffPacket;
		pSendMessage->m_nEvents     = u1SendType;
		pSendMessage->m_u2CommandID = u2CommandID;
		pSendMessage->m_blDelete    = blDelete;
		pSendMessage->m_blSendState = blSendState;
		pSendMessage->m_tvSend      = ACE_OS::gettimeofday();

		_SendMessage** ppSendMessage = (_SendMessage **)mb->base();
		*ppSendMessage = pSendMessage;

		//�ж϶����Ƿ����Ѿ����
		int nQueueCount = (int)msg_queue()->message_count();
		if(nQueueCount >= (int)MAX_MSG_THREADQUEUE)
		{
			OUR_DEBUG((LM_ERROR,"[CConnectManager::PutMessage] Queue is Full nQueueCount = [%d].\n", nQueueCount));
			if(blDelete == true)
			{
				App_BuffPacketManager::instance()->Delete(pBuffPacket);
			}
			m_SendMessagePool.Delete(pSendMessage);
			mb->release();
			return false;
		}

		ACE_Time_Value xtime = ACE_OS::gettimeofday() + ACE_Time_Value(0, m_u4SendQueuePutTime);
		if(this->putq(mb, &xtime) == -1)
		{
			OUR_DEBUG((LM_ERROR,"[CConnectManager::PutMessage] Queue putq  error nQueueCount = [%d] errno = [%d].\n", nQueueCount, errno));
			if(blDelete == true)
			{
				App_BuffPacketManager::instance()->Delete(pBuffPacket);
			}
			m_SendMessagePool.Delete(pSendMessage);
			mb->release();
			return false;
		}
	}
	else
	{
		OUR_DEBUG((LM_ERROR,"[CMessageService::PutMessage] mb new error.\n"));
		if(blDelete == true)
		{
			App_BuffPacketManager::instance()->Delete(pBuffPacket);
		}
		return false;
	}

	return true;
}

const char* CConnectManager::GetError()
{
	return m_szError;
}

bool CConnectManager::StartTimer()
{
	//���������߳�
	if(0 != open())
	{
		OUR_DEBUG((LM_ERROR, "[CConnectManager::StartTimer]Open() is error.\n"));
		return false;
	}

	//���ⶨʱ���ظ�����
	KillTimer();
	OUR_DEBUG((LM_ERROR, "CConnectManager::StartTimer()-->begin....\n"));
	//�õ��ڶ���Reactor
	ACE_Reactor* pReactor = App_ReactorManager::instance()->GetAce_Reactor(REACTOR_POSTDEFINE);
	if(NULL == pReactor)
	{
		OUR_DEBUG((LM_ERROR, "CConnectManager::StartTimer() -->GetAce_Reactor(REACTOR_POSTDEFINE) is NULL.\n"));
		return false;
	}

	m_pTCTimeSendCheck = new _TimerCheckID();
	if(NULL == m_pTCTimeSendCheck)
	{
		OUR_DEBUG((LM_ERROR, "CConnectManager::StartTimer() m_pTCTimeSendCheck is NULL.\n"));
		return false;
	}

	m_pTCTimeSendCheck->m_u2TimerCheckID = PARM_CONNECTHANDLE_CHECK;
	m_u4TimeCheckID = pReactor->schedule_timer(this, (const void *)m_pTCTimeSendCheck, ACE_Time_Value(App_MainConfig::instance()->GetSendAliveTime(), 0), ACE_Time_Value(App_MainConfig::instance()->GetSendAliveTime(), 0));
	if(0 == m_u4TimeCheckID)
	{
		OUR_DEBUG((LM_ERROR, "CConnectManager::StartTimer()--> Start thread m_u4TimeCheckID error.\n"));
		return false;
	}
	else
	{
		OUR_DEBUG((LM_ERROR, "CConnectManager::StartTimer()--> Start thread time OK.\n"));
		return true;
	}
}

bool CConnectManager::KillTimer()
{
	if(m_u4TimeCheckID > 0)
	{
		App_ReactorManager::instance()->GetAce_Reactor(REACTOR_POSTDEFINE)->cancel_timer(m_u4TimeCheckID);
		m_u4TimeCheckID = 0;
	}

	SAFE_DELETE(m_pTCTimeSendCheck);
	return true;
}

int CConnectManager::handle_timeout(const ACE_Time_Value &tv, const void *arg)
{ 
	//ACE_Guard<ACE_Recursive_Thread_Mutex> WGuard(m_ThreadWriteLock);
	if(arg == NULL)
	{
		OUR_DEBUG((LM_ERROR, "[CConnectManager::handle_timeout]arg is not NULL, tv = %d.\n", tv.sec()));
	}

	_TimerCheckID* pTimerCheckID = (_TimerCheckID*)arg;
	if(NULL == pTimerCheckID)
	{
		return 0;
	}

	//��ʱ��ⷢ�ͣ����ｫ��ʱ��¼������Ϣ�������У�����һ����ʱ��
	if(pTimerCheckID->m_u2TimerCheckID == PARM_CONNECTHANDLE_CHECK)
	{
		if(m_mapConnectManager.size() == 0)
		{
		}
		else
		{
			for(mapConnectManager::iterator b = m_mapConnectManager.begin(); b != m_mapConnectManager.end();)
			{
				CConnectHandler* pConnectHandler = (CConnectHandler* )b->second;
				if(pConnectHandler != NULL)
				{
					if(false == pConnectHandler->CheckAlive())
					{
						m_mapConnectManager.erase(b++);
					}
					else
					{
						b++;
					}
				}
				else
				{
					b++;
				}
			}
		}

		//�ж��Ƿ�Ӧ�ü�¼������־
		ACE_Time_Value tvNow = ACE_OS::gettimeofday();
		ACE_Time_Value tvInterval(tvNow - m_tvCheckConnect);
		if(tvInterval.sec() >= MAX_MSG_HANDLETIME)
		{
			AppLogManager::instance()->WriteLog(LOG_SYSTEM_CONNECT, "[CConnectManager]CurrConnectCount = %d,TimeInterval=%d, TimeConnect=%d, TimeDisConnect=%d.", 
				GetCount(), MAX_MSG_HANDLETIME, m_u4TimeConnect, m_u4TimeDisConnect);

			//���õ�λʱ���������ͶϿ�������
			m_u4TimeConnect    = 0;
			m_u4TimeDisConnect = 0;
			m_tvCheckConnect   = tvNow;
		}

		//������������Ƿ�Խ��ط�ֵ
		if(App_MainConfig::instance()->GetConnectAlert()->m_u4ConnectAlert > 0)
		{
			if(GetCount() > (int)App_MainConfig::instance()->GetConnectAlert()->m_u4ConnectAlert)
			{
				AppLogManager::instance()->WriteToMail(LOG_SYSTEM_CONNECT,
					App_MainConfig::instance()->GetConnectAlert()->m_u4MailID,
					(char* )"Alert",
					"[CProConnectManager]active ConnectCount is more than limit(%d > %d).", 
					GetCount(),
					App_MainConfig::instance()->GetConnectAlert()->m_u4ConnectAlert);
			}
		}

		//��ⵥλʱ���������Ƿ�Խ��ֵ
		int nCheckRet = App_ConnectAccount::instance()->CheckConnectCount();
		if(nCheckRet == 1)
		{
			AppLogManager::instance()->WriteToMail(LOG_SYSTEM_CONNECT,
				App_MainConfig::instance()->GetConnectAlert()->m_u4MailID,
				(char* )"Alert",
				"[CProConnectManager]CheckConnectCount is more than limit(%d > %d).", 
				App_ConnectAccount::instance()->GetCurrConnect(),
				App_ConnectAccount::instance()->GetConnectMax());
		}
		else if(nCheckRet == 2)
		{
			AppLogManager::instance()->WriteToMail(LOG_SYSTEM_CONNECT,
				App_MainConfig::instance()->GetConnectAlert()->m_u4MailID,
				(char* )"Alert",
				"[CProConnectManager]CheckConnectCount is little than limit(%d < %d).", 
				App_ConnectAccount::instance()->GetCurrConnect(),
				App_ConnectAccount::instance()->Get4ConnectMin());
		}

		//��ⵥλʱ�����ӶϿ����Ƿ�Խ��ֵ
		nCheckRet = App_ConnectAccount::instance()->CheckDisConnectCount();
		if(nCheckRet == 1)
		{
			AppLogManager::instance()->WriteToMail(LOG_SYSTEM_CONNECT,
				App_MainConfig::instance()->GetConnectAlert()->m_u4MailID,
				(char* )"Alert",
				"[CProConnectManager]CheckDisConnectCount is more than limit(%d > %d).", 
				App_ConnectAccount::instance()->GetCurrConnect(),
				App_ConnectAccount::instance()->GetDisConnectMax());
		}
		else if(nCheckRet == 2)
		{
			AppLogManager::instance()->WriteToMail(LOG_SYSTEM_CONNECT,
				App_MainConfig::instance()->GetConnectAlert()->m_u4MailID,
				(char* )"Alert",
				"[CProConnectManager]CheckDisConnectCount is little than limit(%d < %d).", 
				App_ConnectAccount::instance()->GetCurrConnect(),
				App_ConnectAccount::instance()->GetDisConnectMin());
		}

		return 0;

	}

	return 0;
}

int CConnectManager::GetCount()
{
	//ACE_Guard<ACE_Recursive_Thread_Mutex> WGuard(m_ThreadWriteLock);
	return (int)m_mapConnectManager.size(); 
}

int CConnectManager::open(void* args)
{
	if(args != NULL)
	{
		OUR_DEBUG((LM_INFO,"[CConnectManager::open]args is not NULL.\n"));
	}

	m_blRun = true;
	msg_queue()->high_water_mark(MAX_MSG_MASK);
	msg_queue()->low_water_mark(MAX_MSG_MASK);

	OUR_DEBUG((LM_INFO,"[CConnectManager::open] m_u4HighMask = [%d] m_u4LowMask = [%d]\n", MAX_MSG_MASK, MAX_MSG_MASK));
	if(activate(THR_NEW_LWP | THR_JOINABLE | THR_INHERIT_SCHED | THR_SUSPENDED, MAX_MSG_THREADCOUNT) == -1)
	{
		OUR_DEBUG((LM_ERROR, "[CConnectManager::open] activate error ThreadCount = [%d].", MAX_MSG_THREADCOUNT));
		m_blRun = false;
		return -1;
	}

	m_u4SendQueuePutTime = App_MainConfig::instance()->GetSendQueuePutTime() * 1000;

	resume();

	return 0;
}

int CConnectManager::svc (void)
{
	ACE_Message_Block* mb = NULL;
	ACE_Time_Value xtime;

	while(IsRun())
	{
		mb = NULL;
		if(getq(mb, 0) == -1)
		{
			OUR_DEBUG((LM_ERROR,"[CConnectManager::svc] get error errno = [%d].\n", errno));
			m_blRun = false;
			break;
		}
		if (mb == NULL)
		{
			continue;
		}
		_SendMessage* msg = *((_SendMessage**)mb->base());
		if (! msg)
		{
			mb->release();
			continue;
		}

		//������������
		SendMessage(msg->m_u4ConnectID, msg->m_pBuffPacket, msg->m_u2CommandID, msg->m_blSendState, msg->m_nEvents, msg->m_tvSend);

		m_SendMessagePool.Delete(msg);

		mb->release();
	}

	OUR_DEBUG((LM_INFO,"[CConnectManager::svc] svc finish!\n"));
	return 0;
}

bool CConnectManager::IsRun()
{
	return m_blRun;
}

void CConnectManager::CloseQueue()
{
	this->msg_queue()->deactivate();
}

int CConnectManager::close(u_long)
{
	m_blRun = false;
	OUR_DEBUG((LM_INFO,"[CConnectManager::close] close().\n"));
	return 0;
}

void CConnectManager::SetRecvQueueTimeCost(uint32 u4ConnectID, uint32 u4TimeCost)
{
	//ACE_Guard<ACE_Recursive_Thread_Mutex> WGuard(m_ThreadWriteLock);
	mapConnectManager::iterator f = m_mapConnectManager.find(u4ConnectID);

	if(f != m_mapConnectManager.end())
	{
		CConnectHandler* pConnectHandler = (CConnectHandler* )f->second;
		if(NULL != pConnectHandler)
		{
			pConnectHandler->SetRecvQueueTimeCost(u4TimeCost);
		}
	}
}

void CConnectManager::GetConnectInfo(vecClientConnectInfo& VecClientConnectInfo)
{
	//ACE_Guard<ACE_Recursive_Thread_Mutex> WGuard(m_ThreadWriteLock);

	for(mapConnectManager::iterator b = m_mapConnectManager.begin(); b != m_mapConnectManager.end(); b++)
	{
		CConnectHandler* pConnectHandler = (CConnectHandler* )b->second;
		if(pConnectHandler != NULL)
		{
			VecClientConnectInfo.push_back(pConnectHandler->GetClientInfo());
		}
	}
}

_ClientIPInfo CConnectManager::GetClientIPInfo(uint32 u4ConnectID)
{
	//ACE_Guard<ACE_Recursive_Thread_Mutex> WGuard(m_ThreadWriteLock);
	mapConnectManager::iterator f = m_mapConnectManager.find(u4ConnectID);

	if(f != m_mapConnectManager.end())
	{
		CConnectHandler* pConnectHandler = (CConnectHandler* )f->second;
		if(NULL != pConnectHandler)
		{
			return pConnectHandler->GetClientIPInfo();
		}
		else
		{
			_ClientIPInfo ClientIPInfo;
			return ClientIPInfo;
		}
	}
	else
	{
		_ClientIPInfo ClientIPInfo;
		return ClientIPInfo;
	}
}

bool CConnectManager::PostMessageAll(IBuffPacket* pBuffPacket, uint8 u1SendType, uint16 u2CommandID, bool blSendState, bool blDelete)
{
	m_ThreadWriteLock.acquire();
	vecConnectManager objvecConnectManager;
	for(mapConnectManager::iterator b = m_mapConnectManager.begin(); b != m_mapConnectManager.end(); b++)
	{
		objvecConnectManager.push_back((uint32)b->first);
	}
	m_ThreadWriteLock.release();

	uint32 u4ConnectID = 0;
	for(uint32 i = 0; i < (uint32)objvecConnectManager.size(); i++)
	{
		IBuffPacket* pCurrBuffPacket = App_BuffPacketManager::instance()->Create();
		if(NULL == pCurrBuffPacket)
		{
			OUR_DEBUG((LM_INFO, "[CConnectManager::PostMessage]pCurrBuffPacket is NULL.\n"));
			if(blDelete == true)
			{
				App_BuffPacketManager::instance()->Delete(pBuffPacket);
			}
			return false;
		}

		pCurrBuffPacket->WriteStream(pBuffPacket->GetData(), pBuffPacket->GetPacketLen());

		u4ConnectID = objvecConnectManager[i];

		//�ж��Ƿ�ﵽ�˷��ͷ�ֵ������ﵽ�ˣ���ֱ�ӶϿ����ӡ�
		mapConnectManager::iterator f = m_mapConnectManager.find(u4ConnectID);

		if(f != m_mapConnectManager.end())
		{
			CConnectHandler* pConnectHandler = (CConnectHandler* )f->second;
			if(NULL != pConnectHandler)
			{
				bool blState = pConnectHandler->CheckSendMask(pBuffPacket->GetPacketLen());
				if(false == blState)
				{
					if(blDelete == true)
					{
						//�����˷�ֵ����ر�����
						App_BuffPacketManager::instance()->Delete(pBuffPacket);
					}
					continue;
				}
			}
		}
		else
		{
			//�������ѹ���Ͳ����ڣ��򲻽������ݶ��У�ֱ�Ӷ����������ݣ�������ʧ�ܡ�
			OUR_DEBUG((LM_ERROR,"[CMessageService::PutMessage] u4ConnectID(%d) is not exist.\n", u4ConnectID));
			if(blDelete == true)
			{
				App_BuffPacketManager::instance()->Delete(pBuffPacket);
			}
			continue;
		}

		ACE_Message_Block* mb = NULL;

		ACE_NEW_MALLOC_NORETURN(mb, 
			static_cast<ACE_Message_Block*>(_msg_send_mb_allocator.malloc(sizeof(ACE_Message_Block))),
			ACE_Message_Block(sizeof(_SendMessage*), // size
			ACE_Message_Block::MB_DATA, // type
			0,
			0,
			&_msg_send_mb_allocator, // allocator_strategy
			0, // locking strategy
			ACE_DEFAULT_MESSAGE_BLOCK_PRIORITY, // priority
			ACE_Time_Value::zero,
			ACE_Time_Value::max_time,
			&_msg_send_mb_allocator,
			&_msg_send_mb_allocator
			));

		if(NULL != mb)
		{
			//���뷢�Ͷ���
			_SendMessage* pSendMessage = m_SendMessagePool.Create();

			if(NULL == pSendMessage)
			{
				OUR_DEBUG((LM_ERROR,"[CConnectManager::PutMessage] new _SendMessage is error.\n"));
				if(blDelete == true)
				{
					App_BuffPacketManager::instance()->Delete(pBuffPacket);
				}
				return false;
			}

			pSendMessage->m_u4ConnectID = u4ConnectID;
			pSendMessage->m_pBuffPacket = pCurrBuffPacket;
			pSendMessage->m_nEvents     = u1SendType;
			pSendMessage->m_u2CommandID = u2CommandID;
			pSendMessage->m_blDelete    = blDelete;
			pSendMessage->m_blSendState = blSendState;
			pSendMessage->m_tvSend      = ACE_OS::gettimeofday();

			_SendMessage** ppSendMessage = (_SendMessage **)mb->base();
			*ppSendMessage = pSendMessage;

			//�ж϶����Ƿ����Ѿ����
			int nQueueCount = (int)msg_queue()->message_count();
			if(nQueueCount >= (int)MAX_MSG_THREADQUEUE)
			{
				OUR_DEBUG((LM_ERROR,"[CConnectManager::PutMessage] Queue is Full nQueueCount = [%d].\n", nQueueCount));
				if(blDelete == true)
				{
					App_BuffPacketManager::instance()->Delete(pBuffPacket);
				}
				m_SendMessagePool.Delete(pSendMessage);
				mb->release();
				return false;
			}

			ACE_Time_Value xtime = ACE_OS::gettimeofday() + ACE_Time_Value(0, MAX_MSG_PUTTIMEOUT);
			if(this->putq(mb, &xtime) == -1)
			{
				OUR_DEBUG((LM_ERROR,"[CConnectManager::PutMessage] Queue putq  error nQueueCount = [%d] errno = [%d].\n", nQueueCount, errno));
				if(blDelete == true)
				{
					App_BuffPacketManager::instance()->Delete(pBuffPacket);
				}
				m_SendMessagePool.Delete(pSendMessage);
				mb->release();
				return false;
			}
		}
		else
		{
			OUR_DEBUG((LM_ERROR,"[CMessageService::PutMessage] mb new error.\n"));
			if(blDelete == true)
			{
				App_BuffPacketManager::instance()->Delete(pBuffPacket);
			}
			return false;
		}
	}

	return true;
}

bool CConnectManager::SetConnectName(uint32 u4ConnectID, const char* pName)
{
	mapConnectManager::iterator f = m_mapConnectManager.find(u4ConnectID);

	if(f != m_mapConnectManager.end())
	{
		CConnectHandler* pConnectHandler = (CConnectHandler* )f->second;
		if(NULL != pConnectHandler)
		{
			pConnectHandler->SetConnectName(pName);
			return true;
		}
		else
		{
			return false;
		}
	}	
	else
	{
		return false;
	}
}

bool CConnectManager::SetIsLog(uint32 u4ConnectID, bool blIsLog)
{
	mapConnectManager::iterator f = m_mapConnectManager.find(u4ConnectID);

	if(f != m_mapConnectManager.end())
	{
		CConnectHandler* pConnectHandler = (CConnectHandler* )f->second;
		if(NULL != pConnectHandler)
		{
			pConnectHandler->SetIsLog(blIsLog);
			return true;
		}
		else
		{
			return false;
		}
	}	
	else
	{
		return false;
	}	
}

void CConnectManager::GetClientNameInfo(const char* pName, vecClientNameInfo& objClientNameInfo)
{
	for(mapConnectManager::iterator b = m_mapConnectManager.begin(); b != m_mapConnectManager.end(); b++)
	{
		CConnectHandler* pConnectHandler = (CConnectHandler* )b->second;
		if(NULL != pConnectHandler && ACE_OS::strcmp(pConnectHandler->GetConnectName(), pName) == 0)
		{
			_ClientNameInfo ClientNameInfo;
			ClientNameInfo.m_nConnectID = (int)pConnectHandler->GetConnectID();
			sprintf_safe(ClientNameInfo.m_szName, MAX_BUFF_100, "%s", pConnectHandler->GetConnectName());
			sprintf_safe(ClientNameInfo.m_szClientIP, MAX_BUFF_20, "%s", pConnectHandler->GetClientIPInfo().m_szClientIP);
			ClientNameInfo.m_nPort =  pConnectHandler->GetClientIPInfo().m_nPort;
			if(pConnectHandler->GetIsLog() == true)
			{
				ClientNameInfo.m_nLog = 1;
			}
			else
			{
				ClientNameInfo.m_nLog = 0;
			}

			objClientNameInfo.push_back(ClientNameInfo);
		}
	}
}

//*********************************************************************************

CConnectHandlerPool::CConnectHandlerPool(void)
{
	//ConnectID��������1��ʼ
	m_u4CurrMaxCount = 1;
}

CConnectHandlerPool::~CConnectHandlerPool(void)
{
	OUR_DEBUG((LM_INFO, "[CConnectHandlerPool::~CConnectHandlerPool].\n"));
	Close();
	OUR_DEBUG((LM_INFO, "[CConnectHandlerPool::~CConnectHandlerPool]End.\n"));
}

void CConnectHandlerPool::Init(int nObjcetCount)
{
	Close();

	for(int i = 0; i < nObjcetCount; i++)
	{
		CConnectHandler* pPacket = new CConnectHandler();
		if(NULL != pPacket)
		{
			//���ӵ�Free map����
			mapHandle::iterator f = m_mapMessageFree.find(pPacket);
			if(f == m_mapMessageFree.end())
			{
				pPacket->Init(m_u4CurrMaxCount);
				m_mapMessageFree.insert(mapHandle::value_type(pPacket, pPacket));
				m_u4CurrMaxCount++;
			}
		}
	}
}

void CConnectHandlerPool::Close()
{
	//���������Ѵ��ڵ�ָ��
	for(mapHandle::iterator itorFreeB = m_mapMessageFree.begin(); itorFreeB != m_mapMessageFree.end(); itorFreeB++)
	{
		CConnectHandler* pObject = (CConnectHandler* )itorFreeB->second;
		SAFE_DELETE(pObject);
	}

	for(mapHandle::iterator itorUsedB = m_mapMessageUsed.begin(); itorUsedB != m_mapMessageUsed.end(); itorUsedB++)
	{
		CConnectHandler* pPacket = (CConnectHandler* )itorUsedB->second;
		SAFE_DELETE(pPacket);
	}

	m_u4CurrMaxCount = 0;
	m_mapMessageFree.clear();
	m_mapMessageUsed.clear();
}

int CConnectHandlerPool::GetUsedCount()
{
	ACE_Guard<ACE_Recursive_Thread_Mutex> WGuard(m_ThreadWriteLock);

	return (int)m_mapMessageUsed.size();
}

int CConnectHandlerPool::GetFreeCount()
{
	ACE_Guard<ACE_Recursive_Thread_Mutex> WGuard(m_ThreadWriteLock);

	return (int)m_mapMessageFree.size();
}

CConnectHandler* CConnectHandlerPool::Create()
{
	ACE_Guard<ACE_Recursive_Thread_Mutex> WGuard(m_ThreadWriteLock);

	//���free�����Ѿ�û���ˣ������ӵ�free���С�
	if(m_mapMessageFree.size() <= 0)
	{
		CConnectHandler* pPacket = new CConnectHandler();

		if(pPacket != NULL)
		{
			//���ӵ�Free map����
			mapHandle::iterator f = m_mapMessageFree.find(pPacket);
			if(f == m_mapMessageFree.end())
			{
				pPacket->Init(m_u4CurrMaxCount);
				m_mapMessageFree.insert(mapHandle::value_type(pPacket, pPacket));
				m_u4CurrMaxCount++;
			}
		}
		else
		{
			return NULL;
		}
	}

	//��free�����ó�һ��,���뵽used����
	mapHandle::iterator itorFreeB = m_mapMessageFree.begin();
	CConnectHandler* pPacket = (CConnectHandler* )itorFreeB->second;
	m_mapMessageFree.erase(itorFreeB);
	//���ӵ�used map����
	mapHandle::iterator f = m_mapMessageUsed.find(pPacket);
	if(f == m_mapMessageUsed.end())
	{
		m_mapMessageUsed.insert(mapHandle::value_type(pPacket, pPacket));
	}

	return (CConnectHandler* )pPacket;
}

bool CConnectHandlerPool::Delete(CConnectHandler* pObject)
{
	ACE_Guard<ACE_Recursive_Thread_Mutex> WGuard(m_ThreadWriteLock);

	if(NULL == pObject)
	{
		return false;
	}

	mapHandle::iterator f = m_mapMessageUsed.find(pObject);
	if(f != m_mapMessageUsed.end())
	{
		m_mapMessageUsed.erase(f);

		//���ӵ�Free map����
		mapHandle::iterator f = m_mapMessageFree.find(pObject);
		if(f == m_mapMessageFree.end())
		{
			m_mapMessageFree.insert(mapHandle::value_type(pObject, pObject));
		}
	}

	return true;
}

//==============================================================
CConnectManagerGroup::CConnectManagerGroup()
{
	m_u4CurrMaxCount     = 0;
	m_u2ThreadQueueCount = SENDQUEUECOUNT;
}

CConnectManagerGroup::~CConnectManagerGroup()
{
	OUR_DEBUG((LM_INFO, "[CConnectManagerGroup::~CConnectManagerGroup].\n"));

	for(mapConnectManager::iterator b = m_mapConnectManager.begin(); b != m_mapConnectManager.end();b++)
	{
		CConnectManager* pConnectManager = (CConnectManager* )b->second;
		SAFE_DELETE(pConnectManager);
	}

	m_mapConnectManager.clear();
}

void CConnectManagerGroup::Init(uint16 u2SendQueueCount)
{
	for(int i = 0; i < u2SendQueueCount; i++)
	{
		CConnectManager* pConnectManager = new CConnectManager();
		if(NULL != pConnectManager)	
		{
			//����map
			m_mapConnectManager.insert(mapConnectManager::value_type(i, pConnectManager));
			OUR_DEBUG((LM_INFO, "[CConnectManagerGroup::Init]Creat %d SendQueue OK.\n", i));
		}
	}

	m_u2ThreadQueueCount = (uint16)m_mapConnectManager.size();
}

uint32 CConnectManagerGroup::GetGroupIndex()
{
	//�������ӻ�����У��������������㷨��
	ACE_Guard<ACE_Recursive_Thread_Mutex> WGuard(m_ThreadWriteLock);
	m_u4CurrMaxCount++;
	return m_u4CurrMaxCount;
}

bool CConnectManagerGroup::AddConnect(CConnectHandler* pConnectHandler)
{
	ACE_Guard<ACE_Recursive_Thread_Mutex> WGrard(m_ThreadWriteLock);

	uint32 u4ConnectID = GetGroupIndex();

	//�ж����е���һ���߳�������ȥ
	uint16 u2ThreadIndex = u4ConnectID % m_u2ThreadQueueCount;

	mapConnectManager::iterator f = m_mapConnectManager.find(u2ThreadIndex);
	if(f == m_mapConnectManager.end())
	{
		OUR_DEBUG((LM_INFO, "[CConnectManagerGroup::AddConnect]Out of range Queue ID.\n"));
		return false;
	}

	CConnectManager* pConnectManager = (CConnectManager* )f->second;
	if(NULL == pConnectManager)
	{
		OUR_DEBUG((LM_INFO, "[CConnectManagerGroup::AddConnect]No find send Queue object.\n"));
		return false;		
	}

	//OUR_DEBUG((LM_INFO, "[CConnectManagerGroup::Init]u4ConnectID=%d, u2ThreadIndex=%d.\n", u4ConnectID, u2ThreadIndex));

	return pConnectManager->AddConnect(u4ConnectID, pConnectHandler);
}

bool CConnectManagerGroup::PostMessage(uint32 u4ConnectID, IBuffPacket* pBuffPacket, uint8 u1SendType, uint16 u2CommandID, bool blSendState, bool blDelete)
{
	//�ж����е���һ���߳�������ȥ
	uint16 u2ThreadIndex = u4ConnectID % m_u2ThreadQueueCount;

	mapConnectManager::iterator f = m_mapConnectManager.find(u2ThreadIndex);
	if(f == m_mapConnectManager.end())
	{
		OUR_DEBUG((LM_INFO, "[CConnectManagerGroup::PostMessage]Out of range Queue ID.\n"));
		return false;
	}

	CConnectManager* pConnectManager = (CConnectManager* )f->second;
	if(NULL == pConnectManager)
	{
		OUR_DEBUG((LM_INFO, "[CConnectManagerGroup::PostMessage]No find send Queue object.\n"));
		return false;		
	}

	//OUR_DEBUG((LM_INFO, "[CConnectManagerGroup::PostMessage]u4ConnectID=%d, u2ThreadIndex=%d.\n", u4ConnectID, u2ThreadIndex));

	return pConnectManager->PostMessage(u4ConnectID, pBuffPacket, u1SendType, u2CommandID, blSendState, blDelete);
}

bool CConnectManagerGroup::PostMessage( uint32 u4ConnectID, const char* pData, uint32 nDataLen, uint8 u1SendType, uint16 u2CommandID, bool blSendState, bool blDelete)
{
	//�ж����е���һ���߳�������ȥ
	uint16 u2ThreadIndex = u4ConnectID % m_u2ThreadQueueCount;

	mapConnectManager::iterator f = m_mapConnectManager.find(u2ThreadIndex);
	if(f == m_mapConnectManager.end())
	{
		OUR_DEBUG((LM_INFO, "[CConnectManagerGroup::PostMessage]Out of range Queue ID.\n"));

		if(blDelete == true)
		{
			SAFE_DELETE_ARRAY(pData);
		}

		return false;
	}

	CConnectManager* pConnectManager = (CConnectManager* )f->second;
	if(NULL == pConnectManager)
	{
		OUR_DEBUG((LM_INFO, "[CConnectManagerGroup::PostMessage]No find send Queue object.\n"));

		if(blDelete == true)
		{
			SAFE_DELETE_ARRAY(pData);
		}

		return false;		
	}

	//OUR_DEBUG((LM_INFO, "[CConnectManagerGroup::PostMessage]u4ConnectID=%d, u2ThreadIndex=%d.\n", u4ConnectID, u2ThreadIndex));
	IBuffPacket* pBuffPacket = App_BuffPacketManager::instance()->Create();
	if(NULL != pBuffPacket)
	{
		pBuffPacket->WriteStream(pData, nDataLen);

		if(blDelete == true)
		{
			SAFE_DELETE_ARRAY(pData);
		}

		return pConnectManager->PostMessage(u4ConnectID, pBuffPacket, u1SendType, u2CommandID, blSendState, true);
	} 
	else
	{
		OUR_DEBUG((LM_INFO, "[CConnectManagerGroup::PostMessage]pBuffPacket is NULL.\n"));

		if(blDelete == true)
		{
			SAFE_DELETE_ARRAY(pData);
		}

		return false;
	}
}

bool CConnectManagerGroup::PostMessage( vector<uint32> vecConnectID, IBuffPacket* pBuffPacket, uint8 u1SendType, uint16 u2CommandID, bool blSendState, bool blDelete)
{
	uint32 u4ConnectID = 0;
	for(uint32 i = 0; i < (uint32)vecConnectID.size(); i++)
	{
		//�ж����е���һ���߳�������ȥ
		uint16 u2ThreadIndex = u4ConnectID % m_u2ThreadQueueCount;

		mapConnectManager::iterator f = m_mapConnectManager.find(u2ThreadIndex);
		if(f == m_mapConnectManager.end())
		{
			OUR_DEBUG((LM_INFO, "[CConnectManagerGroup::PostMessage]Out of range Queue ID.\n"));
			return false;
		}

		CConnectManager* pConnectManager = (CConnectManager* )f->second;
		if(NULL == pConnectManager)
		{
			OUR_DEBUG((LM_INFO, "[CConnectManagerGroup::PostMessage]No find send Queue object.\n"));
			return false;		
		}

		pConnectManager->PostMessage(u4ConnectID, pBuffPacket, u1SendType, u2CommandID, blSendState, blDelete);
	}

	return true;
}

bool CConnectManagerGroup::PostMessage( vector<uint32> vecConnectID, const char* pData, uint32 nDataLen, uint8 u1SendType, uint16 u2CommandID, bool blSendState, bool blDelete)
{
	uint32 u4ConnectID = 0;
	for(uint32 i = 0; i < (uint32)vecConnectID.size(); i++)
	{
		//�ж����е���һ���߳�������ȥ
		uint16 u2ThreadIndex = u4ConnectID % m_u2ThreadQueueCount;

		mapConnectManager::iterator f = m_mapConnectManager.find(u2ThreadIndex);
		if(f == m_mapConnectManager.end())
		{
			OUR_DEBUG((LM_INFO, "[CConnectManagerGroup::PostMessage]Out of range Queue ID.\n"));

			if(blDelete == true)
			{
				SAFE_DELETE_ARRAY(pData);
			}

			return false;
		}

		CConnectManager* pConnectManager = (CConnectManager* )f->second;
		if(NULL == pConnectManager)
		{
			OUR_DEBUG((LM_INFO, "[CConnectManagerGroup::PostMessage]No find send Queue object.\n"));

			if(blDelete == true)
			{
				SAFE_DELETE_ARRAY(pData);
			}

			return false;		
		}

		//OUR_DEBUG((LM_INFO, "[CConnectManagerGroup::PostMessage]u4ConnectID=%d, u2ThreadIndex=%d.\n", u4ConnectID, u2ThreadIndex));
		IBuffPacket* pBuffPacket = App_BuffPacketManager::instance()->Create();
		if(NULL != pBuffPacket)
		{
			return pBuffPacket->WriteStream(pData, nDataLen);

			if(blDelete == true)
			{
				SAFE_DELETE_ARRAY(pData);
			}

			return pConnectManager->PostMessage(u4ConnectID, pBuffPacket, u1SendType, u2CommandID, blSendState, true);
		} 
		else
		{
			OUR_DEBUG((LM_INFO, "[CConnectManagerGroup::PostMessage]pBuffPacket is NULL.\n"));

			if(blDelete == true)
			{
				SAFE_DELETE_ARRAY(pData);
			}

			return false;
		}
	}

	return false;
}

bool CConnectManagerGroup::CloseConnect(uint32 u4ConnectID)
{
	//�ж����е���һ���߳�������ȥ
	uint16 u2ThreadIndex = u4ConnectID % m_u2ThreadQueueCount;

	mapConnectManager::iterator f = m_mapConnectManager.find(u2ThreadIndex);
	if(f == m_mapConnectManager.end())
	{
		OUR_DEBUG((LM_INFO, "[CConnectManagerGroup::CloseConnect]Out of range Queue ID.\n"));
		return false;
	}

	CConnectManager* pConnectManager = (CConnectManager* )f->second;
	if(NULL == pConnectManager)
	{
		OUR_DEBUG((LM_INFO, "[CConnectManagerGroup::CloseConnect]No find send Queue object.\n"));
		return false;		
	}	

	return pConnectManager->CloseConnect(u4ConnectID);
}

_ClientIPInfo CConnectManagerGroup::GetClientIPInfo(uint32 u4ConnectID)
{
	_ClientIPInfo objClientIPInfo;
	//�ж����е���һ���߳�������ȥ
	uint16 u2ThreadIndex = u4ConnectID % m_u2ThreadQueueCount;

	mapConnectManager::iterator f = m_mapConnectManager.find(u2ThreadIndex);
	if(f == m_mapConnectManager.end())
	{
		OUR_DEBUG((LM_INFO, "[CConnectManagerGroup::GetClientIPInfo]Out of range Queue ID.\n"));
		return objClientIPInfo;
	}

	CConnectManager* pConnectManager = (CConnectManager* )f->second;
	if(NULL == pConnectManager)
	{
		OUR_DEBUG((LM_INFO, "[CConnectManagerGroup::GetClientIPInfo]No find send Queue object.\n"));
		return objClientIPInfo;		
	}	

	return pConnectManager->GetClientIPInfo(u4ConnectID);	
}

void CConnectManagerGroup::GetConnectInfo(vecClientConnectInfo& VecClientConnectInfo)
{
	for(mapConnectManager::iterator b = m_mapConnectManager.begin(); b != m_mapConnectManager.end(); b++)
	{
		CConnectManager* pConnectManager = (CConnectManager* )b->second;
		if(NULL != pConnectManager)
		{
			pConnectManager->GetConnectInfo(VecClientConnectInfo);
		}
	}
}

int CConnectManagerGroup::GetCount()
{
	uint32 u4Count = 0;
	for(mapConnectManager::iterator b = m_mapConnectManager.begin(); b != m_mapConnectManager.end(); b++)
	{
		CConnectManager* pConnectManager = (CConnectManager* )b->second;
		if(NULL != pConnectManager)
		{
			u4Count += pConnectManager->GetCount();
		}
	}

	return u4Count;
}

void CConnectManagerGroup::CloseAll()
{
	for(mapConnectManager::iterator b = m_mapConnectManager.begin(); b != m_mapConnectManager.end(); b++)
	{
		CConnectManager* pConnectManager = (CConnectManager* )b->second;
		if(NULL != pConnectManager)
		{
			pConnectManager->CloseAll();
		}
	}	
}

bool CConnectManagerGroup::StartTimer()
{
	for(mapConnectManager::iterator b = m_mapConnectManager.begin(); b != m_mapConnectManager.end();b++)
	{
		CConnectManager* pConnectManager = (CConnectManager* )b->second;
		if(NULL != pConnectManager)
		{
			pConnectManager->StartTimer();
		}
	}

	return true;	
}

bool CConnectManagerGroup::Close(uint32 u4ConnectID)
{
	//�ж����е���һ���߳�������ȥ
	uint16 u2ThreadIndex = u4ConnectID % m_u2ThreadQueueCount;

	mapConnectManager::iterator f = m_mapConnectManager.find(u2ThreadIndex);
	if(f == m_mapConnectManager.end())
	{
		OUR_DEBUG((LM_INFO, "[CConnectManagerGroup::GetClientIPInfo]Out of range Queue ID.\n"));
		return false;
	}

	CConnectManager* pConnectManager = (CConnectManager* )f->second;
	if(NULL == pConnectManager)
	{
		OUR_DEBUG((LM_INFO, "[CConnectManagerGroup::GetClientIPInfo]No find send Queue object.\n"));
		return false;		
	}	

	return pConnectManager->Close(u4ConnectID);
}

const char* CConnectManagerGroup::GetError()
{
	return (char* )"";
}

void CConnectManagerGroup::SetRecvQueueTimeCost(uint32 u4ConnectID, uint32 u4TimeCost)
{
	//�ж����е���һ���߳�������ȥ
	uint16 u2ThreadIndex = u4ConnectID % m_u2ThreadQueueCount;

	mapConnectManager::iterator f = m_mapConnectManager.find(u2ThreadIndex);
	if(f == m_mapConnectManager.end())
	{
		OUR_DEBUG((LM_INFO, "[CConnectManagerGroup::GetClientIPInfo]Out of range Queue ID.\n"));
		return;
	}

	CConnectManager* pConnectManager = (CConnectManager* )f->second;
	if(NULL == pConnectManager)
	{
		OUR_DEBUG((LM_INFO, "[CConnectManagerGroup::GetClientIPInfo]No find send Queue object.\n"));
		return;		
	}		

	pConnectManager->SetRecvQueueTimeCost(u4ConnectID, u4TimeCost);
}

bool CConnectManagerGroup::PostMessageAll( IBuffPacket* pBuffPacket, uint8 u1SendType, uint16 u2CommandID, bool blSendState, bool blDelete)
{
	//ȫ��Ⱥ��
	for(mapConnectManager::iterator b = m_mapConnectManager.begin(); b != m_mapConnectManager.end(); b++)
	{
		CConnectManager* pConnectManager = (CConnectManager* )b->second;
		if(NULL == pConnectManager)
		{
			OUR_DEBUG((LM_INFO, "[CConnectManagerGroup::PostMessage]No find send Queue object.\n"));
			return false;		
		}

		pConnectManager->PostMessageAll(pBuffPacket, u1SendType, u2CommandID, blSendState, blDelete);
	}

	//�����˾�ɾ��
	App_BuffPacketManager::instance()->Delete(pBuffPacket);

	return true;
}

bool CConnectManagerGroup::PostMessageAll( const char* pData, uint32 nDataLen, uint8 u1SendType, uint16 u2CommandID, bool blSendState, bool blDelete)
{
	IBuffPacket* pBuffPacket = App_BuffPacketManager::instance()->Create();
	if(NULL == pBuffPacket)
	{
		OUR_DEBUG((LM_INFO, "[CConnectManagerGroup::PostMessageAll]pBuffPacket is NULL.\n"));

		if(blDelete == true)
		{
			SAFE_DELETE_ARRAY(pData);
		}

		return false;
	}
	else
	{
		pBuffPacket->WriteStream(pData, nDataLen);

		if(blDelete == true)
		{
			SAFE_DELETE_ARRAY(pData);
		}
	}

	//ȫ��Ⱥ��
	for(mapConnectManager::iterator b = m_mapConnectManager.begin(); b != m_mapConnectManager.end(); b++)
	{
		CConnectManager* pConnectManager = (CConnectManager* )b->second;
		if(NULL == pConnectManager)
		{
			OUR_DEBUG((LM_INFO, "[CConnectManagerGroup::PostMessage]No find send Queue object.\n"));
			return false;		
		}

		pConnectManager->PostMessageAll(pBuffPacket, u1SendType, u2CommandID, blSendState, true);
	}

	//�����˾�ɾ��
	App_BuffPacketManager::instance()->Delete(pBuffPacket);

	return true;
}

bool CConnectManagerGroup::SetConnectName(uint32 u4ConnectID, const char* pName)
{
	//�ж����е���һ���߳�������ȥ
	uint16 u2ThreadIndex = u4ConnectID % m_u2ThreadQueueCount;

	mapConnectManager::iterator f = m_mapConnectManager.find(u2ThreadIndex);
	if(f == m_mapConnectManager.end())
	{
		OUR_DEBUG((LM_INFO, "[CConnectManagerGroup::CloseConnect]Out of range Queue ID.\n"));
		return false;
	}

	CConnectManager* pConnectManager = (CConnectManager* )f->second;
	if(NULL == pConnectManager)
	{
		OUR_DEBUG((LM_INFO, "[CConnectManagerGroup::CloseConnect]No find send Queue object.\n"));
		return false;		
	}	

	return pConnectManager->SetConnectName(u4ConnectID, pName);	
}

bool CConnectManagerGroup::SetIsLog(uint32 u4ConnectID, bool blIsLog)
{
	//�ж����е���һ���߳�������ȥ
	uint16 u2ThreadIndex = u4ConnectID % m_u2ThreadQueueCount;

	mapConnectManager::iterator f = m_mapConnectManager.find(u2ThreadIndex);
	if(f == m_mapConnectManager.end())
	{
		OUR_DEBUG((LM_INFO, "[CConnectManagerGroup::CloseConnect]Out of range Queue ID.\n"));
		return false;
	}

	CConnectManager* pConnectManager = (CConnectManager* )f->second;
	if(NULL == pConnectManager)
	{
		OUR_DEBUG((LM_INFO, "[CConnectManagerGroup::CloseConnect]No find send Queue object.\n"));
		return false;		
	}	

	return pConnectManager->SetIsLog(u4ConnectID, blIsLog);		
}

void CConnectManagerGroup::GetClientNameInfo(const char* pName, vecClientNameInfo& objClientNameInfo)
{
	objClientNameInfo.clear();
	//ȫ������
	for(mapConnectManager::iterator b = m_mapConnectManager.begin(); b != m_mapConnectManager.end(); b++)
	{
		CConnectManager* pConnectManager = (CConnectManager* )b->second;
		if(NULL != pConnectManager)
		{
			pConnectManager->GetClientNameInfo(pName, objClientNameInfo);
		}
	}	
}