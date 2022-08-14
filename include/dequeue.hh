#pragma once

#include <condition_variable>
#include <cstddef>
#include <iterator>
#include <map>
#include <memory>
#include <optional>
#include <queue>
#include <shared_mutex>
#include <string>
#include <variant>

#include <libpq-fe.h>

#include <nix/sync.hh>
#include <nix/util.hh>

#include <event.hh>
#include <postgres.hh>

using std::condition_variable_any;
using std::map;
using std::monostate;
using std::optional;
using std::queue;
using std::shared_mutex;
using std::shared_ptr;
using std::string;
using std::unique_ptr;
using std::variant;

using nix::overloaded;
using nix::Sync;

namespace remote_build {
namespace dequeue {

struct PollingError {
  postgres::PollingError pg_err;
  PollingError(postgres::PollingError pg_err) : pg_err(pg_err) {}
  string msg() {
    return "polling postgres socket: " + postgres::err_msg(pg_err);
  };
};

struct ConsumingInput {
  string pg_msg;
  ConsumingInput(string pg_msg) : pg_msg(pg_msg) {}
  string msg() { return pg_msg; }
};

struct JsonDecodeError {
  string orig;
  string json_msg;
  JsonDecodeError(string orig, string json_msg)
      : orig(orig), json_msg(json_msg) {}
  string msg() { return json_msg + "got: " + orig; }
};

struct ParsingEvent {
  string m;
  ParsingEvent(string m) : m(m) {}
  string msg() { return "error parsing event: " + m; }
};

struct WrongChannel {
  string channel;
  WrongChannel(string channel) : channel(channel) {}
  string msg() { return "got message on unexpected channel: " + channel; }
};

struct EscapingChannel {
  string e;
  EscapingChannel(string e) : e(e) {}
  string msg() { return "escaping relname: " + e; }
};

struct NoMessages {
  string msg = "unexpectedly got no messages even though poll was ready";
};

typedef variant<PollingError, ConsumingInput, JsonDecodeError, ParsingEvent,
                WrongChannel, EscapingChannel, NoMessages>
    Error;

string err_msg(Error e);

typedef variant<Error, event::Event> ListenResult;

struct NonEmptyListenResults {
  ListenResult head;
  std::queue<ListenResult> tail;

  static NonEmptyListenResults singleton(ListenResult res) {
    return NonEmptyListenResults{
        .head = res,
        .tail = std::queue<ListenResult>(),
    };
  }
};

NonEmptyListenResults await_events(PGconn *conn, string const &channel_ident);

struct QueueState {
  PGconn *conn;

  shared_ptr<ListenResult> curr;

  unique_ptr<std::queue<ListenResult>> queue;

  const string channel_ident;

  QueueState(PGconn *conn, string const &channel_ident,
             NonEmptyListenResults events)
      : conn(conn), curr(std::make_shared<ListenResult>(events.head)),
        queue(std::make_unique<std::queue<ListenResult>>(events.tail)),
        channel_ident(channel_ident){};
};

struct Events {
  shared_ptr<PGconn> conn;
  const string channel_ident;

  Events(shared_ptr<PGconn> &&conn, string const &channel_ident)
      : conn(conn), channel_ident(channel_ident) {}

  struct Iterator {
    using iterator_category = std::input_iterator_tag;
    using difference_type = std::ptrdiff_t;
    using value_type = QueueState;
    using pointer = value_type *;
    using reference = value_type &;

    QueueState *state;

    Iterator(QueueState *state) : state(state) {}

    reference operator*() const { return *state; }
    pointer operator->() { return state; }

    Iterator &operator++() {
      if (this->state->queue->empty()) {
        auto next = await_events(this->state->conn, this->state->channel_ident);

        this->state->curr = std::make_shared<ListenResult>(next.head);

        this->state->queue =
            std::make_unique<std::queue<ListenResult>>(next.tail);
      }

      else {
        this->state->curr =
            std::make_shared<ListenResult>(this->state->queue->front());

        this->state->queue->pop();
      }

      return *this;
    };

    Iterator operator++(int) {
      auto tmp = *this;
      ++(*this);
      return tmp;
    }

    friend bool operator!=(const Iterator &a, const Iterator &b) {
      return &a.state->curr != &b.state->curr &&
             &a.state->queue != &b.state->queue;
    };
  };

  Iterator begin(std::queue<ListenResult> &&seed) {
    auto init = seed.empty()
                    ? await_events(this->conn.get(), this->channel_ident)
                    : NonEmptyListenResults{
                          .head = std::move(seed.front()),
                          .tail = {},
                      };

    if (!seed.empty()) {
      seed.pop();

      init.tail = std::move(seed);
    }

    return Events::Iterator(
        new QueueState(this->conn.get(), this->channel_ident, init));
  }

  Iterator begin() {
    auto init = await_events(this->conn.get(), this->channel_ident);

    return Events::Iterator(
        new QueueState(this->conn.get(), this->channel_ident, init));
  }
};

variant<string, Events>
listen_channel(postgres::ConnectionParams const &conn_params,
               const string &channel_ident);

template <typename T> struct Buffer {
private:
  std::queue<T> buf;
  shared_mutex mut;
  condition_variable_any ready;

public:
  Buffer() : buf(), mut(), ready() {}

  void push(T x) {
    std::unique_lock lock(this->mut);

    this->buf.push(x);

    this->ready.notify_one();
  }

  optional<T> try_front() {
    std::shared_lock lock(this->mut);

    if (buf.empty()) {
      return std::nullopt;
    }

    return buf.front();
  }

  T pop() {
    std::unique_lock lock(this->mut);

    this->ready.wait(lock, [this]() { return !this->buf.empty(); });

    auto front = this->buf.front();

    this->buf.pop();

    return front;
  }
};

typedef variant<string, std::queue<ListenResult>> GetEventsResult;

GetEventsResult get_events(PGconn *conn, Uuid const &job);

} // namespace dequeue
} // namespace remote_build
