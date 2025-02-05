#pragma once

#include "WebSocketClient.hpp"

#include "MessageHandler.hpp"

#include <functional>
#include <memory>

namespace ix
{
  class WebSocketMessage;
  using WebSocketMessagePtr = std::unique_ptr<WebSocketMessage>;
};

namespace yaodaq
{
  class MessageHandlerClient : public WebSocketClient, public MessageHandler
  {
  public:
    MessageHandlerClient(const Identifier& identifier);
    virtual void onOwnMessage(const ix::WebSocketMessagePtr& msg);
    virtual void onOwnOpen(const ix::WebSocketMessagePtr& msg);
    virtual void onOwnClose(const ix::WebSocketMessagePtr& msg);
    virtual void onOwnConnectionError(const ix::WebSocketMessagePtr& msg);
    virtual void onOwnPing(const ix::WebSocketMessagePtr& msg);
    virtual void onOwnPong(const ix::WebSocketMessagePtr& msg);
    virtual void onOwnFragment(const ix::WebSocketMessagePtr& msg);
  protected:
    std::function<void(const ix::WebSocketMessagePtr&)> getMessageCallback();
  private:
    std::function<void(const ix::WebSocketMessagePtr&)> m_MessageCallback;
  };
};
