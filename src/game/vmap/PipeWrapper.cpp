#include "PipeWrapper.h"
#include "ByteBuffer.h"

#include <ace/SPIPE_Acceptor.h>
#include <ace/SPIPE_Connector.h>
#include <ace/SPIPE_Addr.h>

#define ERROR_CONNECT_NO_PIPE   2

#define ERROR_EOF_ON_PIPE       109
#define ERROR_MORE_DATA_IN_PIPE 234

#define sLog Logger::Instance()

namespace VMAP
{
    Logger Logger::logger = Logger();

    PipeWrapper::~PipeWrapper()
    {
        if(m_stream)
            delete m_stream;
    }

    void SynchronizedSendPipeWrapper::Connect(const char* name, int32 id)
    {
        Guard g(m_lock);
        if(!g.locked())
            sLog.outError("Connect: failed to aquire lock");

        SendPipeWrapper::Connect(name, id);
    }

    void SendPipeWrapper::Connect(const char* name, int32 id)
    {
        if(m_connected)
            return;

        ACE_SPIPE_Addr addr;
        if (id >= 0)
        {
            char addr_buf[50];
            sprintf(addr_buf, "%s_%u", name, id);
            addr.set(addr_buf);
        } 
        else
            addr.set(name);

        ACE_SPIPE_Connector connetor = ACE_SPIPE_Connector();


        m_stream = new ACE_SPIPE_Stream();

        // block until connected
        while(true)
        {
            if (connetor.connect(*m_stream, addr) == -1)
            {
                if(ACE_OS::last_error() != ERROR_CONNECT_NO_PIPE)
                {
                    sLog.outError("Connect: failed to connect to stream %s because of error %d", addr.get_path_name(), ACE_OS::last_error());
                    return;
                }
            }
            else
            {
                m_connected = true;
                return;
            }
            ACE_Thread::yield();
        }
    }

    void SynchronizedRecvPipeWrapper::Accept(const char* name, int32 id)
    {
        Guard g(m_lock);
        if(!g.locked())
            sLog.outError("Accept: failed to aquire log");

        RecvPipeWrapper::Accept(name, id);    
    }

    void RecvPipeWrapper::Accept(const char* name, int32 id)
    {
        if(m_connected)
            return;

        ACE_SPIPE_Addr addr;
        if (id >= 0)
        {
            char addr_buf[50];
            sprintf(addr_buf, "%s_%u", name, id);
            addr.set(addr_buf);
        } 
        else
            addr.set(name);

        m_stream = new ACE_SPIPE_Stream();

        ACE_SPIPE_Acceptor acceptor = ACE_SPIPE_Acceptor(addr);
        if(acceptor.accept(*m_stream) == -1)
        {
            sLog.outError("Accept: failed to accept on stream %s becaus of error %d", addr.get_path_name(), ACE_OS::last_error());
            delete m_stream;
            m_stream = 0;
        }
        else
            m_connected = true;
    }

    ByteBuffer SynchronizedRecvPipeWrapper::RecvPacket()
    {
        Guard g(m_lock);
        if(!g.locked())
        {
            ByteBuffer packet;
            sLog.outError("RecvPacket: failed to aquire lock");
            m_eof = true;
            return packet;
        }
        printf("SynchronizedRecvPipeWrapper::RecvPacket() into recv\n");
        return RecvPipeWrapper::RecvPacket();
    }

    bool RecvPipeWrapper::recv(ByteBuffer &packet, uint32 size)
    {
        int n;
        while(true)
        {
            n = m_stream->recv_n(m_buffer, size);
            if (n < 0) 
            {
                int code = ACE_OS::last_error();
                if(code == ERROR_EOF_ON_PIPE)
                {
                    m_eof = true;
                    return false;
                } 
                else if(code == ERROR_MORE_DATA_IN_PIPE) 
                {
                    // ignore error
                }
                else 
                {
                    sLog.outError("recv: failed to recv data from stream because of error %d", code);
                    m_eof = true;
                    return false;
                }
            } 
            else if(n == 0)
            {
                ACE_Thread::yield();
                printf("[DEBUG]Recv_n n == 0\n");
                continue;
            }
            break;
        }

        for(uint32 i = 0; i < size; i++)
            packet << m_buffer[i];

        return true;
    }

    ByteBuffer RecvPipeWrapper::RecvPacket()
    {
        ByteBuffer packet;
        uint8 size;

        if(!recv(packet, 1))
            return packet;

        size = m_buffer[0];

        if (size > 1)
            if(!recv(packet, size - 1))
                return packet;
        return packet;
    }

    void SynchronizedSendPipeWrapper::SendPacket(ByteBuffer &packet)
    {
        Guard g(m_lock);
        if(!g.locked())
            sLog.outError("SendPacket: failed to aquire lock, unintended bahaviour possible");

        return SendPipeWrapper::SendPacket(packet);
    }

    void SendPipeWrapper::SendPacket(ByteBuffer &packet)
    {
        uint32 len = packet.size();
        uint8 *buf = new uint8[len];
        packet.read(buf, len);
        packet.rpos(0);

        uint32 offset = 0;
        while(offset < len)
            offset += m_stream->send(buf + offset, len - offset);

        delete [] buf;
    }

}