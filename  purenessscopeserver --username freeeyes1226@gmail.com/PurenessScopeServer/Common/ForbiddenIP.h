#ifndef _FORBIDDEN_H
#define _FORBIDDEN_H

//�ܾò������������д�����ˣ���ʵ��һ�����������һ�ּ�֡�����ֻʣ����һ���ˡ�
//�������ã���ʵ����������ң����������ڴ���Ҳ�����ڷ���������һ�ֱ�Ȼ��
//�ǵģ�����������������ô�򵥡�û��ʲô�������Ҽ�֣�Ҳ������ʲô�������ҷ�����
//���ǣ�������ǰ�У����汾Ȼ�ɡ�
//add by freeeyes
//2011-09-01

#include "AppConfig.h"

#include "ace/Singleton.h"

#define MAX_IP_SIZE 20

struct _ForbiddenIP
{
	char           m_szClientIP[MAX_IP_SIZE];   //����ֹ��IP
	uint32		   m_u4Pattern;					//IP��ģʽ
	union//IPV4��ַ
	{
		uint32 m_u4ClientIp;//IPV4 ��ַ
		uint8  m_u1ClientIpSection[4];//IPV4��ַ��4������
	};
	uint8          m_u1Type;                    //��ֹ�����ͣ�0Ϊ���ý�ֹ��1Ϊʱ�ν�ֹ��
	ACE_Time_Value m_tvBegin;                   //ʱ�ν�ֹ��ʼʱ��
	uint32         m_u4Second;                  //��ֹ������
	uint8          m_u1ConnectType;             //���ӵ����ͣ�0ΪTCP��1ΪUDP  

	_ForbiddenIP()
	{
		m_szClientIP[0] = '\0';
		m_u1Type        = 0;
		m_u4Second      = 0;
		m_u1ConnectType = CONNECT_TCP;   //Ĭ��ΪTCP��
		m_u4Pattern = 0xFFFFFFFF;
		m_u4ClientIp = 0;
	}
};

typedef vector<_ForbiddenIP> VecForbiddenIP;

class CForbiddenIP
{
public:
	CForbiddenIP();
	~CForbiddenIP();

	bool Init(const char* szConfigPath);                                                    //��ʼ���������÷�ͣIP�ļ�
	bool CheckIP(uint32 u4ClientIp, uint8 u1ConnectType = CONNECT_TCP);                       //���IP�Ƿ�������� 
	bool AddForeverIP(const char* pIP, uint8 u1ConnectType = CONNECT_TCP);                  //�������÷�ͣ��IP 
	bool AddTempIP(const char* pIP, uint32 u4Second, uint8 u1ConnectType = CONNECT_TCP);    //������ʱ��ͣ��IP
	bool DelForeverIP(const char* pIP, uint8 u1ConnectType = CONNECT_TCP);                  //ɾ�����÷�ͣIP
	bool DelTempIP(const char* pIP, uint8 u1ConnectType = CONNECT_TCP);                     //ɾ����ʱ��ͣIP
	VecForbiddenIP* ShowForeverIP() const;                                                  //��ʾ���÷�ͣIP
	VecForbiddenIP* ShowTemoIP() const;                                                     //��ʾ��ʱ��ͣIP

private:
	bool ParseIp(const char* pszIp, _ForbiddenIP& ipadr);
	bool LoadListCommon(LPCTSTR pszFileName, VecForbiddenIP& iplist);

	bool SaveConfig();                                      //�洢�����ļ�
private:
	CAppConfig     m_AppConfig;
	VecForbiddenIP m_VecForeverForbiddenIP;           //���÷�ͣ��IP�б�
	VecForbiddenIP m_VecTempForbiddenIP;              //��ʱ��ͣ��IP�б�
};

typedef ACE_Singleton<CForbiddenIP, ACE_Null_Mutex> App_ForbiddenIP;

#endif