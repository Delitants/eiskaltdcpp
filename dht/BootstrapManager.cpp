/*
 * Copyright (C) 2009-2010 Big Muscle, http://strongdc.sourceforge.net/
 * Copyright (C) 2019 Boris Pek <tehnick-8@yandex.ru>
 * Copyright (C) 2026 Joe Rivera <transfix@sublevels.net>
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

#include "stdafx.h"
#include "BootstrapManager.h"
#include "Constants.h"
#include "DHT.h"
#include "SearchManager.h"
#include "dcpp/AdcCommand.h"
#include "dcpp/ClientManager.h"
#include "dcpp/HttpConnection.h"
#include "dcpp/LogManager.h"
#include "dcpp/SettingsManager.h"
#include <zlib.h>
#include <algorithm>
#include "dcpp/DCPlusPlus.h"

namespace dht
{
    namespace {
        constexpr size_t MAX_BOOTSTRAP_COMPRESSED_BYTES = 1024 * 1024;
        constexpr uLongf MAX_BOOTSTRAP_DECOMPRESSED_BYTES = 4 * 1024 * 1024;
        constexpr size_t MAX_BOOTSTRAP_NODES = 4096;
    }

    vector<string> BootstrapManager::parseServers(const string& configured)
    {
        vector<string> parsed;
        string::size_type start = 0;
        while(start < configured.size())
        {
            const auto end = configured.find(';', start);
            string url = configured.substr(start, end == string::npos ? string::npos : end - start);

            const auto first = url.find_first_not_of(" \t\r\n");
            const auto last = url.find_last_not_of(" \t\r\n");
            if(first != string::npos && last != string::npos)
            {
                url = url.substr(first, last - first + 1);
                if(std::find(parsed.begin(), parsed.end(), url) == parsed.end())
                    parsed.push_back(url);
            }

            if(end == string::npos)
                break;
            start = end + 1;
        }
        return parsed;
    }

    string BootstrapManager::buildBootstrapUrl(const string& server, const string& cid,
        const string& advertisedPort, bool advertisePort)
    {
        string url = server + "?cid=" + cid + "&encryption=1";
        if(advertisePort)
            url += "&u4=" + advertisedPort;
        return url;
    }
 
    BootstrapManager::BootstrapManager(DHT& dht) : dht_(dht), httpConnection(dht.ctx())
    {
        httpConnection.addListener(this);

        servers = parseServers(dht_.ctx().getSettingsManager()->get(SettingsManager::DHT_BOOTSTRAP_URLS, true));
    }

    BootstrapManager::~BootstrapManager(void)
    {
        httpConnection.removeListener(this);
    }

    void BootstrapManager::bootstrap()
    {
        if(servers.empty())
            return;

        {
            Lock l(cs);
            if(requestActive || !bootstrapNodes.empty())
                return;

            nodesXML.clear();
            requestActive = true;
        }

        dht_.ctx().getLogManager()->message(_("DHT bootstrapping started"));
        string dhturl = servers[Util::rand(servers.size())];
        const bool advertisePort = dht_.ctx().getClientManager()->isActive(Util::emptyString) ||
            dht_.hasUdpProxyEndpoint();
        string url = buildBootstrapUrl(dhturl,
            dht_.ctx().getClientManager()->getMe()->getCID().toBase32(),
            dht_.getAdvertisedPort(), advertisePort);

        httpConnection.downloadFile(url);
    }

    void BootstrapManager::on(HttpConnectionListener::Data, HttpConnection* conn, const uint8_t* buf, size_t len) throw()
    {
        bool abortRequest = false;
        bool responseTooLarge = false;

        {
            Lock l(cs);
            if(!requestActive)
            {
                abortRequest = true;
            }
            else if(len > MAX_BOOTSTRAP_COMPRESSED_BYTES || nodesXML.size() > MAX_BOOTSTRAP_COMPRESSED_BYTES - len)
            {
                nodesXML.clear();
                requestActive = false;
                abortRequest = true;
                responseTooLarge = true;
            }
            else
            {
                nodesXML.append(reinterpret_cast<const char*>(buf), len);
            }
        }

        if(abortRequest)
        {
            conn->abort();
            if(responseTooLarge)
                dht_.ctx().getLogManager()->message(string(_("DHT bootstrap error: ")) + "response is too large");
            return;
        }
    }

    #define BUFSIZE 16384
    void BootstrapManager::on(HttpConnectionListener::Complete, HttpConnection*, string const&) throw()
    {
        string compressedNodesXML;

        {
            Lock l(cs);
            requestActive = false;
            compressedNodesXML.swap(nodesXML);
        }

        if(!compressedNodesXML.empty())
        {
            try
            {
                uLongf destLen = BUFSIZE;
                std::unique_ptr<uint8_t[]> destBuf;

                // decompress incoming packet
                int result;

                do
                {
                    if(destLen >= MAX_BOOTSTRAP_DECOMPRESSED_BYTES)
                        throw Exception("Decompressed response is too large.");

                    destLen *= 2;
                    destLen = std::min<uLongf>(destLen, MAX_BOOTSTRAP_DECOMPRESSED_BYTES);
                    destBuf.reset(new uint8_t[destLen]);

                    result = uncompress(&destBuf[0], &destLen, (Bytef*)compressedNodesXML.data(), compressedNodesXML.length());
                }
                while (result == Z_BUF_ERROR);

                if(result != Z_OK)
                {
                    // decompression error!!!
                    throw Exception("Decompress error.");
                }

                SimpleXML remoteXml;
                remoteXml.fromXML(string((char*)&destBuf[0], destLen));
                remoteXml.stepIn();

                size_t nodeCount = 0;
                while(remoteXml.findChild("Node"))
                {
                    if(nodeCount >= MAX_BOOTSTRAP_NODES)
                    {
                        dht_.ctx().getLogManager()->message(string(_("DHT bootstrap error: ")) + "response contains too many nodes; ignored the rest");
                        break;
                    }

                    CID cid     = CID(remoteXml.getChildAttrib("CID"));
                    string i4   = remoteXml.getChildAttrib("I4");
                    string u4   = remoteXml.getChildAttrib("U4");

                    addBootstrapNode(i4, u4, cid, UDPKey());
                    ++nodeCount;
                }

                remoteXml.stepOut();

                dht_.ctx().getLogManager()->message(_("DHT bootstrapping is finished successfully."));
            }
            catch(Exception& e)
            {
                dht_.ctx().getLogManager()->message(_("DHT bootstrap error: ") + e.getError());
            }
        }
    }

    void BootstrapManager::on(HttpConnectionListener::Failed, HttpConnection*, const string& aLine) throw()
    {
        {
            Lock l(cs);
            requestActive = false;
            nodesXML.clear();
        }

        dht_.ctx().getLogManager()->message(_("DHT bootstrap error: ") + aLine);
    }

    void BootstrapManager::addBootstrapNode(const string& ip, const string& udpPort, const CID& targetCID, const UDPKey& udpKey)
    {
        BootstrapNode node = { ip, udpPort, targetCID, udpKey };
        Lock l(cs);
        bootstrapNodes.push_back(node);
    }

    void BootstrapManager::process()
    {
        BootstrapNode node;

        {
            Lock l(cs);
            if(bootstrapNodes.empty())
                return;

            node = bootstrapNodes.front();
            bootstrapNodes.pop_front();
        }

        // send bootstrap request
        AdcCommand cmd(AdcCommand::CMD_GET, AdcCommand::TYPE_UDP);
        cmd.addParam("nodes");
        cmd.addParam("dht.xml");

        CID key;
        // if our external IP changed from the last time, we can't encrypt packet with this key
        // this won't probably work now
        if(dht_.getLastExternalIP() == node.udpKey.ip)
            key = node.udpKey.key;

        dht_.send(cmd, node.ip, node.udpPort, node.cid, key);
    }

}
