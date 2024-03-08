//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2024
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include <td/telegram/Client.h>
#include <td/telegram/td_api.h>
#include <td/telegram/td_api.hpp>

#include <cstdint>
#include <functional>
#include <iostream>
#include <map>
#include <memory>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

// Simple single-threaded example of TDLib usage.
// Real world programs should use separate thread for the user input.
// Example includes user authentication, receiving updates, getting chat list and sending text messages.

// overloaded
namespace detail {
template <class... Fs>
struct overload;

template <class F>
struct overload<F> : public F {
  explicit overload(F f) : F(f) {
  }
};
template <class F, class... Fs>
struct overload<F, Fs...>
    : public overload<F>
    , public overload<Fs...> {
  overload(F f, Fs... fs) : overload<F>(f), overload<Fs...>(fs...) {
  }
  using overload<F>::operator();
  using overload<Fs...>::operator();
};
}  // namespace detail

template <class... F>
auto overloaded(F... f) {
  return detail::overload<F...>(f...);
}

namespace td_api = td::td_api;

class TdExample {
 public:
  TdExample() : chat_id_(110), from_message_id_(1), offset_(0), limit_(20) {
    td::ClientManager::execute(td_api::make_object<td_api::setLogVerbosityLevel>(1));
    client_manager_ = std::make_unique<td::ClientManager>();
    client_id_ = client_manager_->create_client_id();
    send_query(td_api::make_object<td_api::getOption>("version"), {});
  }

  void loop_response() {
    while (true) {
      if (need_restart_) {
        restart();
      } else if (!are_authorized_) {
        process_response(client_manager_->receive(10));
      }
      while (true) {  //asynchronise get response to request or incoming updates from TDlib
        auto response = client_manager_->receive(1);
        if (response.object) {
          process_response(std::move(response));
        } else {
          break;
        }
      }
    }  //end while
  }

  std::int64_t chat_id_;
  std::int64_t from_message_id_;
  std::int32_t offset_;
  std::int32_t limit_;

  void loop_his() {
    std::int64_t cur_mss_id = 0;
    while (true) {
      std::this_thread::sleep_for(std::chrono::seconds(3));
      if (cur_mss_id == from_message_id_)
        continue;

      auto his_mess = td_api::make_object<td_api::getChatHistory>();
      his_mess->chat_id_ = this->chat_id_;
      his_mess->offset_ = this->offset_;
      his_mess->limit_ = this->limit_;
      his_mess->from_message_id_ = this->from_message_id_;  // mess->messages_[mess->total_count_ - 1]->id_;
      his_mess->only_local_ = false;

      send_query(std::move(his_mess), [this](Object object) {
        if (object->get_id() == td_api::error::ID) {
          return;
        }
        auto mess = td::move_tl_object_as<td_api::messages>(object);
        if (mess->total_count_ <= 0)
          return;
        for (td_api::int32 i = 0; i < mess->total_count_; i++) {  //object_ptr<message>
          std::cout << "mes_id:" << mess->messages_[i]->id_ << ",mes_typeid_:"
                    << mess->messages_[i]->content_->get_id()
                    //case td_api::messageAudio::ID case td_api::messagePhoto::ID case td_api::messageVideo::ID
                    << ",is_bot:" << mess->messages_[i]->via_bot_user_id_
                    << ",mes_protected:" << mess->messages_[i]->can_be_saved_ << ",send_id:"
                    << static_cast<const td_api::messageSenderUser *>(mess->messages_[i]->sender_id_.get())->user_id_;
          switch (mess->messages_[i]->content_->get_id()) {
            case td_api::messageText::ID: {
              auto content = static_cast<const td_api::messageText *>(mess->messages_[i]->content_.get());
              std::cout << ",text:" << content->text_->text_ << std::endl;
              break;
            }
            case td_api::messageAudio::ID: {
              std::cout << "," << std::endl;
              break;
            }
            case td_api::messagePhoto::ID: {
              std::cout << "," << std::endl;
              break;
            }
            case td_api::messageVideo::ID: {
              /*
                    his 6183430019 0 0 1  得到最新数据 比如mesid=123
                    his 6183430019 123 0 3  获取从mesid=123开始的更早的 最多3条数据 ，并将获取的最早信息的mesid 作为下一次查询的from_message_id_ 值
                  */
              auto content = static_cast<const td_api::messageVideo *>(mess->messages_[i]->content_.get());
              std::cout << ",file_name_:" << content->video_->file_name_
                        << ",mime_type_:" << content->video_->mime_type_
                        << ",complete_down:" << content->video_->video_->local_->is_downloading_completed_
                        << ",down_id_:" << content->video_->video_->remote_->id_ << std::endl;
              break;
            }
            default: {
              std::cout << std::endl;
              break;
            }
          }  //end switch
        }    //end for

        if (mess->messages_[mess->total_count_ - 1]->id_ < this->from_message_id_)
          this->from_message_id_ = mess->messages_[mess->total_count_ - 1]->id_;
      });
    }
  }

  void loop() {
    while (true) {
      {
        std::cout << "Enter action [q] quit [c] show chats [m <chat_id> "
                     "<text>] send message [me <user_id>] show self [his <chat_id> <from_mssid> <offset> <limit>] show "
                     "history "
                     "messages [l] logout: "
                  << std::endl;
        std::string line;
        std::getline(std::cin, line);
        std::istringstream ss(line);
        std::string action;
        if (!(ss >> action)) {
          continue;
        }
        if (action == "q") {
          return;
        }
        if (action == "l") {
          std::cout << "Logging out..." << std::endl;
          send_query(td_api::make_object<td_api::logOut>(), {});
        } else if (action == "c") {
          std::cout << "Loading chat list..." << std::endl;
          send_query(td_api::make_object<td_api::getChats>(nullptr, 30), [this](Object object) {
            if (object->get_id() == td_api::error::ID) {
              return;
            }
            auto chats = td::move_tl_object_as<td_api::chats>(object);
            for (auto chat_id : chats->chat_ids_) {
              std::cout << "[chat_id:" << chat_id << "] [title:" << chat_title_[chat_id] << "]" << std::endl;
            }
          });
        } else if (action == "his") {
          //his 644767638 1 -10 11
          //his 644767638 24117248 0 10
          //std::int64_t chat_id;
          ss >> this->chat_id_;
          ss.get();

          //std::int64_t from_messid;
          ss >> this->from_message_id_;
          ss.get();

          //std::int64_t offset;
          ss >> this->offset_;
          ss.get();

          //std::int64_t limit;
          ss >> this->limit_;
          ss.get();

          std::cout << "Get History message from chat " << this->chat_id_ << " ... " << std::endl;
          //auto his_mess = td_api::make_object<td_api::getChatHistory>();
          // his_mess->chat_id_ = chat_id;
          // his_mess->offset_ = offset;
          // his_mess->limit_ = limit;
          // his_mess->from_message_id_ = from_messid;
          // his_mess->only_local_ = false;

          // send_query(std::move(his_mess), [this, chat_id, from_messid, limit](Object object) {
          //   if (object->get_id() == td_api::error::ID) {
          //     return;
          //   }
          //   auto mess = td::move_tl_object_as<td_api::messages>(object);
          //   std::cout << "Total count:" << mess->total_count_ << std::endl;
          //   if (mess->total_count_ <= 0)
          //     return;
          //   for (td_api::int32 i = 0; i < mess->total_count_; i++) {  //object_ptr<message>
          //     std::cout
          //         << "mes_id:" << mess->messages_[i]->id_ << ",mes_typeid_:"
          //         << mess->messages_[i]->content_->get_id()
          //         //case td_api::messageAudio::ID case td_api::messagePhoto::ID case td_api::messageVideo::ID
          //         << ",is_bot:" << mess->messages_[i]->via_bot_user_id_
          //         << ",mes_protected:" << mess->messages_[i]->can_be_saved_ << ",send_id:"
          //         << static_cast<const td_api::messageSenderUser *>(mess->messages_[i]->sender_id_.get())->user_id_;
          //     switch (mess->messages_[i]->content_->get_id()) {
          //       case td_api::messageText::ID: {
          //         auto content = static_cast<const td_api::messageText *>(mess->messages_[i]->content_.get());
          //         std::cout << ",text:" << content->text_->text_ << std::endl;
          //         break;
          //       }
          //       case td_api::messageAudio::ID: {
          //         std::cout << "," << std::endl;
          //         break;
          //       }
          //       case td_api::messagePhoto::ID: {
          //         std::cout << "," << std::endl;
          //         break;
          //       }
          //       case td_api::messageVideo::ID: {
          //         /*
          //           his 6183430019 0 0 1  得到最新数据 比如mesid=123
          //           his 6183430019 123 0 3  获取从mesid=123开始的更早的 最多3条数据 ，并将获取的最早信息的mesid 作为下一次查询的from_message_id_ 值
          //         */
          //         auto content = static_cast<const td_api::messageVideo *>(mess->messages_[i]->content_.get());
          //         std::cout << ",file_name_:" << content->video_->file_name_
          //                   << ",mime_type_:" << content->video_->mime_type_
          //                   << ",complete_down:" << content->video_->video_->local_->is_downloading_completed_
          //                   << ",down_id_:" << content->video_->video_->remote_->id_ << std::endl;
          //         break;
          //       }
          //       default: {
          //         std::cout << std::endl;
          //         break;
          //       }
          //     }  //end switch
          //   }    //end for
          //   if (mess->messages_[mess->total_count_ - 1]->id_ >= from_messid)
          //     return;
          //   auto his_mess = td_api::make_object<td_api::getChatHistory>();
          //   his_mess->chat_id_ = chat_id;
          //   his_mess->offset_ = 0;
          //   his_mess->limit_ = limit;
          //   his_mess->from_message_id_ = mess->messages_[mess->total_count_ - 1]->id_;
          //   his_mess->only_local_ = false;
          //   send_query(std::move(his_mess), {});
          // });
        }
      }
    }
  }

 private:
  using Object = td_api::object_ptr<td_api::Object>;
  std::unique_ptr<td::ClientManager> client_manager_;
  std::int32_t client_id_{0};

  td_api::object_ptr<td_api::AuthorizationState> authorization_state_;
  bool are_authorized_{false};
  bool need_restart_{false};
  std::uint64_t current_query_id_{0};
  std::uint64_t authentication_query_id_{0};

  std::map<std::uint64_t, std::function<void(Object)>> handlers_;

  std::map<std::int64_t, td_api::object_ptr<td_api::user>> users_;

  std::map<std::int64_t, std::string> chat_title_;

  std::mutex mutex_querid_;

  std::mutex mutex_hander_;

  void restart() {
    client_manager_.reset();
    //*this = TdExample();
  }

  void send_query(td_api::object_ptr<td_api::Function> f, std::function<void(Object)> handler) {
    auto query_id = next_query_id();
    if (handler) {
      std::lock_guard<std::mutex> ls(mutex_hander_);
      handlers_.emplace(query_id, std::move(handler));
    }
    client_manager_->send(client_id_, query_id, std::move(f));
  }

  void process_response(td::ClientManager::Response response) {
    if (!response.object) {
      return;
    }
    //std::cout << response.request_id << " " << to_string(response.object) << std::endl;
    if (response.request_id == 0) {  //incoming update from TDLib.
      return process_update(std::move(response.object));
    }
    //std::cout << response.request_id << "<-xx->" << to_string(response.object) << std::endl;
    std::lock_guard<std::mutex> ls(mutex_hander_);
    auto it = handlers_.find(response.request_id);
    if (it != handlers_.end()) {
      it->second(std::move(response.object));
      handlers_.erase(it);
    }
  }

  std::string get_user_name(std::int64_t user_id) const {
    auto it = users_.find(user_id);
    if (it == users_.end()) {
      return "unknown user";
    }
    return it->second->first_name_ + " " + it->second->last_name_;
  }

  std::string get_chat_title(std::int64_t chat_id) const {
    auto it = chat_title_.find(chat_id);
    if (it == chat_title_.end()) {
      return "unknown chat";
    }
    return it->second;
  }

  void process_update(td_api::object_ptr<td_api::Object> update) {
    td_api::downcast_call(
        *update, overloaded(
                     [this](td_api::updateAuthorizationState &update_authorization_state) {
                       authorization_state_ = std::move(update_authorization_state.authorization_state_);
                       on_authorization_state_update();
                     },
                     [this](td_api::updateNewChat &update_new_chat) {
                       chat_title_[update_new_chat.chat_->id_] = update_new_chat.chat_->title_;
                     },
                     [this](td_api::updateChatTitle &update_chat_title) {
                       chat_title_[update_chat_title.chat_id_] = update_chat_title.title_;
                     },
                     [this](td_api::updateUser &update_user) {
                       auto user_id = update_user.user_->id_;
                       users_[user_id] = std::move(update_user.user_);
                     },
                     [this](td_api::updateNewMessage &update_new_message) {
                       auto chat_id = update_new_message.message_->chat_id_;
                       std::string sender_name;
                       td_api::downcast_call(*update_new_message.message_->sender_id_,
                                             overloaded(
                                                 [this, &sender_name](td_api::messageSenderUser &user) {
                                                   sender_name = get_user_name(user.user_id_);
                                                 },
                                                 [this, &sender_name](td_api::messageSenderChat &chat) {
                                                   sender_name = get_chat_title(chat.chat_id_);
                                                 }));
                       std::string text;
                       if (update_new_message.message_->content_->get_id() == td_api::messageText::ID) {
                         text = static_cast<td_api::messageText &>(*update_new_message.message_->content_).text_->text_;
                       }
                       std::cout << "Receive message: [chat_id:" << chat_id << "] [from:" << sender_name << "] ["
                                 << text << "]" << std::endl;
                     },
                     [](auto &update) {}));
  }

  auto create_authentication_query_handler() {
    return [this, id = authentication_query_id_](Object object) {
      if (id == authentication_query_id_) {
        check_authentication_error(std::move(object));
      }
    };
  }

  void on_authorization_state_update() {
    authentication_query_id_++;
    td_api::downcast_call(*authorization_state_,
                          overloaded(
                              [this](td_api::authorizationStateReady &) {
                                are_authorized_ = true;
                                std::cout << "Authorization is completed" << std::endl;
                              },
                              [this](td_api::authorizationStateLoggingOut &) {
                                are_authorized_ = false;
                                std::cout << "Logging out" << std::endl;
                              },
                              [this](td_api::authorizationStateClosing &) { std::cout << "Closing" << std::endl; },
                              [this](td_api::authorizationStateClosed &) {
                                are_authorized_ = false;
                                need_restart_ = true;
                                std::cout << "Terminated" << std::endl;
                              },
                              [this](td_api::authorizationStateWaitPhoneNumber &) {
                                std::cout << "Enter phone number: " << std::flush;
                                std::string phone_number;
                                std::cin >> phone_number;
                                send_query(
                                    td_api::make_object<td_api::setAuthenticationPhoneNumber>(phone_number, nullptr),
                                    create_authentication_query_handler());
                              },
                              [this](td_api::authorizationStateWaitEmailAddress &) {
                                std::cout << "Enter email address: " << std::flush;
                                std::string email_address;
                                std::cin >> email_address;
                                send_query(td_api::make_object<td_api::setAuthenticationEmailAddress>(email_address),
                                           create_authentication_query_handler());
                              },
                              [this](td_api::authorizationStateWaitEmailCode &) {
                                std::cout << "Enter email authentication code: " << std::flush;
                                std::string code;
                                std::cin >> code;
                                send_query(td_api::make_object<td_api::checkAuthenticationEmailCode>(
                                               td_api::make_object<td_api::emailAddressAuthenticationCode>(code)),
                                           create_authentication_query_handler());
                              },
                              [this](td_api::authorizationStateWaitCode &) {
                                std::cout << "Enter authentication code: " << std::flush;
                                std::string code;
                                std::cin >> code;
                                send_query(td_api::make_object<td_api::checkAuthenticationCode>(code),
                                           create_authentication_query_handler());
                              },
                              [this](td_api::authorizationStateWaitRegistration &) {
                                std::string first_name;
                                std::string last_name;
                                std::cout << "Enter your first name: " << std::flush;
                                std::cin >> first_name;
                                std::cout << "Enter your last name: " << std::flush;
                                std::cin >> last_name;
                                send_query(td_api::make_object<td_api::registerUser>(first_name, last_name, false),
                                           create_authentication_query_handler());
                              },
                              [this](td_api::authorizationStateWaitPassword &) {
                                std::cout << "Enter authentication password: " << std::flush;
                                std::string password;
                                std::getline(std::cin, password);
                                send_query(td_api::make_object<td_api::checkAuthenticationPassword>(password),
                                           create_authentication_query_handler());
                              },
                              [this](td_api::authorizationStateWaitOtherDeviceConfirmation &state) {
                                std::cout << "Confirm this login link on another device: " << state.link_ << std::endl;
                              },
                              [this](td_api::authorizationStateWaitTdlibParameters &) {
                                auto request = td_api::make_object<td_api::setTdlibParameters>();
                                request->database_directory_ = "tdlib";
                                request->use_message_database_ = true;
                                request->use_secret_chats_ = true;
                                request->api_id_ = 29479177;
                                request->api_hash_ = "84c4476df12afff19917a1ca71da39e3";
                                request->system_language_code_ = "JP";
                                request->device_model_ = "Andro";
                                request->application_version_ = "1.0";
                                send_query(std::move(request), create_authentication_query_handler());
                              }));
  }

  void check_authentication_error(Object object) {
    if (object->get_id() == td_api::error::ID) {
      auto error = td::move_tl_object_as<td_api::error>(object);
      std::cout << "Error: " << to_string(error) << std::flush;
      on_authorization_state_update();
    }
  }

  std::uint64_t next_query_id() {
    std::lock_guard<std::mutex> lc(mutex_querid_);
    return ++current_query_id_;
  }
};

int main() {
  TdExample example;
  std::thread work_th_([&example](void) { example.loop_response(); });
  std::thread send_th_([&example]() { example.loop_his(); });
  example.loop();
  work_th_.join();
  send_th_.join();
}
