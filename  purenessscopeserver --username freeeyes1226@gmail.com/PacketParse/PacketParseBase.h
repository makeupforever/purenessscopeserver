#ifndef _PACKETPARSEBASE_H
#define _PACKETPARSEBASE_H

//������ǰѲ������仯�ĺ������������ֻ�ÿ�����ȥʵ�ֱ�Ҫ��5���ӿ�
//ʣ�µģ������߲���Ҫ��ϵ�����ǿ���Լ�ʵ�ֵ����顣
//���԰������������࣬����������ࡣ
//���ÿһ��εĽ���������������ȥ����������롣
//add by freeeyes

#include "BuffPacket.h"
#include "IMessageBlockManager.h"

#ifdef WIN32
#if defined PACKETPARSE_BUILD_DLL
#define DLL_EXPORT __declspec(dllexport)
#else
#define DLL_EXPORT __declspec(dllimport)
#endif
#else
#define DLL_EXPORT
#endif 

#ifdef WIN32
class DLL_EXPORT CPacketParseBase
#else
class CPacketParseBase
#endif 
{
public:
	CPacketParseBase(void);

	virtual ~CPacketParseBase(void);

	void Init();

	void Clear();

	void Close();

	const char* GetPacketVersion();
	uint8 GetPacketMode();
	uint32 GetPacketHeadLen();
	uint32 GetPacketBodyLen();

	uint16 GetPacketCommandID();
	bool GetIsHead();
	uint32 GetPacketHeadSrcLen();
	uint32 GetPacketBodySrcLen();

	ACE_Message_Block* GetMessageHead();
	ACE_Message_Block* GetMessageBody();

	virtual bool SetPacketHead(ACE_Message_Block* pmbHead, IMessageBlockManager* pMessageBlockManager) = 0;
	virtual bool SetPacketBody(ACE_Message_Block* pmbBody, IMessageBlockManager* pMessageBlockManager) = 0;
	virtual uint8 GetPacketStream(ACE_Message_Block* pCurrMessage, IMessageBlockManager* pMessageBlockManager) = 0;
	virtual bool MakePacket(const char* pData, uint32 u4Len, ACE_Message_Block* pMbData) = 0;
	virtual uint32 MakePacketLength(uint32 u4DataLen)                                    = 0;

protected:
	uint32 m_u4PacketHead;               //��ͷ�ĳ���
	uint32 m_u4PacketData;               //����ĳ���
	uint32 m_u4HeadSrcSize;              //��ͷ��ԭʼ���� 
	uint32 m_u4BodySrcSize;              //�����ԭʼ����
	uint16 m_u2PacketCommandID;          //������
	bool   m_blIsHead;
	char   m_szPacketVersion[MAX_BUFF_20];   //���������汾
	uint8  m_u1PacketMode;                   //������ģʽ    

	ACE_Message_Block* m_pmbHead;   //��ͷ����
	ACE_Message_Block* m_pmbBody;   //���岿��

	CBuffPacket m_objCurrBody;      //��¼��δ�����İ���
};

#endif