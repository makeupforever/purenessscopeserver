#include "StdAfx.h"
#include "ClientTcpSocket.h"

CClientTcpSocket::CClientTcpSocket(void)
{
  m_pSocket_Info       = NULL;
  m_pSocket_State_Info = NULL;
  m_blRun              = false;
}

CClientTcpSocket::~CClientTcpSocket(void)
{
  Close();
}

void CClientTcpSocket::Close()
{
  if(NULL != m_pSocket_Info)
  {
    delete m_pSocket_Info;
    m_pSocket_Info = NULL;
  }

  if(NULL != m_pSocket_State_Info)
  {
    delete m_pSocket_State_Info;
    m_pSocket_State_Info = NULL;
  }
}

void CClientTcpSocket::SetSocketThread( _Socket_Info* pSocket_Info, _Socket_State_Info* pSocket_State_Info )
{
  Close();

  m_pSocket_Info       = pSocket_Info;
  m_pSocket_State_Info = pSocket_State_Info;
}

_Socket_State_Info* CClientTcpSocket::GetStateInfo()
{
  return m_pSocket_State_Info;
}

void CClientTcpSocket::Stop()
{
  m_blRun = false;
}

void CClientTcpSocket::Run()
{

  m_blRun = true;
  SOCKET sckClient;

  if(m_pSocket_Info == NULL || m_pSocket_State_Info == NULL)
  {
    m_blRun = false;
    return;
  }

  //�����Ƿ��ǳ����ӣ�����ǳ����ӣ���ֻ����һ�Ρ�
  bool blIsConnect = false;

  //socket������׼������
  struct sockaddr_in sockaddr;
  
  memset(&sockaddr, 0, sizeof(sockaddr));
  sockaddr.sin_family = AF_INET;
  sockaddr.sin_port   = htons(m_pSocket_Info->m_nPort);
  sockaddr.sin_addr.S_un.S_addr = inet_addr(m_pSocket_Info->m_szSerevrIP);

  while(m_blRun)
  {
    if(blIsConnect == false)
    {
      sckClient = socket(AF_INET, SOCK_STREAM, 0);

      DWORD TimeOut = (DWORD)m_pSocket_Info->m_nRecvTimeout;
      ::setsockopt(sckClient, SOL_SOCKET, SO_RCVTIMEO, (char *)&TimeOut, sizeof(TimeOut));

      DWORD dwSleepTime = (DWORD)m_pSocket_Info->m_nDelaySecond;
      if(m_pSocket_Info->m_blIsRadomaDelay == true)
      {
        //���������ģ����1-1000֮�����һ��ʱ��
        dwSleepTime = (DWORD)RandomValue(1, 1000);
      }

      //����ָ����ʱ��
      Sleep(dwSleepTime);

      //����Զ�̷�����
      int nErr = connect(sckClient, (SOCKADDR*)&sockaddr, sizeof(SOCKADDR));
      if(0 != nErr)
      {
        m_pSocket_State_Info->m_nFailConnect++;
        m_pSocket_State_Info->m_nCurrectSocket = 0;
      }
      else
      {
        //�ɹ�����
        m_pSocket_State_Info->m_nSuccessConnect++;
        m_pSocket_State_Info->m_nCurrectSocket = 1;
        blIsConnect = true;
      }
    }

    if(blIsConnect == true)
    {
      //��������
      char szSendBuffData[MAX_BUFF_1024 * 100] = {'\0'};
      char szRecvBuffData[MAX_BUFF_1024 * 100] = {'\0'};

      char* pSendData   = NULL;
      int nSendLen      = 0;
      int nTotalRecvLen = 0;
      //�������Ϊ������ݰ�
      if(m_pSocket_Info->m_blIsSendCount == true)
      {
        int nSendCount = RandomValue(1, 10);
        for(int i = 0; i < nSendCount; i++)
        {
          memcpy(&szSendBuffData[i * m_pSocket_Info->m_nSendLength], m_pSocket_Info->m_pSendBuff, m_pSocket_Info->m_nSendLength);
        }

        //��������
        pSendData     = (char* )szSendBuffData;
        nSendLen      = m_pSocket_Info->m_nSendLength * nSendCount;
        nTotalRecvLen = m_pSocket_Info->m_nRecvLength * nSendCount;
      }
      else
      {
        //��������
        pSendData     = (char* )m_pSocket_Info->m_pSendBuff;
        nSendLen      = m_pSocket_Info->m_nSendLength;
        nTotalRecvLen = m_pSocket_Info->m_nRecvLength;
      }

      int nTotalSendLen = nSendLen;
      int nBeginSend    = 0;
      int nCurrSendLen  = 0;
      bool blSendFlag   = false;
      int nBeginRecv    = 0;
      int nCurrRecvLen  = 0;
      bool blRecvFlag   = false;
      while(true)
      {
        nCurrSendLen = send(sckClient, pSendData + nBeginSend, nTotalSendLen, 0);
        if(nCurrSendLen <= 0)
        {
          m_pSocket_State_Info->m_nFailSend++;
          closesocket(sckClient);
          m_pSocket_State_Info->m_nCurrectSocket = 0;
          blIsConnect = false;
          break;
        }
        else
        {
          nTotalSendLen -= nCurrSendLen;
          if(nTotalSendLen == 0)
          {
            //�������
            m_pSocket_State_Info->m_nSuccessSend++;
            blSendFlag = true;
            break;
          }
          else
          {
            nBeginSend += nCurrSendLen;
          }
        }
      }

      //��������
      if(blSendFlag == true)
      {
        while(true)
        {
          //������ͳɹ��ˣ�������������
          nCurrRecvLen = recv(sckClient, (char* )szRecvBuffData + nBeginRecv, nTotalRecvLen, 0);
          if(nCurrRecvLen <= 0)
          {
            m_pSocket_State_Info->m_nFailSend++;
            closesocket(sckClient);
            m_pSocket_State_Info->m_nCurrectSocket = 0;
            blIsConnect = false;
            break;
          }
          else
          {
            nTotalRecvLen -= nCurrRecvLen;
            if(nTotalRecvLen == 0)
            {
              //�������
              m_pSocket_State_Info->m_nSuccessRecv++;
              blRecvFlag = true;
              break;
            }
            else
            {
              nBeginRecv += nCurrRecvLen;
            }
          }
        }
      }

      //����ǳ����ӣ��򲻹ر�����
      if(m_pSocket_Info->m_blIsAlwayConnect == false)
      {
        closesocket(sckClient);
        m_pSocket_State_Info->m_nCurrectSocket = 0;
        blIsConnect = false;
      }
    }
  }

  //�������û�ϣ���Ͽ�
  if(blIsConnect == true)
  {
    closesocket(sckClient);
    m_pSocket_State_Info->m_nCurrectSocket = 0;
    blIsConnect = false;
  }
}