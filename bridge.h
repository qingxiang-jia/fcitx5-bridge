#pragma once

#include "msgs.pb.h"
#include <cstdint>
#include <fcitx-utils/eventdispatcher.h>
#include <fcitx-utils/inputbuffer.h>
#include <fcitx-utils/macros.h>
#include <fcitx/addonfactory.h>
#include <fcitx/addoninstance.h>
#include <fcitx/addonmanager.h>
#include <fcitx/candidatelist.h>
#include <fcitx/event.h>
#include <fcitx/inputcontext.h>
#include <fcitx/inputcontextproperty.h>
#include <fcitx/inputmethodengine.h>
#include <fcitx/inputpanel.h>
#include <fcitx/instance.h>
#include <memory>
#include <shared_mutex>
#include <thread>
#include <vector>
#include <zmq.hpp>

class Engine;
class Server;

class Engine : public fcitx::InputMethodEngineV2 {
public:
  Engine(fcitx::Instance *instance);
  ~Engine();

  void activate(const fcitx::InputMethodEntry &entry,
                fcitx::InputContextEvent &event) override;
  void keyEvent(const fcitx::InputMethodEntry &entry,
                fcitx::KeyEvent &keyEvent) override;
  void reset(const fcitx::InputMethodEntry &,
             fcitx::InputContextEvent &event) override;

  fcitx::InputContext *getInputContext();
  fcitx::Instance *getInstance();
  std::unique_ptr<fcitx::CommonCandidateList> makeCandidateList();

private:
  fcitx::Instance *instance_;
  fcitx::InputContext *ic;
  zmq::context_t *ctx;
  zmq::socket_t *sock;
  Server *server;
  fcitx::EventDispatcher *dispatcher;
};

class Server {
public:
  Server();
  void setEngine(Engine *engine);
  void setDispatcher(fcitx::EventDispatcher *dispatcher);
  void serve();
  ~Server();

private:
  zmq::context_t *ctx;
  zmq::socket_t *sock;
  Engine *engine;
  fcitx::EventDispatcher *dispatcher;
  void dispatch(CommandToFcitx *);
};

class EngineFactory : public fcitx::AddonFactory {
  fcitx::AddonInstance *create(fcitx::AddonManager *manager) override {
    return new Engine(manager->instance());
  }
};
