//////////////////////////////////////////////////////////////////////////
// TunnelConnection.cpp
//
// Implementation of the NetworkConnection interface that actually 
// tunnels all incoming and outgoing traffic through a proxy
//
// Copyright (C) 2014 Microsoft Corp.  All Rights Reserved
//////////////////////////////////////////////////////////////////////////

#include "stdafx.h"
#include "TunnelConnection.h"
#include "NetworkOutMessageImpl.h"
#include "NetworkInMessageImpl.h"
#include <random>

XTOOLS_NAMESPACE_BEGIN


TunnelConnection::TunnelConnection(const NetworkConnectionPtr& connection)
: m_netConnection(connection)
, m_remoteSystemConnected(false)
, m_messageBuffer(new byte[kDefaultMessageBufferSize])
, m_messageBufferSize(kDefaultMessageBufferSize)
{
	m_netConnection->AddListener(MessageID::Tunnel, this);
	m_netConnection->AddListener(MessageID::TunnelControl, this);

	std::random_device randomGenerator;
	m_connectionGUID = ((uint64)randomGenerator() << 32) + (uint64)randomGenerator();
}


TunnelConnection::~TunnelConnection()
{
	
}


ConnectionGUID TunnelConnection::GetConnectionGUID() const
{
	return m_connectionGUID;
}


bool TunnelConnection::IsConnected() const
{
	return m_netConnection->IsConnected() && m_remoteSystemConnected;
}


void TunnelConnection::Send(const NetworkOutMessagePtr& msg, MessagePriority priority, MessageReliability reliability, MessageChannel channel, bool releaseMessage)
{
	if (IsConnected())
	{
		// Append a tunnel header to the outgoing message

		const uint32 msgSize = msg->GetSize();

		const uint32 sendPacketSize = msgSize + sizeof(NetworkHeader);
		if (m_messageBufferSize < sendPacketSize)
		{
			m_messageBuffer = new byte[sendPacketSize];
		}

		// Write a header onto the front of the buffer
		NetworkHeader* header = reinterpret_cast<NetworkHeader*>(m_messageBuffer.get());
		header->m_messageID = MessageID::Tunnel;
		header->m_priority = priority;
		header->m_reliability = reliability;
		header->m_channel = channel;

		// Write the message onto the rest of the buffer
		byte* payload = m_messageBuffer.get() + sizeof(NetworkHeader);
		memcpy(payload, msg->GetData(), msgSize);

		// Send the constructed packet
		m_netConnection->GetSocket()->Send(m_messageBuffer.get(), sendPacketSize, priority, reliability, channel);
	}
	else
	{
		LogError("Trying to send a message to a remote host that is not connected");
	}

	if (releaseMessage)
	{
		m_netConnection->ReturnMessage(msg);
	}
}


void TunnelConnection::SendTo(const UserPtr& user, ClientRole deviceRole, const NetworkOutMessagePtr& msg, MessagePriority priority, MessageReliability reliability, MessageChannel channel, bool releaseMessage)
{
	if (IsConnected())
	{
		// Append a tunnel header AND a sendto header to the outgoing message

		const uint32 msgSize = msg->GetSize();
		const uint32 totalHeaderSize = sizeof(NetworkHeader) + sizeof(SendToNetworkHeader);

		const uint32 sendPacketSize = msgSize + totalHeaderSize;
		if (m_messageBufferSize < sendPacketSize)
		{
			m_messageBuffer = new byte[sendPacketSize];
		}

		// Write a header onto the front of the buffer
		NetworkHeader* tunnelHeader = reinterpret_cast<NetworkHeader*>(m_messageBuffer.get());
		tunnelHeader->m_messageID = MessageID::Tunnel;
		tunnelHeader->m_priority = priority;
		tunnelHeader->m_reliability = reliability;
		tunnelHeader->m_channel = channel;

		SendToNetworkHeader* sendToHeader = reinterpret_cast<SendToNetworkHeader*>(m_messageBuffer.get() + sizeof(NetworkHeader));
		sendToHeader->m_messageID = MessageID::SendTo;
		sendToHeader->m_priority = priority;
		sendToHeader->m_reliability = reliability;
		sendToHeader->m_channel = channel;
		sendToHeader->m_userID = user->GetID();
		sendToHeader->m_deviceRole = deviceRole;

		// Write the message onto the rest of the buffer
		byte* payload = m_messageBuffer.get() + totalHeaderSize;
		memcpy(payload, msg->GetData(), msgSize);

		// Send the constructed packet
		m_netConnection->GetSocket()->Send(m_messageBuffer.get(), sendPacketSize, priority, reliability, channel);
	}
	else
	{
		LogError("Trying to send a message to a remote host that is not connected");
	}

	if (releaseMessage)
	{
		m_netConnection->ReturnMessage(msg);
	}
}


void TunnelConnection::Broadcast(const NetworkOutMessagePtr& msg, MessagePriority priority, MessageReliability reliability, MessageChannel channel, bool releaseMessage)
{
	if (IsConnected())
	{
		// Append a tunnel header to the outgoing message

		const uint32 msgSize = msg->GetSize();
		const uint32 totalHeaderSize = sizeof(NetworkHeader) * 2;

		const uint32 sendPacketSize = msgSize + totalHeaderSize;
		if (m_messageBufferSize < sendPacketSize)
		{
			m_messageBuffer = new byte[sendPacketSize];
		}

		// Write a header onto the front of the buffer
		NetworkHeader* tunnelHeader = reinterpret_cast<NetworkHeader*>(m_messageBuffer.get());
		tunnelHeader->m_messageID = MessageID::Tunnel;
		tunnelHeader->m_priority = priority;
		tunnelHeader->m_reliability = reliability;
		tunnelHeader->m_channel = channel;

		NetworkHeader* broadcastHeader = tunnelHeader + 1;
		broadcastHeader->m_messageID = MessageID::Broadcast;
		broadcastHeader->m_priority = priority;
		broadcastHeader->m_reliability = reliability;
		broadcastHeader->m_channel = channel;

		// Write the message onto the rest of the buffer
		byte* payload = m_messageBuffer.get() + totalHeaderSize;
		memcpy(payload, msg->GetData(), msgSize);

		// Send the constructed packet
		m_netConnection->GetSocket()->Send(m_messageBuffer.get(), sendPacketSize, priority, reliability, channel);
	}
	else
	{
		LogError("Trying to send a message to a remote host that is not connected");
	}

	if (releaseMessage)
	{
		m_netConnection->ReturnMessage(msg);
	}
}


void TunnelConnection::AddListener(byte messageType, NetworkConnectionListener* newListener)
{
	// If the message ID being registered for is outside the valid range, then set it to be
	// StatusOnly messages: the listener will still get connect/disconnect notifications, but will not receive any messages
	if (messageType < MessageID::Start)
	{
		messageType = MessageID::StatusOnly;
	}

	ListenerListPtr list = m_listeners[messageType];
	if (!list)
	{
		list = ListenerList::Create();
		m_listeners[messageType] = list;
	}

	list->AddListener(newListener);
}


void TunnelConnection::RemoveListener(byte messageType, NetworkConnectionListener* oldListener)
{
	// If the message ID being registered for is outside the valid range, then set it to be
	// StatusOnly messages: the listener will still get connect/disconnect notifications, but will not receive any messages
	if (messageType < MessageID::Start)
	{
		messageType = MessageID::StatusOnly;
	}

	m_listeners[messageType]->RemoveListener(oldListener);
}


bool TunnelConnection::RegisterAsyncCallback(byte messageType, NetworkConnectionListener* cb)
{
	XT_UNREFERENCED_PARAM(messageType);
	XT_UNREFERENCED_PARAM(cb);
	XTASSERT(false);	// Time constrained; implement when needed
	return false;
}


void TunnelConnection::UnregisterAsyncCallback(byte messageType)
{
	XT_UNREFERENCED_PARAM(messageType);
	XTASSERT(false);	// Time constrained; implement when needed
}


NetworkOutMessagePtr TunnelConnection::CreateMessage(byte messageType)
{
	return m_netConnection->CreateMessage(messageType);
}


void TunnelConnection::ReturnMessage(const NetworkOutMessagePtr& msg)
{
	m_netConnection->ReturnMessage(msg);
}


void TunnelConnection::Disconnect()
{
	m_remoteSystemConnected = false;
}


XStringPtr TunnelConnection::GetRemoteAddress() const
{
	return m_netConnection->GetRemoteAddress();
}


const XSocketPtr& TunnelConnection::GetSocket() const
{
	return m_netConnection->GetSocket();
}


void TunnelConnection::SetSocket(const XSocketPtr&)
{
	// The connection should not be set on tunnel connections
	XTASSERT(false);
}


void TunnelConnection::OnConnected(const NetworkConnectionPtr&)
{
    // Note: we don't notify listeners about the connection until we have received a 
	// message telling us the the tunnel has been fully connected
}


void TunnelConnection::OnConnectFailed(const NetworkConnectionPtr&)
{
	m_remoteSystemConnected = false;

	// Prevent this object from getting destroyed while iterating through callbacks
	NetworkConnectionPtr thisPtr(this);

	for (auto it = m_listeners.begin(); it != m_listeners.end(); ++it)
	{
		it->second->NotifyListeners(&NetworkConnectionListener::OnConnectFailed, thisPtr);
	}
}


void TunnelConnection::OnDisconnected(const NetworkConnectionPtr&)
{
	if (m_remoteSystemConnected)
	{
		m_remoteSystemConnected = false;
		NotifyDisconnected();
	}
}


void TunnelConnection::OnMessageReceived(const NetworkConnectionPtr&, NetworkInMessage& message)
{
	// Look at which MessageID this message is using
	MessageID msgID = static_cast<MessageID>(*message.GetData());

	if (msgID == MessageID::Tunnel)
	{
		const NetworkHeader* header = reinterpret_cast<NetworkHeader*>(message.GetData());

		// NOTE: it is possible to receive tunneled messages BEFORE we get the RemotePeerConnected notification 
		// when the messages are sent as unreliable and/or in a different channel than Default.  Check for those cases here.
		XTASSERT(m_remoteSystemConnected ||
			header->m_channel != MessageChannel::Default ||
			header->m_reliability != MessageReliability::ReliableOrdered);
		XT_UNREFERENCED_PARAM(header);

		if (m_remoteSystemConnected)
		{
			// Extract the payload
			const uint32 headerSize = sizeof(NetworkHeader);
			const uint32 payloadSize = message.GetSize() - headerSize;
			byte* payload = message.GetData() + headerSize;

			// Wrap the message in a NetworkMessage on the stack and call the callback.
			NetworkInMessageImpl unwrappedMsg(payload, payloadSize);

			// Read the message ID off the front
			byte payloadMsgID = unwrappedMsg.ReadByte();

			auto callbackIter = m_listeners.find(payloadMsgID);
			if (callbackIter != m_listeners.end())
			{
				callbackIter->second->NotifyListeners(&NetworkConnectionListener::OnMessageReceived, this, unwrappedMsg);
			}
		}
	}
	else if (msgID == MessageID::TunnelControl)
	{
		TunnelMsgType msgType = static_cast<TunnelMsgType>(message.ReadByte());

		if (msgType == RemotePeerConnected)
		{
			// Both ends of the tunnel are connected
			m_remoteSystemConnected = true;
			NotifyConnected();
		}
		else if (msgType == RemotePeerDisconnected)
		{
			// The connection on the other side of the bridge was lost
			m_remoteSystemConnected = false;
			NotifyDisconnected();
		}
	}
}


void TunnelConnection::NotifyConnected()
{
	// Prevent this object from getting destroyed while iterating through callbacks
	NetworkConnectionPtr thisPtr(this);

	for (auto it = m_listeners.begin(); it != m_listeners.end(); ++it)
	{
		it->second->NotifyListeners(&NetworkConnectionListener::OnConnected, thisPtr);
	}
}


void TunnelConnection::NotifyDisconnected()
{
	// Prevent this object from getting destroyed while iterating through callbacks
	NetworkConnectionPtr thisPtr(this);

	for (auto it = m_listeners.begin(); it != m_listeners.end(); ++it)
	{
		it->second->NotifyListeners(&NetworkConnectionListener::OnDisconnected, thisPtr);
	}
}


XTOOLS_NAMESPACE_END