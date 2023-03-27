//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2023
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/telegram/DialogFilterInviteLink.h"

#include "td/telegram/MessagesManager.h"
#include "td/telegram/Td.h"

namespace td {

DialogFilterInviteLink::DialogFilterInviteLink(
    Td *td, telegram_api::object_ptr<telegram_api::exportedChatlistInvite> exported_invite) {
  CHECK(exported_invite != nullptr);
  invite_link_ = std::move(exported_invite->url_);
  title_ = std::move(exported_invite->title_);
  for (const auto &peer : exported_invite->peers_) {
    DialogId dialog_id(peer);
    if (dialog_id.is_valid()) {
      td->messages_manager_->force_create_dialog(dialog_id, "DialogFilterInviteLink");
      dialog_ids_.push_back(dialog_id);
    }
  }
}

td_api::object_ptr<td_api::chatFilterInviteLink> DialogFilterInviteLink::get_chat_filter_invite_link_object() const {
  return td_api::make_object<td_api::chatFilterInviteLink>(
      invite_link_, title_, transform(dialog_ids_, [](DialogId dialog_id) { return dialog_id.get(); }));
}

bool operator==(const DialogFilterInviteLink &lhs, const DialogFilterInviteLink &rhs) {
  return lhs.invite_link_ == rhs.invite_link_ && lhs.title_ == rhs.title_ && lhs.dialog_ids_ == rhs.dialog_ids_;
}

bool operator!=(const DialogFilterInviteLink &lhs, const DialogFilterInviteLink &rhs) {
  return !(lhs == rhs);
}

StringBuilder &operator<<(StringBuilder &string_builder, const DialogFilterInviteLink &invite_link) {
  return string_builder << "FolderInviteLink[" << invite_link.invite_link_ << '(' << invite_link.title_ << ')'
                        << invite_link.dialog_ids_ << ']';
}

}  // namespace td