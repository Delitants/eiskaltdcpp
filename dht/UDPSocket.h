/*
 * Copyright (C) 2009-2010 Big Muscle, http://strongdc.sourceforge.net/
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

#pragma once

#include "dcpp/AdcCommand.h"
#include "dcpp/CID.h"
#include "dcpp/FastAlloc.h"
#include "dcpp/MerkleTree.h"
#include "dcpp/Socket.h"
#include "dcpp/Thread.h"

#include <atomic>
#include <memory>
#include <mutex>

namespace dht
{
    class DHT;

    struct Packet :
        FastAlloc<Packet>
    {
        /** Public constructor */
        Packet(const string& ip_, const string& port_, const std::string& data_, const CID& _targetCID, const CID& _udpKey) :
            ip(ip_), port(port_), data(data_), targetCID(_targetCID), udpKey(_udpKey)
        {
        }

        /** IP where send this packet to */
        string ip;

        /** To which port this packet should be sent */
        string port;

        /** Data to sent */
        std::string data;

        /** CID of target node */
        CID targetCID;

        /** Key to encrypt packet */
        CID udpKey;

    };

    class UDPSocket :
        private Thread
    {
    public:
        struct SendObservation
        {
            string logicalIp;
            string logicalPort;
            string physicalIp;
            string physicalPort;
            bool proxied = false;
            size_t payloadBytes = 0;
        };

        class TransportObserver
        {
        public:
            virtual ~TransportObserver() = default;
            virtual void onSend(const SendObservation& observation) = 0;
        };

        UDPSocket(void);
        ~UDPSocket(void);

        /** Set back-reference to owning DHT (called once before listen()) */
        void setDHT(DHT& dht) { dht_ = &dht; }

        /** Disconnects UDP socket */
        void disconnect() throw();

        /** Starts listening to UDP socket */
        void listen();

        /** Returns the local UDP listen port. */
        const std::string& getPort() const { return port; }

        /** Returns the externally reachable port, including a SOCKS5 UDP relay. */
        std::string getAdvertisedPort() const;

        static std::string selectPort(const std::string& listenPort, const std::string& relayPort, bool advertiseRelay);

        /** Returns true when DHT can register a public UDP proxy endpoint. */
        bool hasUdpProxyEndpoint() const;

        /** Installs an optional observer for successfully sent UDP packets. */
        void setTransportObserver(std::shared_ptr<TransportObserver> observer);

        /** Sends command to ip and port */
        void send(AdcCommand& cmd, const string& ip, const string &port, const CID& targetCID, const CID& udpKey);

    private:

        std::unique_ptr<Socket> socket;

        DHT* dht_ = nullptr;

        /** Indicates to stop socket thread */
        bool stop;

        /** Port for communicating in this network */
        std::string port;

        /** Queue for sending packets through UDP socket */
        std::deque<Packet*> sendQueue;

        /** Antiflooding protection */
        uint64_t delay;

        /** Locks access to sending queue */
        CriticalSection cs;

#ifdef _DEBUG
        // debug constants to optimize bandwidth
        size_t sentBytes;
        size_t receivedBytes;

        size_t sentPackets;
        size_t receivedPackets;
#endif

        /** Thread for receiving UDP packets */
        int run();

        void checkIncoming();
        void checkOutgoing(uint64_t& timer);

        void compressPacket(const string& data, uint8_t* destBuf, unsigned long& destSize);
        void encryptPacket(const CID& targetCID, const CID& udpKey, uint8_t* destBuf, unsigned long& destSize);
        void notifyTransportObserver(const SendObservation& observation) const;

        bool decompressPacket(uint8_t* destBuf, unsigned long& destLen, const uint8_t* buf, size_t len);
        bool decryptPacket(uint8_t* buf, int& len, const string& remoteIp, bool& isUdpKeyValid);

        mutable std::mutex observerMutex;
        std::atomic<bool> observerEnabled { false };
        std::shared_ptr<TransportObserver> transportObserver;
    };

}
