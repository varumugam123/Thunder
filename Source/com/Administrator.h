/*
 * If not stated otherwise in this file or this component's LICENSE file the
 * following copyright and licenses apply:
 *
 * Copyright 2020 RDK Management
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef __COM_ADMINISTRATOR_H
#define __COM_ADMINISTRATOR_H

#include "Messages.h"
#include "Module.h"

namespace WPEFramework {

namespace ProxyStub {

    class UnknownStub;
    class UnknownProxy;
}

namespace RPC {

#ifdef __DEBUG__
    enum { CommunicationTimeOut = Core::infinite }; // Time in ms. Forever
#else
    enum { CommunicationTimeOut = 10000 }; // Time in ms. 10 Seconden
#endif
    enum { CommunicationBufferSize = 8120 }; // 8K :-)

    class EXTERNAL Administrator {
    private:
        Administrator();
        Administrator(const Administrator&) = delete;
        Administrator& operator=(const Administrator&) = delete;

        typedef std::list<std::pair<uint32_t, Core::IUnknown*>> StubList;
        typedef std::list<ProxyStub::UnknownProxy*> ProxyList;
        typedef std::map<const Core::IPCChannel*, ProxyList> ChannelMap;
        typedef std::map<const Core::IPCChannel*, StubList> ReferenceMap;

        struct EXTERNAL IMetadata {
            virtual ~IMetadata(){};

            virtual ProxyStub::UnknownProxy* CreateProxy(const Core::ProxyType<Core::IPCChannel>& channel, const instance_id& implementation, const bool remoteRefCounted) = 0;
        };

        template <typename PROXY>
        class ProxyType : public IMetadata {
        private:
            ProxyType(const ProxyType<PROXY>& copy) = delete;
            ProxyType<PROXY>& operator=(const ProxyType<PROXY>& copy) = delete;

        public:
            ProxyType()
            {
            }
            virtual ~ProxyType()
            {
            }

        private:
            virtual ProxyStub::UnknownProxy* CreateProxy(const Core::ProxyType<Core::IPCChannel>& channel, const instance_id& implementation, const bool remoteRefCounted)
            {
                return (new PROXY(channel, implementation, remoteRefCounted))->Administration();
            }
        };

    public:
        virtual ~Administrator();

        static Administrator& Instance();

    public:
        template <typename ACTUALINTERFACE, typename PROXY, typename STUB>
        void Announce()
        {
            _adminLock.Lock();

            _stubs.insert(std::pair<uint32_t, ProxyStub::UnknownStub*>(ACTUALINTERFACE::ID, new STUB()));
            _proxy.insert(std::pair<uint32_t, IMetadata*>(ACTUALINTERFACE::ID, new ProxyType<PROXY>()));

            _adminLock.Unlock();
        }

        Core::ProxyType<InvokeMessage> Message()
        {
            return (_factory.Element());
        }

        void DeleteChannel(const Core::ProxyType<Core::IPCChannel>& channel, std::list<ProxyStub::UnknownProxy*>& pendingProxies);

        template <typename ACTUALINTERFACE>
        ACTUALINTERFACE* ProxyFind(const Core::ProxyType<Core::IPCChannel>& channel, const instance_id& impl)
        {
            ACTUALINTERFACE* result = nullptr;
            ProxyFind(channel, impl, ACTUALINTERFACE::ID, result);
            return (result);
        }
        ProxyStub::UnknownProxy* ProxyFind(const Core::ProxyType<Core::IPCChannel>& channel, const instance_id& impl, const uint32_t id, void*& interface);

        template <typename ACTUALINTERFACE>
        ProxyStub::UnknownProxy* ProxyInstance(const Core::ProxyType<Core::IPCChannel>& channel, const instance_id& impl, const bool outbound, ACTUALINTERFACE*& base)
        {
            void* proxyInterface;
            ProxyStub::UnknownProxy* result = ProxyInstance(channel, impl, outbound, ACTUALINTERFACE::ID, proxyInterface);
            base = reinterpret_cast<ACTUALINTERFACE*>(proxyInterface);
            return (result);
        }
        ProxyStub::UnknownProxy* ProxyInstance(const Core::ProxyType<Core::IPCChannel>& channel, const instance_id& impl, const bool outbound, const uint32_t id, void*& interface);

        // ----------------------------------------------------------------------------------------------------
        // Methods for the Proxy Environment
        // ----------------------------------------------------------------------------------------------------
        void AddRef(Core::ProxyType<Core::IPCChannel>& channel, void* impl, const uint32_t interfaceId);
        void Release(Core::ProxyType<Core::IPCChannel>& channel, void* impl, const uint32_t interfaceId);

        // ----------------------------------------------------------------------------------------------------
        // Methods for the Stub Environment
        // ----------------------------------------------------------------------------------------------------
        void Release(ProxyStub::UnknownProxy* proxy, Data::Output& response);
        void Invoke(Core::ProxyType<Core::IPCChannel>& channel, Core::ProxyType<InvokeMessage>& message);

        // ----------------------------------------------------------------------------------------------------
        // Methods for the Administration
        // ----------------------------------------------------------------------------------------------------
        // Stub method for entries that the Stub returns to the callee
        template <typename ACTUALINTERFACE>
        void RegisterInterface(Core::ProxyType<Core::IPCChannel>& channel, ACTUALINTERFACE* reference)
        {
            RegisterInterface(channel, reference, ACTUALINTERFACE::ID);
        }
        void RegisterInterface(Core::ProxyType<Core::IPCChannel>& channel, void* source, const uint32_t id) {
            RegisterUnknownInterface(channel, Convert(source, id), id);
        }

        bool StubFind(const Core::ProxyType<Core::IPCChannel>& channel, Core::ProxyType<InvokeMessage>& message);

        void UnregisterInterface(Core::ProxyType<Core::IPCChannel>& channel, const Core::IUnknown* source, const uint32_t interfaceId)
        {
            ReferenceMap::iterator index(_channelReferenceMap.find(channel.operator->()));

            if (index != _channelReferenceMap.end()) {
                StubList::iterator element(index->second.begin());

                while ( (element != index->second.end()) && ((element->first != interfaceId) || (element->second != source)) ) {
                    element++;
                }

                ASSERT(element != index->second.end());

                if (element != index->second.end()) {
                    index->second.erase(element);
                    if (index->second.size() == 0) {
                        _channelReferenceMap.erase(index);
                    }
                } else {
                    printf("====> Unregistering an interface [0x%x, %d] which has not been registered!!!\n", interfaceId, Core::ProcessInfo().Id());
                }
            } else {
                printf("====> Unregistering an interface [0x%x, %d] from a non-existing channel!!!\n", interfaceId, Core::ProcessInfo().Id());
            }
        }
        void UnregisterProxy(const ProxyStub::UnknownProxy& proxy);
        
   private:
        // ----------------------------------------------------------------------------------------------------
        // Methods for the Stub Environment
        // ----------------------------------------------------------------------------------------------------
        Core::IUnknown* Convert(void* rawImplementation, const uint32_t id);
       void RegisterUnknownInterface(Core::ProxyType<Core::IPCChannel>& channel, Core::IUnknown* source, const uint32_t id);

    private:
        // Seems like we have enough information, open up the Process communcication Channel.
        Core::CriticalSection _adminLock;
        std::map<uint32_t, ProxyStub::UnknownStub*> _stubs;
        std::map<uint32_t, IMetadata*> _proxy;
        Core::ProxyPoolType<InvokeMessage> _factory;
        ChannelMap _channelProxyMap;
        ReferenceMap _channelReferenceMap;
    };

    class EXTERNAL Job : public Core::IDispatch {
    public:
        Job()
            : _message()
            , _channel()
            , _handler(nullptr)
        {
        }
        Job(Core::IPCChannel& channel, const Core::ProxyType<Core::IIPC>& message, Core::IIPCServer* handler)
            : _message(message)
            , _channel(channel)
            , _handler(handler)
        {
        }
        Job(const Job& copy)
            : _message(copy._message)
            , _channel(copy._channel)
            , _handler(copy._handler)
        {
        }
        virtual ~Job()
        {
        }

        Job& operator=(const Job& rhs) {
            _message = rhs._message;
            _channel = rhs._channel;
            _handler = rhs._handler;

            return (*this);
        }

    public:
        static Core::ProxyType<Job> Instance()
        {
            return (_factory.Element());
        }
        void Clear()
        {
            _message.Release();
            _channel.Release();
            _handler = nullptr;
        }
        void Set(Core::IPCChannel& channel, const Core::ProxyType<Core::IIPC>& message, Core::IIPCServer* handler)
        {
            _message = message;
            _channel = Core::ProxyType<Core::IPCChannel>(channel);
            _handler = handler;
        }
        virtual void Dispatch() override
        {
            if (_message->Label() == InvokeMessage::Id()) {
                Invoke(_channel, _message);
            } else {
                ASSERT(_message->Label() == AnnounceMessage::Id());
                ASSERT(_handler != nullptr);

                _handler->Procedure(*_channel, _message);
            }
        }

		static void Invoke(Core::ProxyType<Core::IPCChannel>& channel, Core::ProxyType<Core::IIPC>& data)
		{
            Core::ProxyType<InvokeMessage> message(data);
            ASSERT(message.IsValid() == true);
            _administrator.Invoke(channel, message);
            channel->ReportResponse(data);

		}

    private:
        Core::ProxyType<Core::IIPC> _message;
        Core::ProxyType<Core::IPCChannel> _channel;
        Core::IIPCServer* _handler;

        static Core::ProxyPoolType<Job> _factory;
        static Administrator& _administrator;
    };

    class EXTERNAL InvokeServer : public Core::IIPCServer {
    public:
        InvokeServer(const InvokeServer&) = delete;
        InvokeServer& operator=(const InvokeServer&) = delete;

        InvokeServer(Core::IWorkerPool* workers)
            : _threadPoolEngine(*workers)
            , _handler(nullptr)
        {
            ASSERT(workers != nullptr);
        }
        ~InvokeServer()
        {
        }

        void Announcements(Core::IIPCServer* announces)
        {
            ASSERT((announces != nullptr) ^ (_handler != nullptr));
            _handler = announces;
        }

    private:
        virtual void Procedure(Core::IPCChannel& source, Core::ProxyType<Core::IIPC>& message)
        {
            Core::ProxyType<Job> job(Job::Instance());

            job->Set(source, message, _handler);
            _threadPoolEngine.Submit(Core::ProxyType<Core::IDispatch>(job));
        }

    private:
        Core::IWorkerPool& _threadPoolEngine;
        Core::IIPCServer* _handler;
    };

    template <const uint8_t THREADPOOLCOUNT, const uint32_t STACKSIZE, const uint32_t MESSAGESLOTS>
    class InvokeServerType : public Core::IIPCServer {
    public:
        InvokeServerType(const InvokeServerType<THREADPOOLCOUNT,STACKSIZE,MESSAGESLOTS>&) = delete;
        InvokeServerType<THREADPOOLCOUNT,STACKSIZE,MESSAGESLOTS>& operator = (const InvokeServerType<THREADPOOLCOUNT,STACKSIZE,MESSAGESLOTS>&) = delete;

        InvokeServerType()
            : _threadPoolEngine(THREADPOOLCOUNT,STACKSIZE,MESSAGESLOTS)
            , _handler(nullptr)
        {
            _threadPoolEngine.Run();
        }
        ~InvokeServerType()
        {
            _threadPoolEngine.Stop();
        }

        void Announcements(Core::IIPCServer* announces)
        {
            ASSERT((announces != nullptr) ^ (_handler != nullptr));
            _handler = announces;
        }

    private:
        virtual void Procedure(Core::IPCChannel& source, Core::ProxyType<Core::IIPC>& message)
        {
            if (_threadPoolEngine.Pending() >= ((MESSAGESLOTS * 80) / 100)) {
                TRACE_L1("_threadPoolEngine.Pending() == %d", _threadPoolEngine.Pending());
            }

            if (message->Label() == AnnounceMessage::Id()) {
	            _handler->Procedure(source, message);
	    } else {

                Core::ProxyType<Job> job(Job::Instance());

                job->Set(source, message, _handler);
                _threadPoolEngine.Submit(Core::ProxyType<Core::IDispatch>(job), Core::infinite);
            }        
        }

    private:
        Core::ThreadPool _threadPoolEngine;
        Core::IIPCServer* _handler;
    };
}

} // namespace RPC

#endif // __COM_ADMINISTRATOR_H
