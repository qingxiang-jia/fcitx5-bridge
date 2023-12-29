#include "bridge.h"
#include "msgs.pb.h"
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <fcitx-utils/eventdispatcher.h>
#include <fcitx-utils/i18n.h>
#include <fcitx-utils/key.h>
#include <fcitx-utils/keysymgen.h>
#include <fcitx-utils/log.h>
#include <fcitx-utils/macros.h>
#include <fcitx-utils/utf8.h>
#include <fcitx/candidatelist.h>
#include <fcitx/inputcontext.h>
#include <fcitx/inputpanel.h>
#include <fcitx/instance.h>
#include <fcitx/text.h>
#include <fcitx/userinterfacemanager.h>
#include <functional>
#include <future>
#include <google/protobuf/descriptor.h>
#include <google/protobuf/io/coded_stream.h>
#include <google/protobuf/io/zero_copy_stream.h>
#include <google/protobuf/message.h>
#include <iostream>
#include <memory>
#include <new>
#include <thread>
#include <utility>
#include <vector>
#include <zmq.h>
#include <zmq.hpp>

class Candidate : public fcitx::CandidateWord {
public:
  Candidate(fcitx::Text text) { setText(text); }
  void select(fcitx::InputContext *) const {};
};

Engine *engine;

Engine::Engine(fcitx::Instance *instance) : instance_(instance) {
  engine = this;

  ctx = new zmq::context_t();
  pub = new zmq::socket_t(*ctx, ZMQ_PUB);
  pub->bind("tcp://127.0.0.1:8085");

  fcitx::EventDispatcher *dispatcher = new fcitx::EventDispatcher();
  this->dispatcher = dispatcher;
  dispatcher->attach(&instance_->eventLoop());

  server = new Server();
  server->setEngine(this);
  server->setDispatcher(dispatcher);
  std::thread serverThread(&Server::serve, server);
  serverThread.detach();
}

Engine::~Engine() {
  pub->close();
  ctx->shutdown();
  ctx->close();
  delete pub;
  delete ctx;
  delete server;
  dispatcher->detach();
  delete dispatcher;
}

void Engine::activate(const fcitx::InputMethodEntry &entry,
                      fcitx::InputContextEvent &event) {
  FCITX_UNUSED(entry);
  ic = event.inputContext();
}

void Engine::keyEvent(const fcitx::InputMethodEntry &entry,
                      fcitx::KeyEvent &keyEvent) {
  FCITX_UNUSED(entry);

  if (keyEvent.isRelease() || keyEvent.key().isModifier()) {
    return;
  }

  fcitx::KeySym key = keyEvent.key().sym();
  KeyEvent msg;
  msg.set_key(key);
  std::string serialized = msg.SerializeAsString();

  zmq::message_t keyMsg(serialized.size());
  memcpy(keyMsg.data(), serialized.data(), serialized.size());
  pub->send(keyMsg, zmq::send_flags::dontwait);

  keyEvent.filterAndAccept();
}

void Engine::reset(const fcitx::InputMethodEntry &,
                   fcitx::InputContextEvent &event) {
  FCITX_UNUSED(event);
}

fcitx::InputContext *Engine::getInputContext() { return ic; }

fcitx::Instance *Engine::getInstance() { return instance_; }

std::unique_ptr<fcitx::CommonCandidateList> Engine::makeCandidateList() {
  auto candidateList = std::make_unique<fcitx::CommonCandidateList>();
  candidateList->setLabels(
      std::vector<std::string>{"1. ", "2. ", "3. ", "4. ", "5. "});
  candidateList->setCursorPositionAfterPaging(
      fcitx::CursorPositionAfterPaging::ResetToFirst);
  candidateList->setPageSize(instance_->globalConfig().defaultPageSize());

  return candidateList;
}

Server::Server() {
  ctx = new zmq::context_t();
  rep = new zmq::socket_t(*ctx, ZMQ_REP);
  rep->bind("tcp://127.0.0.1:8086");
}

Server::~Server() {
  rep->close();
  ctx->shutdown();
  ctx->close();
  delete rep;
  delete ctx;
}

void Server::setEngine(Engine *engine) { this->engine = engine; }

void Server::setDispatcher(fcitx::EventDispatcher *dispatcher) {
  this->dispatcher = dispatcher;
}

void Server::dispatch(CommandToFcitx *cmd) {
  if (engine == nullptr || engine->getInputContext() == nullptr) {
    return;
  }
  // Thanks to the author of Fcitx5 for pointing out the correct way of working
  // with EventDispatcher.
  dispatcher->schedule([engine = engine, cmd = *cmd]() {
    auto ic = engine->getInputContext();
    ic->inputPanel().reset();

    if (cmd.has_commit_text()) {
      auto text = cmd.commit_text().text();
      ic->commitString(text);
      return;
    }

    if (cmd.has_update_preedit()) {
      auto preedit = cmd.update_preedit().text();
      if (ic->capabilityFlags().test(fcitx::CapabilityFlag::Preedit)) {
        fcitx::Text text(preedit, fcitx::TextFormatFlag::HighLight);
        ic->inputPanel().setClientPreedit(text);
      } else {
        fcitx::Text text(preedit);
        ic->inputPanel().setPreedit(text);
      }
      ic->updatePreedit();
      return;
    }

    if (cmd.has_update_candidates()) {
      if (ic->inputPanel().candidateList() == nullptr) {
        ic->inputPanel().setCandidateList(engine->makeCandidateList());
      }
      auto clist = std::dynamic_pointer_cast<fcitx::CommonCandidateList>(
          ic->inputPanel().candidateList());
      clist->clear();
      auto candidates = cmd.update_candidates().candidates();
      for (int i = 0; i < candidates.size(); i++) {
        std::string candidate = candidates.Get(i);
        std::unique_ptr<fcitx::CandidateWord> fcitxCandidate =
            std::make_unique<Candidate>(fcitx::Text(candidate));
        clist->append(std::move(fcitxCandidate));
      }
    }
    ic->updateUserInterface(fcitx::UserInterfaceComponent::InputPanel);
  });
}

void Server::serve() {
  zmq::message_t *msg = new zmq::message_t();
  zmq::message_t *empty = new zmq::message_t();
  while (true) {
    // Receive request.
    zmq::recv_result_t maybeSize;
    try {
      maybeSize = rep->recv(*msg);
    } catch (const zmq::error_t &e) {
      exit(0);
    }
    if (!maybeSize.has_value()) {
      continue;
    }
    auto size = maybeSize.value();

    // Process request.
    auto data = msg->data();
    CommandToFcitx cmd;
    if (cmd.ParseFromArray(data, size)) {
      dispatch(&cmd);
    }

    // Signal process completion.
    maybeSize = rep->send(*empty, zmq::send_flags::none);
  }
}

FCITX_ADDON_FACTORY(EngineFactory);
