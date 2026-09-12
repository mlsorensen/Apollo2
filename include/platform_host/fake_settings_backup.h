#pragma once

#include "core/settings_backup.h"

namespace host {

// Posable settings-backup source so the sim can render every state of the
// Backup page and its modals. Operations complete instantly.
class FakeSettingsBackup : public core::ISettingsBackup {
 public:
  core::BackupInfo info() const override { return info_; }

  void request_backup(bool include_wifi, bool include_token) override {
    op_ = core::BackupOp::kBackup;
    state_ = core::BackupState::kDone;
    info_.file_present = true;
    info_.has_wifi = include_wifi;
    info_.has_token = include_token;
    info_.saved_unix = 1789214040;  // 2026-09-12 08:14 UTC
    msg_ = "38 settings written";
  }

  void request_restore() override {
    op_ = core::BackupOp::kRestore;
    state_ = core::BackupState::kDone;
    msg_ = "38 settings restored";
  }

  core::BackupState state() const override { return state_; }
  core::BackupOp last_op() const override { return op_; }
  std::string message() const override { return msg_; }

  // --- sim posing ---------------------------------------------------------
  void pose_no_card() { info_ = {}; }
  void pose_empty_card() {
    info_ = {};
    info_.medium_ready = true;
  }
  void pose_backup_present(bool with_wifi = true, bool newer = false,
                           bool with_token = true) {
    info_.medium_ready = true;
    info_.file_present = true;
    info_.saved_unix = 1789214040;  // 2026-09-12 08:14 UTC
    info_.version = newer ? "0.15.0" : "0.14.0";
    info_.board = "p4-wifi6-touch-lcd-5";
    info_.has_wifi = with_wifi;
    info_.has_token = with_token;
    info_.newer_firmware = newer;
  }

 private:
  core::BackupInfo info_;
  core::BackupState state_ = core::BackupState::kIdle;
  core::BackupOp op_ = core::BackupOp::kNone;
  std::string msg_;
};

}  // namespace host
